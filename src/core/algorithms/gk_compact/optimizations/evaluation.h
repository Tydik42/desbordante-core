#pragma once

#include <cstddef>

#include "core/algorithms/gk_compact/evaluation/eval_gk.h"
#include "core/algorithms/gk_compact/optimizations/neighborhood.h"
#include "core/algorithms/gk_compact/optimizations/pairing_filter.h"
#include "core/algorithms/gk_compact/optimizations/subgraph_reduction.h"

namespace gk_compact {

// Necessary local conditions only: root type, incident predicates, neighbor
// kinds/types, constants and self-loops. Eq-independent; never drops a root
// admitting an injective embedding. No search or neighborhood construction.
std::vector<VertexId> SelectRoots(Graph const& graph, GraphKey const& key);

// Build once per execution and retain the packed selections during all pair checks.
// No edge copies and no repeated BFS inside the pair loop.
Neighborhoods PrepareNeighborhoods(Graph const& input, std::span<VertexId const> roots,
                                   GraphKey const& key);
Neighborhoods PrepareNeighborhoods(Graph const& input, std::span<VertexId const> roots,
                                   std::size_t radius);
Neighborhoods PrepareNeighborhoods(Graph&&, std::span<VertexId const>, GraphKey const&) = delete;
Neighborhoods PrepareNeighborhoods(Graph const&&, std::span<VertexId const>,
                                   GraphKey const&) = delete;
Neighborhoods PrepareNeighborhoods(Graph&&, std::span<VertexId const>, std::size_t) = delete;
Neighborhoods PrepareNeighborhoods(Graph const&&, std::span<VertexId const>, std::size_t) = delete;

// Neighborhood storage must outlive the returned domains. Only enabled pairing
// needs these caches; they are reused across root pairs and Chase passes.
std::vector<PairingDomain> BuildPairingDomains(PairingPlan const& plan,
                                               Neighborhoods const& neighborhoods);
std::vector<PairingDomain> BuildPairingDomains(PairingPlan const&, Neighborhoods&&) = delete;
std::vector<PairingDomain> BuildPairingDomains(PairingPlan const&, Neighborhoods const&&) = delete;

// Inputs are already full graphs or neighborhoods, chosen by the caller.
// Only pair-specific filtering/reduction happens here. Inputs, key, evaluator
// and Eq share the original graph's numbering; options were checked up front.
bool EvaluatePair(EvalGK& evaluator, GraphKey const& key, EvalGKInput const& input,
                  EquivalenceRelation const& eq, bool use_pairing_filter = false,
                  bool use_subgraph_reduction = false);

// Prepared path for repeated checks. Domains belong to plan and preserve matches
// for input's roots. Input views are used for exact search: they may be the full
// base graph when the caller has established redundant localization. Reduction
// ALWAYS restricts the domain graphs, never bypasses their selections.
// Filter is reusable scratch shared across sequential pairs (and keys).
bool EvaluatePair(EvalGK& evaluator, EvalGKInput const& input, EquivalenceRelation const& eq,
                  PairingPlan const& plan, PairingFilter& filter, PairingDomain const& first,
                  PairingDomain const& second, SubgraphReduction* reduction = nullptr);

}  // namespace gk_compact
