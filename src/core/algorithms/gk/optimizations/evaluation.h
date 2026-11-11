#pragma once

#include "core/algorithms/gk/evaluation/eval_gk.h"
#include "core/algorithms/gk/optimizations/neighborhood.h"

namespace gk {

Neighborhoods PrepareNeighborhoods(model::Graph const& input,
                                   std::span<model::Node const* const> roots, GraphKey const& key);
Neighborhoods PrepareNeighborhoods(model::Graph&&, std::span<model::Node const* const>,
                                   GraphKey const&) = delete;
Neighborhoods PrepareNeighborhoods(model::Graph const&&, std::span<model::Node const* const>,
                                   GraphKey const&) = delete;

bool EvaluatePair(EvalGK& evaluator, GraphKey const& key, EvalGKInput const& input,
                  EquivalenceRelation const& eq, bool use_pairing_filter = false,
                  bool use_subgraph_reduction = false);

}  // namespace gk
