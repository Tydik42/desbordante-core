#include "core/algorithms/gk_compact/optimizations/pairing_filter.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "core/util/logger.h"

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
#include <chrono>
#endif

namespace gk_compact {

using gk::model::VarKind;

PairingPlan BuildPairingPlan(GraphKey const& key) {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    bool const trace = util::logging::Initialize()->should_log(spdlog::level::trace);
    auto const started =
            trace ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
#endif
    PairingPlan plan{key, {}, {}, {}, 0};
    std::vector<TypeId> types;
    for (auto const& node : key.nodes)
        if (node.kind == VarKind::kEntity || node.kind == VarKind::kWildcard)
            types.push_back(node.type);
    std::ranges::sort(types);
    types.erase(std::unique(types.begin(), types.end()), types.end());
    for (auto type : types) plan.domain_specs.push_back({VarKind::kEntity, type, std::nullopt});
    plan.entity_domains = types.size();
    std::optional<std::size_t> value_domain;
    for (auto const& node : key.nodes) {
        switch (node.kind) {
            case VarKind::kEntity:
            case VarKind::kWildcard:
                plan.node_domains.push_back(static_cast<std::size_t>(
                        std::ranges::lower_bound(types, node.type) - types.begin()));
                break;
            case VarKind::kValue:
                if (!value_domain) {
                    value_domain = plan.domain_specs.size();
                    plan.domain_specs.push_back(node);
                }
                plan.node_domains.push_back(*value_domain);
                break;
            case VarKind::kConstant:
                plan.node_domains.push_back(plan.domain_specs.size());
                plan.domain_specs.push_back(node);
                break;
        }
    }
    plan.incident.resize(key.nodes.size());
    for (std::size_t index = 0; index < key.triples.size(); ++index) {
        auto const& triple = key.triples[index];
        plan.incident[triple.subject].push_back(index);
        if (triple.object != triple.subject) plan.incident[triple.object].push_back(index);
    }
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    if (trace)
        LOG_TRACE("Compact GK phase 'pairing_plan': {} ns, nodes={}, domains={}",
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - started)
                          .count(),
                  key.nodes.size(), plan.domain_specs.size());
#endif
    return plan;
}

PairingDomain BuildPairingDomain(PairingPlan const& plan, GraphView graph) {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    bool const trace = util::logging::Initialize()->should_log(spdlog::level::trace);
    auto const started =
            trace ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
#endif
    PairingDomain domain{graph, {0}, {}};
    std::vector<std::vector<VertexId>> groups(plan.domain_specs.size());
    auto const entity_specs = std::span(plan.domain_specs).first(plan.entity_domains);
    for (auto vertex : graph.EntityIds()) {
        auto const type = graph.GetType(vertex);
        auto const found = std::ranges::lower_bound(entity_specs, type, {}, &PatternNode::type);
        if (found != entity_specs.end() && found->type == type)
            groups[static_cast<std::size_t>(found - entity_specs.begin())].push_back(vertex);
    }
    for (std::size_t index = plan.entity_domains; index < plan.domain_specs.size(); ++index) {
        auto const& node = plan.domain_specs[index];
        if (node.kind == VarKind::kValue) {
            auto values = graph.VertexIds() | std::views::drop(graph.GetEntityCount());
            groups[index].assign(values.begin(), values.end());
        } else if (graph.ContainsVertex(*node.constant)) {
            groups[index].push_back(*node.constant);
        }
    }
    domain.offsets.reserve(groups.size() + 1);
    for (auto const& group : groups) domain.offsets.push_back(domain.offsets.back() + group.size());
    domain.vertices.reserve(domain.offsets.back());
    for (auto const& group : groups)
        domain.vertices.insert(domain.vertices.end(), group.begin(), group.end());
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    if (trace)
        LOG_TRACE("Compact GK phase 'pairing_domain': {} ns, graph_vertices={}, candidates={}",
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - started)
                          .count(),
                  graph.GetVertexCount(), domain.vertices.size());
#endif
    return domain;
}

void PairingFilter::Clear(std::size_t nodes) {
    relation_.offsets.assign(nodes + 1, 0);
    relation_.pairs.clear();
    states_.clear();
    common_values_.clear();
    queue_.clear();
    live_counts_.clear();
    queue_head_ = queue_tail_ = pending_ = 0;
}

