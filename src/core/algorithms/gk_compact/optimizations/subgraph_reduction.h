#pragma once

#include "core/algorithms/gk_compact/optimizations/pairing_filter.h"

namespace gk_compact {

enum class PairingProjection { kFirst, kSecond };

// Keep the union of the selected projection over ALL pattern nodes, and every
// neighborhood edge whose endpoints are retained. Return sorted unique original
// IDs, all contained in neighborhood. The caller owns this selection and borrows
// the neighborhood's base CSR through GraphView. Only call for pairable roots.
std::vector<VertexId> ReduceSubgraph(GraphView neighborhood, PairingRelation const& pairing,
                                     PairingProjection projection);

}  // namespace gk_compact
