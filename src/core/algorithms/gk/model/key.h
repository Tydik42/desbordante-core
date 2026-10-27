#pragma once

#include <cstddef>
#include <vector>

#include "core/algorithms/gk/graph_descriptor.h"

namespace gk {

using TraversalOrder = std::vector<std::size_t>;

// Prepared once at the boundary. Storage remains string-based; only the
// execution order is derived. Ready constraints precede further expansion.
struct CompiledKey {
    model::GraphKey descriptor;
    TraversalOrder traversal_order;
};

CompiledKey CompileKey(model::GraphKey key);

}  // namespace gk
