#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/algorithms/gk/model/key.h"
#include "core/algorithms/gk_compact/model/graph.h"

namespace gk_compact {

using PatternNodeId = std::uint32_t;
using TraversalOrder = std::vector<std::size_t>;

struct PatternNode {
    gk::model::VarKind kind;
    TypeId type = kNoType;
    std::optional<VertexId> constant;
};

struct PatternTriple {
    PatternNodeId subject;
    PredicateId predicate;
    PatternNodeId object;
};

// Produced by BuildGraphKey and bound to that graph's shared metadata. The traversal
// covers every triple and expands from root, checking all edges between reached
// nodes before the next expansion. Consumers trust these invariants;
// callers must not mutate the key or mix independently numbered graphs.
struct GraphKey {
    std::vector<PatternNode> nodes;
    std::vector<PatternTriple> triples;
    PatternNodeId root = 0;
    TraversalOrder traversal_order;
};

// Malformed descriptors throw. Missing types, predicates or constants yield
// nullopt: the key is valid but has no embeddings in this graph.
std::optional<GraphKey> BuildGraphKey(gk::model::GraphKeyDescriptor const& key, Graph const& graph);

}  // namespace gk_compact
