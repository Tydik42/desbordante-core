#pragma once

#include <cstddef>

#include <utility>
#include <vector>

#include "core/algorithms/gk/model/key.h"

namespace gk {

using EntityPair = std::pair<model::EntityId, model::EntityId>;

struct ValidationResult {
    bool holds = true;
    std::vector<EntityPair> violations;
};

struct DeduplicationResult {
    std::vector<std::vector<model::EntityId>> clusters;
    std::size_t merge_count = 0;
};

}  // namespace gk
