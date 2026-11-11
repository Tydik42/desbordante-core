#pragma once

#include "core/algorithms/gk/optimizations/pairing_filter.h"

namespace gk {

enum class PairingProjection { kFirst, kSecond };

// Materializes the induced string graph on a projection of the surviving pairs.
model::Graph ReduceSubgraph(GraphView neighborhood, PairingRelation const& pairing,
                            PairingProjection projection);

}  // namespace gk
