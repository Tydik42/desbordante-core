#include "core/algorithms/gk/optimizations/evaluation.h"

#include <cassert>
#include <chrono>
#include <optional>

#include "core/util/logger.h"

namespace gk {
namespace {

std::uint64_t Elapsed(std::chrono::steady_clock::time_point started) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                              std::chrono::steady_clock::now() - started)
                                              .count());
}

}  // namespace

Neighborhoods PrepareNeighborhoods(model::Graph const& graph,
                                   std::span<model::Node const* const> roots,
                                   model::GraphKey const& key, EvaluationStatistics& statistics) {
    auto const started = std::chrono::steady_clock::now();
    Neighborhoods result(graph, roots, GetPatternRadius(key));
    statistics.neighborhood_ns += Elapsed(started);
    for (std::size_t i = 0; i < result.size(); ++i)
        statistics.neighborhood_triple_count += result[i].size();
    LOG_TRACE("GK localization: {} roots, {} stored triples, {} ns", roots.size(),
              statistics.neighborhood_triple_count, statistics.neighborhood_ns);
    return result;
}

bool EvaluatePair(EvalGK& evaluator, EvalGKInput const& input, EquivalenceRelation const& eq,
                  PairingFilter const* filter, bool reduce, EvaluationStatistics* statistics) {
    assert(!reduce || filter);
    EvaluationStatistics local;
    auto& stats = statistics ? *statistics : local;
    std::optional<PairingResult> prepared;
    auto const* first = &input.first_graph;
    auto const* second = &input.second_graph;
    if (filter) {
        prepared = filter->Prepare(*first, *second, input.first_root, input.second_root, reduce,
                                   stats);
        if (!prepared->pairable) return false;
        if (reduce) {
            first = &prepared->first_graph;
            second = &prepared->second_graph;
        }
    }
    auto const started = std::chrono::steady_clock::now();
    ++stats.search_call_count;
    bool const result =
            evaluator.Evaluate({*first, *second, input.first_root, input.second_root}, eq);
    auto const elapsed = Elapsed(started);
    stats.search_ns += elapsed;
    LOG_TRACE("GK exact search: roots ('{}', '{}'), result={}, {} ns", input.first_root.id_or_val,
              input.second_root.id_or_val, result, elapsed);
    return result;
}

}  // namespace gk
