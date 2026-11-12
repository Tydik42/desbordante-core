#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "core/algorithms/gk_compact/model/graph_view.h"
#include "core/algorithms/gk_compact/model/key.h"

namespace gk_compact {

// Maximum undirected distance from key.root in the pattern, not traversal length.
std::size_t GetPatternRadius(GraphKey const& key);

// Owns only sorted vertex selections, packed in one flat array with offsets.
// Neighborhood i corresponds to roots[i]. The graph is borrowed and never copied.
// Distances ignore direction and predicate; isolated roots are retained.
class Neighborhoods {
public:
    Neighborhoods(Graph const& input, std::span<VertexId const> roots, std::size_t radius);
    Neighborhoods(Graph&&, std::span<VertexId const>, std::size_t) = delete;
    Neighborhoods(Graph const&&, std::span<VertexId const>, std::size_t) = delete;

    std::size_t size() const noexcept {
        return offsets_.empty() ? 0 : offsets_.size() - 1;
    }

    GraphView operator[](std::size_t index) const& {
        auto const first = offsets_.at(index);
        auto const last = offsets_.at(index + 1);
        return GraphView(*graph_,
                         std::span<VertexId const>(vertices_).subspan(first, last - first));
    }

    GraphView operator[](std::size_t) const&& = delete;

private:
    Graph const* graph_;
    std::vector<Offset> offsets_{0};
    std::vector<VertexId> vertices_;
};

}  // namespace gk_compact
