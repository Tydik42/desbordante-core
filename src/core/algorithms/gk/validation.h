#pragma once

#include <cstddef>
#include <vector>

#include "core/algorithms/gk/graph_descriptor.h"

namespace gk {

// Local checks only; graph-wide entity type consistency belongs to the graph builder/checker.
void CheckTriple(model::Triple const& triple, std::size_t index);
void CheckGraph(model::Graph const& graph);
void CheckKey(model::GraphKey const& key);

// Preserve first-occurrence order, including entities appearing only as objects.
std::vector<model::Node> CollectEntities(model::Graph const& graph);

}  // namespace gk
