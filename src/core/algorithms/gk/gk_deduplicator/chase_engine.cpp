#include "core/algorithms/gk/gk_deduplicator/chase_engine.h"

#include <chrono>
#include <map>
#include <utility>

#include "core/algorithms/gk/validation.h"
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
    std::vector<EvalGK const*> evaluators;
};

}  // namespace

ChaseEngine::ChaseEngine(std::vector<model::GraphKey> const& keys) {
    rules_.reserve(keys.size());
    for (auto const& key : keys) {
        CheckKey(key);
        rules_.push_back({key.T_Q.at(key.root_x), key});
    }
    LOG_DEBUG("Prepared ChaseEngine with {} graph keys", rules_.size());
}

ChaseResult ChaseEngine::Run(model::Graph const& graph, EvaluationOptions options) const {
    auto const started_at = std::chrono::steady_clock::now();
    ChaseResult result;
    auto& stats = result.statistics;
    std::vector<model::Node> const entities = CollectEntities(graph);
    stats.entity_count = entities.size();

    EquivalenceRelation eq;
    std::map<model::TypeId, RootGroup> groups;
    std::vector<EvalGK> evaluators;
    evaluators.reserve(rules_.size());
    for (auto const& rule : rules_) {
        evaluators.emplace_back(rule.key, options);
        groups[rule.root_type].evaluators.push_back(&evaluators.back());
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
                        for (auto const* evaluator : group.evaluators) {
                            ++stats.eval_call_count;
                            if (!evaluator->Evaluate({graph, graph, lhs, rhs}, eq,
                                                     &stats.evaluation))
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
