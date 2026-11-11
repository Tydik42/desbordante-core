#include "core/algorithms/gk_compact/optimizations/pairing_filter.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace gk_compact {
namespace {

using gk::model::VarKind;

enum class CandidateState : std::uint8_t { kRemoved, kLive, kPendingRemoval };

PairingRelation EmptyRelation(std::size_t nodes) {
    return {std::vector<Offset>(nodes + 1, 0), {}};
}

PairingRelation InitialRelation(GraphKey const& key, GraphView first, GraphView second) {
    std::vector<TypeId> types;
    for (auto const& node : key.nodes)
        if (node.kind == VarKind::kEntity || node.kind == VarKind::kWildcard)
            types.push_back(node.type);
    std::ranges::sort(types);
    types.erase(std::unique(types.begin(), types.end()), types.end());
    auto type_index = [&](TypeId type) {
        return static_cast<std::size_t>(std::ranges::lower_bound(types, type) - types.begin());
    };
    auto collect_entities = [&](GraphView graph) {
        std::vector<std::vector<VertexId>> groups(types.size());
        for (auto vertex : graph.EntityIds()) {
            auto const type = graph.GetType(vertex);
            auto const found = std::ranges::lower_bound(types, type);
            if (found != types.end() && *found == type)
                groups[static_cast<std::size_t>(found - types.begin())].push_back(vertex);
        }
        return groups;
    };
    auto const first_entities = collect_entities(first);
    auto const second_entities = collect_entities(second);
    std::vector<VertexId> values;
    if (std::ranges::any_of(key.nodes,
                            [](auto const& node) { return node.kind == VarKind::kValue; })) {
        auto first_values = first.VertexIds() | std::views::drop(first.GetEntityCount());
        auto second_values = second.VertexIds() | std::views::drop(second.GetEntityCount());
        std::ranges::set_intersection(first_values, second_values, std::back_inserter(values));
    }

    // A connected pattern with any empty domain has an empty greatest relation.
    // Check this before reserving a potentially large Cartesian product elsewhere.
    for (auto const& node : key.nodes) {
        switch (node.kind) {
            case VarKind::kEntity:
            case VarKind::kWildcard: {
                auto const index = type_index(node.type);
                if (first_entities[index].empty() || second_entities[index].empty())
                    return EmptyRelation(key.nodes.size());
                break;
            }
            case VarKind::kValue:
                if (values.empty()) return EmptyRelation(key.nodes.size());
                break;
            case VarKind::kConstant:
                if (!first.ContainsVertex(*node.constant) || !second.ContainsVertex(*node.constant))
                    return EmptyRelation(key.nodes.size());
                break;
        }
    }

    PairingRelation result;
    result.offsets.reserve(key.nodes.size() + 1);
    for (auto const& node : key.nodes) {
        std::size_t count = 1;
        auto const remaining = result.pairs.max_size() - result.offsets.back();
        switch (node.kind) {
            case VarKind::kEntity:
            case VarKind::kWildcard: {
                auto const index = type_index(node.type);
                auto const left = first_entities[index].size();
                auto const right = second_entities[index].size();
                if (right > remaining / left)
                    throw std::length_error("Compact GK pairing exceeds its candidate capacity");
                count = left * right;
                break;
            }
            case VarKind::kValue:
                count = values.size();
                break;
            case VarKind::kConstant:
                break;
        }
        if (count > remaining)
            throw std::length_error("Compact GK pairing exceeds its candidate capacity");
        result.offsets.push_back(result.offsets.back() + count);
    }
    result.pairs.reserve(result.offsets.back());
    for (auto const& node : key.nodes) {
        switch (node.kind) {
            case VarKind::kEntity:
            case VarKind::kWildcard: {
                auto const index = type_index(node.type);
                for (auto left : first_entities[index])
                    for (auto right : second_entities[index])
                        result.pairs.emplace_back(left, right);
                break;
            }
            case VarKind::kValue:
                for (auto value : values) result.pairs.emplace_back(value, value);
                break;
            case VarKind::kConstant:
                result.pairs.emplace_back(*node.constant, *node.constant);
                break;
        }
    }
    return result;
}

bool ContainsLive(PairingRelation const& relation, std::span<CandidateState const> states,
                  PatternNodeId node, VertexPair pair) {
    auto const candidates = relation.Candidates(node);
    auto const found = std::ranges::lower_bound(candidates, pair);
    if (found == candidates.end() || *found != pair) return false;
    auto const index =
            relation.offsets[node] + static_cast<std::size_t>(found - candidates.begin());
    // Pending deletions remain visible until the end of the current pass.
    return states[index] != CandidateState::kRemoved;
}

bool HasSupport(VertexPair pair, PredicateId predicate, PatternNodeId neighbor, bool outgoing,
                GraphView first, GraphView second, PairingRelation const& relation,
                std::span<CandidateState const> states) {
    auto first_edges = outgoing ? first.Outgoing(pair.first, predicate)
                                : first.Incoming(pair.first, predicate);
    auto second_edges = outgoing ? second.Outgoing(pair.second, predicate)
                                 : second.Incoming(pair.second, predicate);
    for (auto const& left : first_edges)
        for (auto const& right : second_edges)
            if (ContainsLive(relation, states, neighbor, {left.neighbor, right.neighbor}))
                return true;
    return false;
}

}  // namespace

