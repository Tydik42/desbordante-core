#pragma once

#include <algorithm>
#include <span>
#include <vector>

#include "core/algorithms/gk_compact/model/graph_view.h"
#include "core/algorithms/gk_compact/model/key.h"

namespace gk_compact {

// P^Q grouped by pattern node. offsets has key.nodes.size() + 1 elements,
// starts at zero and ends at pairs.size(). Each range is sorted and unique.
// Empty ranges are allowed. All pair components are original graph vertex IDs.
struct PairingRelation {
    std::vector<Offset> offsets{0};
    std::vector<VertexPair> pairs;

    std::span<VertexPair const> Candidates(PatternNodeId node) const {
        auto const first = offsets.at(node);
        auto const last = offsets.at(static_cast<std::size_t>(node) + 1);
        return std::span<VertexPair const>(pairs).subspan(first, last - first);
    }

    bool Contains(PatternNodeId node, VertexId first, VertexId second) const {
        return std::ranges::binary_search(Candidates(node), VertexPair{first, second});
    }
};

// Greatest fixed point of typed/value-compatible pairs with two-sided edge
// support. Deliberately independent of Eq and injectivity, so Chase merges do
// not invalidate it. The caller tests Contains(key.root, first_root, second_root).
// Pairability is only necessary: a surviving pair still requires exact EvalGK.
PairingRelation ComputePairing(CompiledKey const& key, GraphView first_neighborhood,
                               GraphView second_neighborhood);

}  // namespace gk_compact
