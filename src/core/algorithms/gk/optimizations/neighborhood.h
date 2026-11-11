#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "core/algorithms/gk/model/graph_view.h"
#include "core/algorithms/gk/model/key.h"

namespace gk {

// The key and source graph are already validated. Distances ignore direction.
std::size_t GetPatternRadius(GraphKey const& key);

// String graphs deliberately keep their original representation. Own induced
// graph copies once per root, not once per pair; preserve duplicate triples.
class Neighborhoods {
public:
    Neighborhoods(model::Graph const& graph, std::span<model::Node const* const> roots,
                  std::size_t radius);
    Neighborhoods(model::Graph&&, std::span<model::Node const* const>, std::size_t) = delete;
    Neighborhoods(model::Graph const&&, std::span<model::Node const* const>, std::size_t) = delete;

    std::size_t size() const noexcept {
        return graphs_.size();
    }

    GraphView operator[](std::size_t index) const& {
        return graphs_.at(index);
    }

    GraphView operator[](std::size_t) const&& = delete;

private:
    std::vector<model::Graph> graphs_;
};

}  // namespace gk
