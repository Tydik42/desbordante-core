#pragma once

#include "core/algorithms/gk/evaluation/eval_gk.h"
#include "core/algorithms/gk/evaluation_statistics.h"
#include "core/algorithms/gk/optimizations/neighborhood.h"
#include "core/algorithms/gk/optimizations/pairing_filter.h"

namespace gk {

Neighborhoods PrepareNeighborhoods(model::Graph const& graph,
                                   std::span<model::Node const* const> roots,
                                   model::GraphKey const& key, EvaluationStatistics& statistics);

// The caller selects full graphs or cached neighborhoods. A non-null filter
// enables pairing; reduction requires it. All three flags belong to config.
bool EvaluatePair(EvalGK& evaluator, EvalGKInput const& input, EquivalenceRelation const& eq,
                  PairingFilter const* filter = nullptr, bool reduce = false,
                  EvaluationStatistics* statistics = nullptr);

}  // namespace gk