PairingRelation ComputePairing(GraphKey const& key, GraphView first, GraphView second) {
    auto result = InitialRelation(key, first, second);
    if (result.pairs.empty()) return result;
    std::vector<std::vector<std::size_t>> incident(key.nodes.size());
    for (std::size_t i = 0; i < key.triples.size(); ++i) {
        auto const& triple = key.triples[i];
        incident[triple.subject].push_back(i);
        if (triple.object != triple.subject) incident[triple.object].push_back(i);
    }
    // Never erase candidates while checking supports, including self-references.
    // One byte per initial pair; pending removals are committed after the pass.
    std::vector<CandidateState> states(result.pairs.size(), CandidateState::kLive);
    bool changed;
    do {
        changed = false;
        for (std::size_t node = 0; node < key.nodes.size(); ++node) {
            for (auto index = result.offsets[node]; index < result.offsets[node + 1]; ++index) {
                if (states[index] == CandidateState::kRemoved) continue;
                auto const pair = result.pairs[index];
                for (auto edge : incident[node]) {
                    auto const& triple = key.triples[edge];
                    bool const forward = triple.subject != node ||
                                         HasSupport(pair, triple.predicate, triple.object, true,
                                                    first, second, result, states);
                    bool const backward = triple.object != node ||
                                          HasSupport(pair, triple.predicate, triple.subject, false,
                                                     first, second, result, states);
                    if (forward && backward) continue;
                    states[index] = CandidateState::kPendingRemoval;
                    changed = true;
                    break;
                }
            }
        }
        for (std::size_t node = 0; node < key.nodes.size(); ++node) {
            bool any = false;
            for (auto index = result.offsets[node]; index < result.offsets[node + 1]; ++index) {
                if (states[index] == CandidateState::kPendingRemoval)
                    states[index] = CandidateState::kRemoved;
                any |= states[index] != CandidateState::kRemoved;
            }
            if (!any) return EmptyRelation(key.nodes.size());
        }
    } while (changed);

    // Compact in place only after the fixed point. Sorted order is preserved.
    std::size_t write = 0;
    for (std::size_t node = 0; node < key.nodes.size(); ++node) {
        auto const first_index = result.offsets[node], last_index = result.offsets[node + 1];
        result.offsets[node] = write;
        for (auto index = first_index; index < last_index; ++index)
            if (states[index] != CandidateState::kRemoved)
                result.pairs[write++] = result.pairs[index];
    }
    result.offsets.back() = write;
    result.pairs.resize(write);
    return result;
}

}  // namespace gk_compact
