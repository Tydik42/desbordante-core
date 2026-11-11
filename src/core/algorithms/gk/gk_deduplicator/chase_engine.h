#pragma once

#include <cstddef>
#include <vector>

#include "core/algorithms/gk/model/equivalence_relation.h"
#include "core/algorithms/gk/model/key.h"

namespace gk {

struct ChaseRunResult {
    EquivalenceRelation::Classes clusters;
    std::size_t merge_count = 0;
};

class ChaseEngine {
public:
    explicit ChaseEngine(std::vector<model::GraphKeyDescriptor> const& keys);
    // Validated graph; original vertices are never rewritten. Starts from Eq_0.
    ChaseRunResult Run(model::Graph const& graph, bool use_localization = false,
                       bool use_pairing_filter = false, bool use_subgraph_reduction = false) const;

private:
    std::vector<GraphKey> keys_;
};

}  // namespace gk
