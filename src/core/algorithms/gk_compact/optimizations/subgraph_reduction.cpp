#include "core/algorithms/gk_compact/optimizations/subgraph_reduction.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace gk_compact {
namespace {

void Project(GraphView neighborhood, PairingRelation const& pairing, PairingProjection projection,
             std::vector<std::uint64_t>& retained, std::vector<VertexId>& result) {
    // Mark local positions, not original IDs. This remains small for sparse
    // neighborhoods and avoids a temporary list as large as the pairing relation.
    auto vertices = neighborhood.VertexIds();
    retained.assign((neighborhood.GetVertexCount() + 63) / 64, 0);
    std::size_t count = 0;
    for (auto const& pair : pairing.pairs) {
        auto const vertex = projection == PairingProjection::kFirst ? pair.first : pair.second;
        auto const found = std::ranges::lower_bound(vertices, vertex);
        auto const index = static_cast<std::size_t>(found - vertices.begin());
        auto& word = retained[index / 64];
        auto const bit = std::uint64_t{1} << (index % 64);
        if (word & bit) continue;
        word |= bit;
        ++count;
    }
    result.clear();
    result.reserve(count);
    std::size_t index = 0;
    for (auto vertex : vertices) {
        if (retained[index / 64] & (std::uint64_t{1} << (index % 64))) result.push_back(vertex);
        ++index;
    }
}

}  // namespace

void SubgraphReduction::Compute(GraphView first, GraphView second, PairingRelation const& pairing) {
    Project(first, pairing, PairingProjection::kFirst, retained_, first_);
    Project(second, pairing, PairingProjection::kSecond, retained_, second_);
}

std::vector<VertexId> ReduceSubgraph(GraphView neighborhood, PairingRelation const& pairing,
                                     PairingProjection projection) {
    std::vector<std::uint64_t> retained;
    std::vector<VertexId> result;
    Project(neighborhood, pairing, projection, retained, result);
    return result;
}

}  // namespace gk_compact
