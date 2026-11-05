#pragma once

#include <cstddef>

#include "core/algorithms/gk_compact/evaluation/eval_gk.h"
#include "core/algorithms/gk_compact/optimizations/neighborhood.h"

namespace gk_compact {

// Build once per execution and retain the packed selections during all pair checks.
// No edge copies and no repeated BFS inside the pair loop.
Neighborhoods PrepareNeighborhoods(Graph const& input, std::span<VertexId const> roots,
                                   CompiledKey const& key);
Neighborhoods PrepareNeighborhoods(Graph&&, std::span<VertexId const>, CompiledKey const&) = delete;
Neighborhoods PrepareNeighborhoods(Graph const&&, std::span<VertexId const>,
                                   CompiledKey const&) = delete;

// Inputs are already full graphs or neighborhoods, chosen by the caller.
// Only pair-specific filtering/reduction happens here. Inputs, key, evaluator
// and Eq share the original graph's numbering; options were checked up front.
bool EvaluatePair(EvalGK& evaluator, CompiledKey const& key, EvalGKInput const& input,
                  EquivalenceRelation const& eq, bool use_pairing_filter = false,
                  bool use_subgraph_reduction = false);

}  // namespace gk_compact
