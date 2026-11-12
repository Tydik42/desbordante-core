#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "core/algorithms/gk_compact/optimizations/pairing_filter.h"

namespace gk_compact {

enum class PairingProjection { kFirst, kSecond };

// Owns reusable masks and both selections for one sequential pair evaluation.
// Compute invalidates previous spans. No graph, CSR or metadata is owned here.
// Input selections must not borrow this object's output buffers.
class SubgraphReduction {
public:
    void Compute(GraphView first, GraphView second, PairingRelation const& pairing);

    std::span<VertexId const> First() const& {
        return first_;
    }

    std::span<VertexId const> Second() const& {
        return second_;
    }

    std::span<VertexId const> First() const&& = delete;
    std::span<VertexId const> Second() const&& = delete;

private:
    std::vector<std::uint64_t> retained_;
    std::vector<VertexId> first_;
    std::vector<VertexId> second_;
};

// Keep the union of the selected projection over ALL pattern nodes, and every
// neighborhood edge whose endpoints are retained. Return sorted unique original
// IDs, all contained in neighborhood. The caller owns this selection and borrows
// the neighborhood's base CSR through GraphView. Only call for pairable roots.
std::vector<VertexId> ReduceSubgraph(GraphView neighborhood, PairingRelation const& pairing,
                                     PairingProjection projection);

}  // namespace gk_compact
