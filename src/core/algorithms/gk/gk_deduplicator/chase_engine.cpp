#include "core/algorithms/gk/gk_deduplicator/chase_engine.h"

#include <chrono>
#include <map>
#include <optional>
#include <utility>

#include "core/algorithms/gk/optimizations/evaluation.h"
#include "core/algorithms/gk/validation.h"
#include "core/config/gk/option.h"
#include "core/util/logger.h"

namespace gk {
namespace {

std::uint64_t ElapsedNanoseconds(std::chrono::steady_clock::time_point started_at) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                              std::chrono::steady_clock::now() - started_at)
                                              .count());
}

struct RootGroup {
    std::vector<model::Node const*> entities;
    std::vector<std::size_t> rules;
};

}  // namespace

ChaseEngine::ChaseEngine(std::vector<model::GraphKey> const& keys) {
    rules_.reserve(keys.size());
    for (auto const& key : keys) {
        auto compiled = CompileKey(key);
        rules_.push_back({key.T_Q.at(key.root_x), std::move(compiled)});
    }
    LOG_DEBUG("Prepared ChaseEngine with {} graph keys", rules_.size());
}

ChaseResult ChaseEngine::Run(model::Graph const& graph, bool use_localization,
                             bool use_pairing_filter, bool use_subgraph_reduction) const {
    config::CheckGkOptimizationDependencies(use_localization, use_pairing_filter,
                                            use_subgraph_reduction);
    auto const started_at = std::chrono::steady_clock::now();
    ChaseResult result;
    auto& stats = result.statistics;
    std::vector<model::Node> const entities = CollectEntities(graph);
    stats.entity_count = entities.size();

    EquivalenceRelation eq;
    std::map<model::TypeId, RootGroup> groups;
    std::vector<EvalGK> evaluators;
    evaluators.reserve(rules_.size());
    for (std::size_t i = 0; i < rules_.size(); ++i) {
        evaluators.emplace_back(rules_[i].key);
        groups[rules_[i].root_type].rules.push_back(i);
    }
    for (auto const& entity : entities) {
        eq.AddEntity(entity.id_or_val);
        auto const group = groups.find(entity.type);
        if (group != groups.end()) group->second.entities.push_back(&entity);
    }
    for (auto const& [type, group] : groups) {
        std::size_t const count = group.entities.size();
        if (count > 1) stats.root_pair_count += count * (count - 1) / 2;
    }
    std::vector<std::optional<Neighborhoods>> neighborhoods(rules_.size());
    std::vector<std::optional<PairingFilter>> filters(rules_.size());
    for (std::size_t i = 0; i < rules_.size(); ++i) {
        auto const& rule = rules_[i];
        auto const& roots = groups.at(rule.root_type).entities;
        if (use_localization && roots.size() > 1)
            neighborhoods[i].emplace(
                    PrepareNeighborhoods(graph, roots, rule.key.descriptor, stats.evaluation));
        if (use_pairing_filter) filters[i].emplace(rule.key.descriptor);
    }
    stats.prepare_ns = ElapsedNanoseconds(started_at);

    auto const eval_started_at = std::chrono::steady_clock::now();
    if (stats.root_pair_count != 0) {
        bool changed;
        do {
            changed = false;
            ++stats.pass_count;
            std::size_t const merges_before = stats.merge_count;
            for (auto const& [type, group] : groups) {
                for (std::size_t first = 0; first < group.entities.size(); ++first) {
                    auto const& lhs = *group.entities[first];
                    for (std::size_t second = first + 1; second < group.entities.size(); ++second) {
                        auto const& rhs = *group.entities[second];
                        if (eq.AreEquivalent(lhs.id_or_val, rhs.id_or_val)) continue;
                        ++stats.checked_pair_count;
                        for (auto rule : group.rules) {
                            ++stats.eval_call_count;
                            auto const& left =
                                    neighborhoods[rule] ? (*neighborhoods[rule])[first] : graph;
                            auto const& right =
                                    neighborhoods[rule] ? (*neighborhoods[rule])[second] : graph;
                            if (!EvaluatePair(evaluators[rule], {left, right, lhs, rhs}, eq,
                                              filters[rule] ? &*filters[rule] : nullptr,
                                              use_subgraph_reduction, &stats.evaluation))
                                continue;
                            if (eq.Merge(lhs.id_or_val, rhs.id_or_val)) {
                                ++stats.merge_count;
                                changed = true;
                                LOG_TRACE("Chase merged entities '{}' and '{}' in pass {}",
                                          lhs.id_or_val, rhs.id_or_val, stats.pass_count);
                            }
                            break;
                        }
                    }
                }
            }
            LOG_DEBUG("Chase pass {}: {} new merges, {} classes remain", stats.pass_count,
                      stats.merge_count - merges_before, eq.GetClassCount());
        } while (changed);
    }
    stats.eval_ns = ElapsedNanoseconds(eval_started_at);

    auto const finalize_started_at = std::chrono::steady_clock::now();
    result.clusters = eq.GetClasses();
    stats.finalize_ns = ElapsedNanoseconds(finalize_started_at);
    stats.total_ns = ElapsedNanoseconds(started_at);
    LOG_DEBUG("Chase finished: {} entities, {} classes, {} merges, {} passes, elapsed={} ns",
              stats.entity_count, result.clusters.size(), stats.merge_count, stats.pass_count,
              stats.total_ns);
    return result;
}

}  // namespace gk
