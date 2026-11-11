#include "core/algorithms/gk_compact/optimizations/evaluation.h"

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

Neighborhoods PrepareNeighborhoods(Graph const& input, std::span<VertexId const> roots,
                                   CompiledKey const& key) {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    bool const localization_trace = util::logging::Initialize()->should_log(spdlog::level::trace);
    auto const localization_started = localization_trace ? std::chrono::steady_clock::now()
                                                         : std::chrono::steady_clock::time_point{};
#endif
    Neighborhoods result(input, roots, GetPatternRadius(key));
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    if (localization_trace)
        LOG_TRACE("Compact GK phase 'localization': {} ns",
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - localization_started)
                          .count());
#endif
    return result;
}

bool EvaluatePair(EvalGK& evaluator, CompiledKey const& key, EvalGKInput const& input,
                  EquivalenceRelation const& eq, bool use_pairing_filter,
                  bool use_subgraph_reduction) {
    assert(!use_subgraph_reduction || use_pairing_filter);
    auto first_graph = input.first_graph;
    auto second_graph = input.second_graph;
    std::optional<std::vector<VertexId>> first_reduced, second_reduced;
    if (use_pairing_filter) {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        bool const pairing_trace = util::logging::Initialize()->should_log(spdlog::level::trace);
        auto const pairing_started = pairing_trace ? std::chrono::steady_clock::now()
                                                   : std::chrono::steady_clock::time_point{};
#endif
        auto const pairing = ComputePairing(key, first_graph, second_graph);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (pairing_trace)
            LOG_TRACE("Compact GK phase 'pairing': {} ns",
                      std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::steady_clock::now() - pairing_started)
                              .count());
#endif
        if (!pairing.Contains(key.root, input.first_root, input.second_root)) {
            LOG_TRACE("Compact GK pairing rejected roots ({}, {})", input.first_root,
                      input.second_root);
            return false;
        }
        if (use_subgraph_reduction) {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
            bool const reduction_trace =
                    util::logging::Initialize()->should_log(spdlog::level::trace);
            auto const reduction_started = reduction_trace
                                                   ? std::chrono::steady_clock::now()
                                                   : std::chrono::steady_clock::time_point{};
#endif
            first_reduced.emplace(ReduceSubgraph(first_graph, pairing, PairingProjection::kFirst));
            second_reduced.emplace(
                    ReduceSubgraph(second_graph, pairing, PairingProjection::kSecond));
            first_graph = GraphView(input.first_graph.Base(), *first_reduced);
            second_graph = GraphView(input.second_graph.Base(), *second_reduced);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
            if (reduction_trace)
                LOG_TRACE("Compact GK phase 'reduction': {} ns",
                          std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::steady_clock::now() - reduction_started)
                                  .count());
#endif
        }
    }
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    bool const search_trace = util::logging::Initialize()->should_log(spdlog::level::trace);
    auto const search_started = search_trace ? std::chrono::steady_clock::now()
                                             : std::chrono::steady_clock::time_point{};
#endif
    bool const result = evaluator.Evaluate(
            {first_graph, second_graph, input.first_root, input.second_root}, eq);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    if (search_trace)
        LOG_TRACE("Compact GK phase 'exact_search': {} ns",
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - search_started)
                          .count());
#endif
    return result;
}

}  // namespace gk_compact
