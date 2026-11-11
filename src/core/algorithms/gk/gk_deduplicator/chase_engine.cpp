#include "core/algorithms/gk/gk_deduplicator/chase_engine.h"

#include <map>
#include <optional>
#include <utility>

#include "core/algorithms/gk/evaluation/eval_gk.h"
#include "core/algorithms/gk/model/validation.h"
#include "core/algorithms/gk/optimizations/evaluation.h"
#include "core/config/gk/option.h"
#include "core/util/logger.h"

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
#include <chrono>
#endif

namespace gk {
namespace {

struct RootGroup {
    std::vector<model::Node const*> entities;
    std::vector<std::size_t> keys;
};

}  // namespace

ChaseEngine::ChaseEngine(std::vector<model::GraphKey> const& keys) {
    keys_.reserve(keys.size());
    for (auto const& key : keys) keys_.push_back(CompileKey(key));
}

ChaseRunResult ChaseEngine::Run(model::Graph const& graph, bool use_localization,
                                bool use_pairing_filter, bool use_subgraph_reduction) const {
    config::CheckGkOptimizationDependencies(use_localization, use_pairing_filter,
                                            use_subgraph_reduction);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    bool const chase_trace = util::logging::Initialize()->should_log(spdlog::level::trace);
    auto const chase_started = chase_trace ? std::chrono::steady_clock::now()
                                           : std::chrono::steady_clock::time_point{};
#endif
    ChaseRunResult result;
    auto const entities = CollectEntities(graph);
    EquivalenceRelation eq;
    std::map<model::TypeId, RootGroup> groups;
    std::vector<EvalGK> evaluators;
    evaluators.reserve(keys_.size());
    for (std::size_t i = 0; i < keys_.size(); ++i) {
        evaluators.emplace_back(keys_[i]);
        auto const& descriptor = keys_[i].descriptor;
        groups[descriptor.T_Q.at(descriptor.root_x)].keys.push_back(i);
    }
    for (auto const& entity : entities) {
        eq.AddEntity(entity.id_or_val);
        auto const group = groups.find(entity.type);
        if (group != groups.end()) group->second.entities.push_back(&entity);
    }
    bool has_pairs = false;
    for (auto const& [type, group] : groups) has_pairs |= group.entities.size() > 1;
    std::vector<std::optional<Neighborhoods>> neighborhoods(keys_.size());
    if (use_localization) {
        for (std::size_t i = 0; i < keys_.size(); ++i) {
            auto const& descriptor = keys_[i].descriptor;
            auto const& roots = groups.at(descriptor.T_Q.at(descriptor.root_x)).entities;
            if (roots.size() > 1)
                neighborhoods[i].emplace(PrepareNeighborhoods(graph, roots, keys_[i]));
        }
    }
    std::size_t pass_count = 0;
    if (has_pairs) {
        bool changed;
        do {
            changed = false;
            ++pass_count;
            auto const merges_before = result.merge_count;
            for (auto const& [type, group] : groups) {
                for (std::size_t first = 0; first + 1 < group.entities.size(); ++first) {
                    auto const& lhs = *group.entities[first];
                    for (std::size_t second = first + 1; second < group.entities.size(); ++second) {
                        auto const& rhs = *group.entities[second];
                        if (eq.AreEquivalent(lhs.id_or_val, rhs.id_or_val)) continue;
                        for (auto key : group.keys) {
                            auto const left = neighborhoods[key] ? (*neighborhoods[key])[first]
                                                                 : GraphView(graph);
                            auto const right = neighborhoods[key] ? (*neighborhoods[key])[second]
                                                                  : GraphView(graph);
                            if (!EvaluatePair(evaluators[key], keys_[key], {left, right, lhs, rhs},
                                              eq, use_pairing_filter, use_subgraph_reduction))
                                continue;
                            if (eq.Merge(lhs.id_or_val, rhs.id_or_val)) {
                                ++result.merge_count;
                                changed = true;
                                LOG_TRACE("GK Chase merged '{}' and '{}' in pass {}", lhs.id_or_val,
                                          rhs.id_or_val, pass_count);
                            }
                            break;
                        }
                    }
                }
            }
            LOG_DEBUG("GK Chase pass {}: {} new merges, {} classes remain", pass_count,
                      result.merge_count - merges_before, eq.GetClassCount());
        } while (changed);
    }
    result.clusters = eq.GetClasses();
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    if (chase_trace)
        LOG_TRACE("GK phase 'chase': {} ns",
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - chase_started)
                          .count());
#endif
    return result;
}

}  // namespace gk
