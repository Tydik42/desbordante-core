#pragma once

#include <unordered_map>

#include "core/algorithms/gk/model/graph.h"

namespace gk {

class GraphBuilder {
public:
    void AddTriple(model::Triple const& triple);
    model::Graph Build() &&;

private:
    model::Graph graph_;
    std::unordered_map<model::EntityId, model::TypeId> types_;
};

}  // namespace gk
