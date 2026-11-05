#include "core/algorithms/gk_compact/optimizations/evaluation.h"

#include <cassert>

#include "core/algorithms/gk_compact/optimizations/neighborhood.h"
#include "core/algorithms/gk_compact/optimizations/pairing_filter.h"
#include "core/algorithms/gk_compact/optimizations/subgraph_reduction.h"
#include "core/algorithms/gk_compact/trace.h"

namespace gk_compact {

Neighborhoods PrepareNeighborhoods(Graph const& input, std::span<VertexId const> roots,
                                   CompiledKey const& key) {
    auto const radius = TracePhase("localization_radius", [&] { return GetPatternRadius(key); });
    return TracePhase("localization", [&] { return Neighborhoods(input, roots, radius); });
}

bool EvaluatePair(EvalGK& evaluator, CompiledKey const& key, EvalGKInput const& input,
                  EquivalenceRelation const& eq, bool use_pairing_filter,
                  bool use_subgraph_reduction) {
    assert(!use_subgraph_reduction || use_pairing_filter);
    if (use_pairing_filter) {
        auto const pairing = TracePhase("pairing", [&] {
            return ComputePairing(key, input.first_graph, input.second_graph);
        });
        if (!pairing.Contains(key.root, input.first_root, input.second_root)) return false;
        if (use_subgraph_reduction) {
            auto const first = TracePhase("reduction_first", [&] {
                return ReduceSubgraph(input.first_graph, pairing, PairingProjection::kFirst);
            });
            auto const second = TracePhase("reduction_second", [&] {
                return ReduceSubgraph(input.second_graph, pairing, PairingProjection::kSecond);
            });
            return TracePhase("exact_search", [&] {
                return evaluator.Evaluate({GraphView(input.first_graph.Base(), first),
                                           GraphView(input.second_graph.Base(), second),
                                           input.first_root, input.second_root},
                                          eq);
            });
        }
    }
    return TracePhase("exact_search", [&] { return evaluator.Evaluate(input, eq); });
}

}  // namespace gk_compact
