#include "core/algorithms/gk_compact/gk_deduplicator/chase_engine.h"

#include <map>
#include <optional>
#include <unordered_map>

#include "core/algorithms/gk_compact/model/equivalence_relation.h"
#include "core/algorithms/gk_compact/optimizations/evaluation.h"
#include "core/config/gk/option.h"
#include "core/util/logger.h"

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
#include <chrono>
#endif

namespace gk_compact {
namespace {

struct RootGroup {
    std::vector<VertexId> entities;
    std::vector<std::size_t> keys;
};

}  // namespace

ChaseRunResult ChaseEngine::Run(Graph const& graph, bool use_localization, bool use_pairing_filter,
                                bool use_subgraph_reduction) const {
    config::CheckGkOptimizationDependencies(use_localization, use_pairing_filter,
                                            use_subgraph_reduction);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    bool const chase_trace = util::logging::Initialize()->should_log(spdlog::level::trace);
    auto const chase_started = chase_trace ? std::chrono::steady_clock::now()
                                           : std::chrono::steady_clock::time_point{};
#endif
    ChaseRunResult result;
    EquivalenceRelation eq(graph.Metadata().GetEntityCount());
    std::map<TypeId, RootGroup> groups;
    std::vector<EvalGK> evaluators;
    evaluators.reserve(keys_.size());
    for (std::size_t i = 0; i < keys_.size(); ++i) {
        evaluators.emplace_back(keys_[i]);
        auto const& key = keys_[i];
        groups[key.nodes[key.root].type].keys.push_back(i);
    }
    for (auto entity : graph.EntityIds()) {
        auto const group = groups.find(graph.GetType(entity));
        if (group != groups.end()) group->second.entities.push_back(entity);
    }
    std::vector<std::optional<Neighborhoods>> neighborhoods(keys_.size());
    if (use_localization) {
        for (std::size_t i = 0; i < keys_.size(); ++i) {
            auto const& key = keys_[i];
            auto const& roots = groups.at(key.nodes[key.root].type).entities;
            if (roots.size() > 1) neighborhoods[i].emplace(PrepareNeighborhoods(graph, roots, key));
        }
    }
    for (std::size_t pass_count = 1;; ++pass_count) {
        auto const merges_before = result.merge_count;
        for (auto const& [type, group] : groups) {
            for (std::size_t first = 0; first + 1 < group.entities.size(); ++first) {
                auto const lhs = group.entities[first];
                for (std::size_t second = first + 1; second < group.entities.size(); ++second) {
                    auto const rhs = group.entities[second];
                    if (eq.AreEquivalent(lhs, rhs)) continue;
                    for (auto key : group.keys) {
                        auto const left = neighborhoods[key] ? (*neighborhoods[key])[first]
                                                             : GraphView(graph);
                        auto const right = neighborhoods[key] ? (*neighborhoods[key])[second]
                                                              : GraphView(graph);
                        if (!EvaluatePair(evaluators[key], keys_[key], {left, right, lhs, rhs}, eq,
                                          use_pairing_filter, use_subgraph_reduction))
                            continue;
                        if (eq.Merge(lhs, rhs)) {
                            ++result.merge_count;
                            LOG_TRACE("Compact GK Chase merged ({}, {}) in pass {}", lhs, rhs,
                                      pass_count);
                        }
                        break;
                    }
                }
            }
        }
        LOG_DEBUG("Compact GK Chase pass {}: {} new merges, {} active classes remain", pass_count,
                  result.merge_count - merges_before, graph.GetEntityCount() - result.merge_count);
        if (result.merge_count == merges_before) break;
    }

    // Metadata can contain inactive entities. Only active original IDs belong
    // to this result; every successful merge reduces their class count by one.
    auto const class_count = graph.GetEntityCount() - result.merge_count;
    result.clusters.reserve(class_count);
    if (eq.IsIdentity()) {
        for (auto entity : graph.EntityIds()) result.clusters.push_back({entity});
    } else {
        std::unordered_map<VertexId, std::size_t> indices;
        indices.reserve(class_count);
        for (auto entity : graph.EntityIds()) {
            auto const representative = eq.GetRepresentative(entity);
            auto const [it, inserted] = indices.emplace(representative, result.clusters.size());
            if (inserted) result.clusters.emplace_back();
            result.clusters[it->second].push_back(entity);
        }
    }
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    if (chase_trace)
        LOG_TRACE("Compact GK phase 'chase': {} ns",
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - chase_started)
                          .count());
#endif
    return result;
}

}  // namespace gk_compact
