#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "core/algorithms/gk_compact/model/key.h"

namespace gk_compact {

struct ChaseRunResult {
    std::vector<std::vector<VertexId>> clusters;
    std::size_t merge_count = 0;
};

class ChaseEngine {
public:
    explicit ChaseEngine(std::vector<GraphKey> keys) : keys_(std::move(keys)) {}

    // Start each run at Eq_0 over the metadata's original entity IDs. Graph and
    // keys share one vertex/type/predicate numbering. Return only active entities,
    // including singletons when keys_ is empty; classes and members are sorted by ID.
    // Packed neighborhoods are reused across passes without copying graph edges.
    // Pairing/reduction are delegated to EvaluatePair; flag dependencies are checked once.
    ChaseRunResult Run(Graph const& graph, bool use_localization = false,
                       bool use_pairing_filter = false, bool use_subgraph_reduction = false) const;

private:
    std::vector<GraphKey> keys_;
};

}  // namespace gk_compact
