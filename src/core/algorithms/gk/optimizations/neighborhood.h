#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "core/algorithms/gk/graph_descriptor.h"

namespace gk {

// The key and source graph are already validated. Distances ignore direction.
std::size_t GetPatternRadius(model::GraphKey const& key);

// String graphs deliberately keep their original representation. Own induced
// graph copies once per root, not once per pair; preserve duplicate triples.
class Neighborhoods {
public:
    Neighborhoods(model::Graph const& graph, std::span<model::Node const* const> roots,
                  std::size_t radius);

    std::size_t size() const noexcept {
        return graphs_.size();
    }

    model::Graph const& operator[](std::size_t index) const& {
        return graphs_.at(index);
    }

    model::Graph const& operator[](std::size_t) const&& = delete;

private:
    std::vector<model::Graph> graphs_;
};

}  // namespace gk
