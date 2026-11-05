#pragma once

#include <utility>
#include <vector>

#include "core/algorithms/gk/graph_descriptor.h"

namespace gk {

using EntityPair = std::pair<model::EntityId, model::EntityId>;

struct ValidationResult {
    bool holds = true;
    std::vector<EntityPair> violations;
};

}  // namespace gk