void PairingFilter::Initialize(PairingPlan const& plan, PairingDomain const& first,
                               PairingDomain const& second) {
    Clear(plan.key.nodes.size());
    // Reject an empty shared domain before reserving any Cartesian products.
    for (std::size_t domain = 0; domain < plan.domain_specs.size(); ++domain) {
        auto const left = first.Candidates(domain), right = second.Candidates(domain);
        if (left.empty() || right.empty()) return;
        if (plan.domain_specs[domain].kind == VarKind::kValue) {
            std::ranges::set_intersection(left, right, std::back_inserter(common_values_));
            if (common_values_.empty()) return;
        }
    }
    for (std::size_t node = 0; node < plan.key.nodes.size(); ++node) {
        auto const domain = plan.node_domains[node];
        auto const& spec = plan.domain_specs[domain];
        auto const remaining = relation_.pairs.max_size() - relation_.offsets[node];
        std::size_t count = 1;
        if (spec.kind == VarKind::kEntity) {
            auto const left = first.Candidates(domain).size();
            auto const right = second.Candidates(domain).size();
            if (right > remaining / left)
                throw std::length_error("Compact GK pairing exceeds its candidate capacity");
            count = left * right;
        } else if (spec.kind == VarKind::kValue) {
            count = common_values_.size();
        }
        if (count > remaining)
            throw std::length_error("Compact GK pairing exceeds its candidate capacity");
        relation_.offsets[node + 1] = relation_.offsets[node] + count;
    }
    relation_.pairs.reserve(relation_.offsets.back());
    for (std::size_t node = 0; node < plan.key.nodes.size(); ++node) {
        auto const domain = plan.node_domains[node];
        auto const& spec = plan.domain_specs[domain];
        if (spec.kind == VarKind::kEntity) {
            for (auto left : first.Candidates(domain))
                for (auto right : second.Candidates(domain))
                    relation_.pairs.emplace_back(left, right);
        } else if (spec.kind == VarKind::kValue) {
            for (auto value : common_values_) relation_.pairs.emplace_back(value, value);
        } else {
            relation_.pairs.emplace_back(*spec.constant, *spec.constant);
        }
    }
    // Reset all candidates, including those removed during the preceding call.
    states_.assign(relation_.pairs.size(), CandidateState::kQueued);
    queue_.resize(relation_.pairs.size());
    std::iota(queue_.begin(), queue_.end(), Offset{0});
    pending_ = queue_.size();
    live_counts_.resize(plan.key.nodes.size());
    for (std::size_t node = 0; node < live_counts_.size(); ++node)
        live_counts_[node] = relation_.offsets[node + 1] - relation_.offsets[node];
}

std::optional<Offset> PairingFilter::FindCandidate(PatternNodeId node, VertexPair pair) const {
    auto const candidates = relation_.Candidates(node);
    auto const found = std::ranges::lower_bound(candidates, pair);
    if (found == candidates.end() || *found != pair) return std::nullopt;
    return relation_.offsets[node] + static_cast<std::size_t>(found - candidates.begin());
}

bool PairingFilter::ContainsLive(PatternNodeId node, VertexPair pair) const {
    auto const index = FindCandidate(node, pair);
    return index && states_[*index] != CandidateState::kRemoved;
}

void PairingFilter::Enqueue(PatternNodeId node, VertexPair pair) {
    auto const index = FindCandidate(node, pair);
    if (!index || states_[*index] != CandidateState::kLive) return;
    states_[*index] = CandidateState::kQueued;
    queue_[queue_tail_] = *index;
    if (++queue_tail_ == queue_.size()) queue_tail_ = 0;
    ++pending_;
}

