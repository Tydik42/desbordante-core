#pragma once

#include <algorithm>
#include <compare>
#include <cstddef>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/algorithms/gk/model/graph.h"
#include "core/algorithms/gk_compact/model/graph_metadata.h"

namespace gk_compact {

using VertexPair = std::pair<VertexId, VertexId>;

struct Edge {
    PredicateId predicate;
    VertexId neighbor;
    auto operator<=>(Edge const&) const = default;
};

// Each adjacency list is sorted by (predicate, neighbor), without duplicates.
// An empty index may have either no offsets or the single offset zero.
struct CsrData {
    std::vector<Offset> offsets{0};
    std::vector<Edge> edges;
};

// Mutable construction data. Empty row_to_vertex means row i has original ID i;
// otherwise the sorted unique mapping has one entry per CSR row. Neighbor IDs
// are always original metadata IDs, never row positions. Both indexes describe
// the same edges between active vertices.
struct GraphData {
    std::shared_ptr<GraphMetadata const> metadata = std::make_shared<GraphMetadata const>();
    std::vector<VertexId> row_to_vertex;
    CsrData outgoing;
    CsrData incoming;
    std::size_t input_triple_count = 0;
};

// Owns and validates the data once, then exposes only read access. References and
// spans borrow this object: do not move/replace it while using a borrowed view.
class Graph {
public:
    Graph() = default;
    explicit Graph(GraphData data);
    Graph(Graph const&) = default;
    Graph& operator=(Graph const& other);
    // Moving leaves an empty topology with the same valid metadata domain.
    Graph(Graph&& other) noexcept;
    Graph& operator=(Graph&& other) noexcept;

    GraphData const& Data() const& noexcept {
        return data_;
    }

    GraphData const& Data() const&& = delete;

    GraphMetadata const& Metadata() const& noexcept {
        return *data_.metadata;
    }

    GraphMetadata const& Metadata() const&& = delete;

    std::size_t GetVertexCount() const noexcept {
        return data_.outgoing.offsets.empty() ? 0 : data_.outgoing.offsets.size() - 1;
    }

    std::size_t GetEntityCount() const noexcept {
        auto const domain = Metadata().GetEntityCount();
        if (data_.row_to_vertex.empty()) return std::min(GetVertexCount(), domain);
        auto const end = std::ranges::lower_bound(data_.row_to_vertex, domain);
        return static_cast<std::size_t>(end - data_.row_to_vertex.begin());
    }

    std::size_t GetEdgeCount() const noexcept {
        return data_.outgoing.edges.size();
    }

    std::size_t GetInputTripleCount() const noexcept {
        return data_.input_triple_count;
    }

    std::span<Edge const> Outgoing(VertexId vertex) const&;
    std::span<Edge const> Outgoing(VertexId vertex, PredicateId predicate) const&;
    std::span<Edge const> Incoming(VertexId vertex) const&;
    std::span<Edge const> Incoming(VertexId vertex, PredicateId predicate) const&;
    std::span<Edge const> Outgoing(VertexId) const&& = delete;
    std::span<Edge const> Outgoing(VertexId, PredicateId) const&& = delete;
    std::span<Edge const> Incoming(VertexId) const&& = delete;
    std::span<Edge const> Incoming(VertexId, PredicateId) const&& = delete;
    bool HasEdge(VertexId subject, PredicateId predicate, VertexId object) const;
    bool ContainsVertex(VertexId vertex) const noexcept;

    bool IsEntity(VertexId vertex) const;
    TypeId GetType(VertexId vertex) const;
    std::string const& GetLabel(VertexId vertex) const&;
    std::string const& GetLabel(VertexId) const&& = delete;

    auto VertexIds() const& noexcept {
        return std::views::iota(VertexId{0}, static_cast<VertexId>(GetVertexCount())) |
               std::views::transform(
                       [mapping = std::span<VertexId const>(data_.row_to_vertex)](VertexId row) {
                           return mapping.empty() ? row : mapping[row];
                       });
    }

    auto VertexIds() const&& = delete;

    auto EntityIds() const& noexcept {
        return VertexIds() | std::views::take(static_cast<std::ptrdiff_t>(GetEntityCount()));
    }

    auto EntityIds() const&& = delete;

    // Resolve IDs in the shared metadata, including inactive vertices. Use
    // ContainsVertex for membership and VertexIds/EntityIds for active traversal.
    std::optional<TypeId> FindType(std::string_view label) const;
    std::optional<PredicateId> FindPredicate(std::string_view label) const;
    std::optional<VertexId> FindEntity(std::string_view label) const;
    std::optional<VertexId> FindValue(std::string_view label) const;

private:
    GraphData data_;
};

// Low-level CSR queries take a LOCAL ROW, unlike Graph's original-ID interface.
std::span<Edge const> AdjacentRow(CsrData const& index, std::size_t row);
std::span<Edge const> AdjacentRow(CsrData const& index, std::size_t row, PredicateId predicate);
bool ContainsAtRow(CsrData const& index, std::size_t row, PredicateId predicate, VertexId neighbor);

// Reconstruct strings only at the boundary, for results and diagnostics.
gk::model::Node GetNode(GraphData const& data, VertexId vertex);

}  // namespace gk_compact
