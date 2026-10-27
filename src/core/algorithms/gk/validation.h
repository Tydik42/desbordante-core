#pragma once

#include <vector>

#include "core/algorithms/gk/graph_descriptor.h"

namespace gk {

void CheckGraph(model::Graph const& graph);
void CheckKey(model::GraphKey const& key);

// Preserve first-occurrence order, including entities appearing only as objects.
std::vector<model::Node> CollectEntities(model::Graph const& graph);

}  // namespace gk