void PairingFilter::NotifyDependents(PairingPlan const& plan, PatternNodeId node, VertexPair pair,
                                     GraphView first, GraphView second) {
    for (auto edge : plan.incident[node]) {
        auto const& triple = plan.key.triples[edge];
        if (triple.subject == node) {
            // A removed subject pair can support the incoming constraint of
            // object candidates. Both components must be adjacent with p.
            for (auto const& left : first.OutgoingFromMember(pair.first, triple.predicate))
                for (auto const& right : second.OutgoingFromMember(pair.second, triple.predicate))
                    Enqueue(triple.object, {left.neighbor, right.neighbor});
        }
        if (triple.object == node) {
            for (auto const& left : first.IncomingToMember(pair.first, triple.predicate))
                for (auto const& right : second.IncomingToMember(pair.second, triple.predicate))
                    Enqueue(triple.subject, {left.neighbor, right.neighbor});
        }
    }
}

bool PairingFilter::HasSupport(VertexPair pair, PredicateId predicate, PatternNodeId neighbor,
                               bool outgoing, GraphView first, GraphView second) const {
    // Anchors came from the prepared domains. Filter neighbors before taking
    // their Cartesian product, avoiding out-of-view combinations altogether.
    auto first_edges = outgoing ? first.OutgoingFromMember(pair.first, predicate)
                                : first.IncomingToMember(pair.first, predicate);
    auto second_edges = outgoing ? second.OutgoingFromMember(pair.second, predicate)
                                 : second.IncomingToMember(pair.second, predicate);
    for (auto const& left : first_edges)
        for (auto const& right : second_edges)
            if (ContainsLive(neighbor, {left.neighbor, right.neighbor})) return true;
    return false;
}

PairingRelation const& PairingFilter::Compute(PairingPlan const& plan, PairingDomain const& first,
                                              PairingDomain const& second) {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    bool const trace = util::logging::Initialize()->should_log(spdlog::level::trace);
    auto const started =
            trace ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
#endif
    Initialize(plan, first, second);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    auto const initial = relation_.pairs.size();
#endif
    auto finish = [&]() -> PairingRelation const& {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (trace)
            LOG_TRACE(
                    "Compact GK phase 'pairing': {} ns, initial_candidates={}, "
                    "surviving_candidates={}",
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - started)
                            .count(),
                    initial, relation_.pairs.size());
#endif
        return relation_;
    };
    if (relation_.pairs.empty()) return finish();
    auto const& key = plan.key;
    while (pending_ != 0) {
        auto const index = queue_[queue_head_];
        if (++queue_head_ == queue_.size()) queue_head_ = 0;
        --pending_;
        states_[index] = CandidateState::kLive;
        auto const node = static_cast<PatternNodeId>(
                std::ranges::upper_bound(relation_.offsets, index) - relation_.offsets.begin() - 1);
        auto const pair = relation_.pairs[index];
        bool supported = true;
        for (auto edge : plan.incident[node]) {
            auto const& triple = key.triples[edge];
            bool const forward =
                    triple.subject != node || HasSupport(pair, triple.predicate, triple.object,
                                                         true, first.graph, second.graph);
            bool const backward =
                    triple.object != node || HasSupport(pair, triple.predicate, triple.subject,
                                                        false, first.graph, second.graph);
            if (forward && backward) continue;
            supported = false;
            break;
        }
        if (supported) continue;
        states_[index] = CandidateState::kRemoved;
        if (--live_counts_[node] == 0) {
            Clear(key.nodes.size());
            return finish();
        }
        NotifyDependents(plan, node, pair, first.graph, second.graph);
    }

    // Compact only after the fixed point; sorted order is preserved.
    std::size_t write = 0;
    for (std::size_t node = 0; node < key.nodes.size(); ++node) {
        auto const first_index = relation_.offsets[node], last_index = relation_.offsets[node + 1];
        relation_.offsets[node] = write;
        for (auto index = first_index; index < last_index; ++index)
            if (states_[index] != CandidateState::kRemoved)
                relation_.pairs[write++] = relation_.pairs[index];
    }
    relation_.offsets.back() = write;
    relation_.pairs.resize(write);
    return finish();
}

PairingRelation ComputePairing(GraphKey const& key, GraphView first, GraphView second) {
    auto const plan = BuildPairingPlan(key);
    auto const first_domain = BuildPairingDomain(plan, first);
    auto const second_domain = BuildPairingDomain(plan, second);
    PairingFilter filter;
    filter.Compute(plan, first_domain, second_domain);
    return std::move(filter).TakeRelation();
}

}  // namespace gk_compact
