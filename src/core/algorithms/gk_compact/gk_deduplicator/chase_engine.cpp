#include "core/algorithms/gk_compact/gk_deduplicator/chase_engine.h"

#include <algorithm>
#include <cstdint>
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

// One packed selection set per root type and exact radius. positions maps the
// group's root indices to this cache's union of key-specific eligible roots.
// std::map keeps these owners stable while views borrow their storage.
struct NeighborhoodGroup {
    std::vector<std::size_t> positions;
    std::optional<Neighborhoods> neighborhoods;
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
    EquivalenceRelation eq(graph.Data().metadata);
    std::map<TypeId, RootGroup> groups;
    std::vector<EvalGK> evaluators;
    evaluators.reserve(keys_.size());
    std::vector<std::vector<std::uint8_t>> eligible(keys_.size());
    {
        std::vector<std::vector<VertexId>> roots(keys_.size());
        for (std::size_t index = 0; index < keys_.size(); ++index) {
            evaluators.emplace_back(keys_[index]);
            roots[index] = SelectRoots(graph, keys_[index]);
            if (roots[index].size() < 2) continue;
            auto& group = groups[keys_[index].nodes[keys_[index].root].type];
            group.keys.push_back(index);
            group.entities.insert(group.entities.end(), roots[index].begin(), roots[index].end());
        }
        for (auto& [type, group] : groups) {
            std::ranges::sort(group.entities);
            group.entities.erase(std::unique(group.entities.begin(), group.entities.end()),
                                 group.entities.end());
            for (auto key : group.keys) {
                eligible[key].resize(group.entities.size(), 0);
                auto root = roots[key].begin();
                for (std::size_t index = 0;
                     index < group.entities.size() && root != roots[key].end(); ++index) {
                    if (group.entities[index] != *root) continue;
                    eligible[key][index] = 1;
                    ++root;
                }
            }
        }
    }
    std::map<std::pair<TypeId, std::size_t>, NeighborhoodGroup> neighborhood_groups;
    std::vector<NeighborhoodGroup const*> neighborhoods(keys_.size(), nullptr);
    if (use_localization) {
        for (auto const& [type, group] : groups) {
            for (auto key : group.keys) {
                if (!use_pairing_filter && keys_[key].SearchStaysInNeighborhood()) {
                    LOG_TRACE(
                            "Compact GK localization: key={} exact search already bounded, "
                            "radius={}, expansion_depth={}",
                            key, keys_[key].radius, keys_[key].expansion_depth);
                    continue;
                }
                auto& cached = neighborhood_groups[{type, GetPatternRadius(keys_[key])}];
                if (cached.positions.empty())
                    cached.positions.resize(group.entities.size(), group.entities.size());
                for (std::size_t index = 0; index < group.entities.size(); ++index)
                    if (eligible[key][index]) cached.positions[index] = 0;
                neighborhoods[key] = &cached;
            }
        }
        for (auto& [spec, cached] : neighborhood_groups) {
            auto const& group = groups.at(spec.first);
            std::vector<VertexId> roots;
            for (std::size_t index = 0; index < group.entities.size(); ++index) {
                if (cached.positions[index] == group.entities.size()) continue;
                cached.positions[index] = roots.size();
                roots.push_back(group.entities[index]);
            }
            cached.neighborhoods.emplace(PrepareNeighborhoods(graph, roots, spec.second));
        }
    }
    std::vector<std::optional<PairingPlan>> pairing_plans;
    std::vector<std::vector<PairingDomain>> pairing_domains;
    std::optional<PairingFilter> pairing_filter;
    std::optional<SubgraphReduction> reduction;
    if (use_subgraph_reduction) reduction.emplace();
    if (use_pairing_filter) {
        pairing_plans.resize(keys_.size());
        pairing_domains.resize(keys_.size());
        pairing_filter.emplace();
        for (std::size_t index = 0; index < keys_.size(); ++index) {
            if (!neighborhoods[index]) continue;
            pairing_plans[index].emplace(BuildPairingPlan(keys_[index]));
            pairing_domains[index] = BuildPairingDomains(*pairing_plans[index],
                                                         *neighborhoods[index]->neighborhoods);
        }
    }
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    if (chase_trace)
        LOG_TRACE(
                "Compact GK phase 'chase_preparation': {} ns, keys={}, root_types={}, "
                "neighborhood_groups={}",
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - chase_started)
                        .count(),
                keys_.size(), groups.size(), neighborhood_groups.size());
#endif
    for (std::size_t pass_count = 1;; ++pass_count) {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        auto const pass_started = chase_trace ? std::chrono::steady_clock::now()
                                              : std::chrono::steady_clock::time_point{};
#endif
        auto const merges_before = result.merge_count;
        for (auto const& [type, group] : groups) {
            for (std::size_t first = 0; first + 1 < group.entities.size(); ++first) {
                auto const lhs = group.entities[first];
                for (std::size_t second = first + 1; second < group.entities.size(); ++second) {
                    auto const rhs = group.entities[second];
                    if (eq.AreEquivalent(lhs, rhs)) continue;
                    for (auto key : group.keys) {
                        if (!eligible[key][first] || !eligible[key][second]) continue;
                        auto const* cached = neighborhoods[key];
                        auto const left_index = cached ? cached->positions[first] : first;
                        auto const right_index = cached ? cached->positions[second] : second;
                        bool const restrict_search =
                                cached && !keys_[key].SearchStaysInNeighborhood();
                        auto const left = restrict_search ? (*cached->neighborhoods)[left_index]
                                                          : GraphView(graph);
                        auto const right = restrict_search ? (*cached->neighborhoods)[right_index]
                                                           : GraphView(graph);
                        EvalGKInput const input{left, right, lhs, rhs};
                        bool const matches =
                                pairing_filter
                                        ? EvaluatePair(evaluators[key], input, eq,
                                                       *pairing_plans[key], *pairing_filter,
                                                       pairing_domains[key][left_index],
                                                       pairing_domains[key][right_index],
                                                       reduction ? &*reduction : nullptr)
                                        : EvaluatePair(evaluators[key], keys_[key], input, eq);
                        if (!matches) continue;
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
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (chase_trace)
            LOG_TRACE("Compact GK phase 'chase_pass': {} ns, pass={}, merges={}",
                      std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::steady_clock::now() - pass_started)
                              .count(),
                      pass_count, result.merge_count - merges_before);
#endif
        if (result.merge_count == merges_before) break;
    }

    // Metadata can contain inactive entities. Only active original IDs belong
    // to this result; every successful merge reduces their class count by one.
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    auto const clustering_started = chase_trace ? std::chrono::steady_clock::now()
                                                : std::chrono::steady_clock::time_point{};
#endif
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
    if (chase_trace) {
        LOG_TRACE("Compact GK phase 'result_clustering': {} ns, classes={}",
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - clustering_started)
                          .count(),
                  result.clusters.size());
        LOG_TRACE("Compact GK phase 'chase': {} ns",
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - chase_started)
                          .count());
    }
#endif
    return result;
}

}  // namespace gk_compact
