#pragma once

#include <cstddef>
#include <unordered_map>

#include "core/algorithms/gk/model/graph.h"

namespace gk {

// Shared input checks for string and compact graph builders.
void CheckTriple(model::Triple const& triple, std::size_t index);
void CheckGraph(model::Graph const& graph);

class GraphBuilder {
public:
    void AddTriple(model::Triple const& triple);
    model::Graph Build() &&;

private:
    model::Graph graph_;
    std::unordered_map<model::EntityId, model::TypeId> types_;
};

}  // namespace gk
