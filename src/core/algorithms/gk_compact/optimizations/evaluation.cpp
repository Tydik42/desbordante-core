#include "core/algorithms/gk_compact/optimizations/evaluation.h"

#include <algorithm>
#include <cassert>
#include <optional>
#include <vector>

#include "core/algorithms/gk_compact/optimizations/pairing_filter.h"
#include "core/algorithms/gk_compact/optimizations/subgraph_reduction.h"
#include "core/util/logger.h"

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
#include <chrono>
#endif

namespace gk_compact {
namespace {

bool HasRootSupport(Graph const& graph, GraphKey const& key, VertexId root,
                    PatternTriple const& triple) {
    if (triple.subject == key.root && triple.object == key.root)
        return graph.HasEdge(root, triple.predicate, root);
    bool const outgoing = triple.subject == key.root;
    auto const node = outgoing ? triple.object : triple.subject;
    auto const& spec = key.nodes[node];
    using gk::model::VarKind;
    if (spec.kind == VarKind::kConstant)
        return outgoing ? graph.HasEdge(root, triple.predicate, *spec.constant)
                        : graph.HasEdge(*spec.constant, triple.predicate, root);
    auto const edges = outgoing ? graph.Outgoing(root, triple.predicate)
                                : graph.Incoming(root, triple.predicate);
    return std::ranges::any_of(edges, [&](Edge const& edge) {
        if (spec.kind == VarKind::kValue) return !graph.IsEntity(edge.neighbor);
        return edge.neighbor != root && graph.GetType(edge.neighbor) == spec.type;
    });
}

bool EvaluateExact(EvalGK& evaluator, EvalGKInput const& input, EquivalenceRelation const& eq) {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    bool const search_trace = util::logging::Initialize()->should_log(spdlog::level::trace);
    auto const search_started = search_trace ? std::chrono::steady_clock::now()
                                             : std::chrono::steady_clock::time_point{};
#endif
    bool const result = evaluator.Evaluate(input, eq);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    if (search_trace)
        LOG_TRACE("Compact GK phase 'exact_search': {} ns",
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - search_started)
                          .count());
#endif
    return result;
}

}  // namespace

std::vector<VertexId> SelectRoots(Graph const& graph, GraphKey const& key) {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    bool const roots_trace = util::logging::Initialize()->should_log(spdlog::level::trace);
    auto const roots_started = roots_trace ? std::chrono::steady_clock::now()
                                           : std::chrono::steady_clock::time_point{};
#endif
    std::vector<std::size_t> incident;
    for (std::size_t index = 0; index < key.triples.size(); ++index) {
        auto const& triple = key.triples[index];
        if (triple.subject == key.root || triple.object == key.root) incident.push_back(index);
    }
    std::vector<VertexId> roots;
    for (auto entity : graph.EntityIds()) {
        if (graph.GetType(entity) != key.nodes[key.root].type) continue;
        if (std::ranges::all_of(incident, [&](std::size_t index) {
                return HasRootSupport(graph, key, entity, key.triples[index]);
            }))
            roots.push_back(entity);
    }
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    if (roots_trace)
        LOG_TRACE("Compact GK phase 'root_selection': {} ns, {} candidates",
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - roots_started)
                          .count(),
                  roots.size());
#endif
    return roots;
}

Neighborhoods PrepareNeighborhoods(Graph const& input, std::span<VertexId const> roots,
                                   GraphKey const& key) {
    return PrepareNeighborhoods(input, roots, GetPatternRadius(key));
}

Neighborhoods PrepareNeighborhoods(Graph const& input, std::span<VertexId const> roots,
                                   std::size_t radius) {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    bool const localization_trace = util::logging::Initialize()->should_log(spdlog::level::trace);
    auto const localization_started = localization_trace ? std::chrono::steady_clock::now()
                                                         : std::chrono::steady_clock::time_point{};
#endif
    Neighborhoods result(input, roots, radius);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    if (localization_trace)
        LOG_TRACE("Compact GK phase 'localization': {} ns",
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - localization_started)
                          .count());
#endif
    return result;
}

std::vector<PairingDomain> BuildPairingDomains(PairingPlan const& plan,
                                               Neighborhoods const& neighborhoods) {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    bool const domains_trace = util::logging::Initialize()->should_log(spdlog::level::trace);
    auto const domains_started = domains_trace ? std::chrono::steady_clock::now()
                                               : std::chrono::steady_clock::time_point{};
#endif
    std::vector<PairingDomain> domains;
    domains.reserve(neighborhoods.size());
    for (std::size_t index = 0; index < neighborhoods.size(); ++index)
        domains.push_back(BuildPairingDomain(plan, neighborhoods[index]));
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    if (domains_trace)
        LOG_TRACE("Compact GK phase 'pairing_domains': {} ns",
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - domains_started)
                          .count());
#endif
    return domains;
}

bool EvaluatePair(EvalGK& evaluator, EvalGKInput const& input, EquivalenceRelation const& eq,
                  PairingPlan const& plan, PairingFilter& filter, PairingDomain const& first,
                  PairingDomain const& second, SubgraphReduction* reduction) {
    auto first_graph = input.first_graph;
    auto second_graph = input.second_graph;
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    bool const pairing_trace = util::logging::Initialize()->should_log(spdlog::level::trace);
    auto const pairing_started = pairing_trace ? std::chrono::steady_clock::now()
                                               : std::chrono::steady_clock::time_point{};
#endif
    auto const& pairing = filter.Compute(plan, first, second);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    if (pairing_trace)
        LOG_TRACE("Compact GK phase 'pairing': {} ns",
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - pairing_started)
                          .count());
#endif
    if (!pairing.Contains(plan.key.root, input.first_root, input.second_root)) {
        LOG_TRACE("Compact GK pairing rejected roots ({}, {})", input.first_root,
                  input.second_root);
        return false;
    }
    if (reduction) {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        bool const reduction_trace = util::logging::Initialize()->should_log(spdlog::level::trace);
        auto const reduction_started = reduction_trace ? std::chrono::steady_clock::now()
                                                       : std::chrono::steady_clock::time_point{};
#endif
        reduction->Compute(first_graph, second_graph, pairing);
        first_graph = GraphView(input.first_graph.Base(), reduction->First());
        second_graph = GraphView(input.second_graph.Base(), reduction->Second());
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (reduction_trace)
            LOG_TRACE("Compact GK phase 'reduction': {} ns",
                      std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::steady_clock::now() - reduction_started)
                              .count());
#endif
    }
    return EvaluateExact(evaluator,
                         {first_graph, second_graph, input.first_root, input.second_root}, eq);
}

bool EvaluatePair(EvalGK& evaluator, GraphKey const& key, EvalGKInput const& input,
                  EquivalenceRelation const& eq, bool use_pairing_filter,
                  bool use_subgraph_reduction) {
    assert(!use_subgraph_reduction || use_pairing_filter);
    if (!use_pairing_filter) return EvaluateExact(evaluator, input, eq);
    auto const plan = BuildPairingPlan(key);
    auto const first = BuildPairingDomain(plan, input.first_graph);
    auto const second = BuildPairingDomain(plan, input.second_graph);
    PairingFilter filter;
    std::optional<SubgraphReduction> reduction;
    if (use_subgraph_reduction) reduction.emplace();
    return EvaluatePair(evaluator, input, eq, plan, filter, first, second,
                        reduction ? &*reduction : nullptr);
}

}  // namespace gk_compact
