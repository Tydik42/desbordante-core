#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <ranges>
#include <span>

#include "core/algorithms/gk_compact/model/graph.h"

namespace gk_compact {

// Borrows a graph and, optionally, a sorted unique subset of its active IDs.
// The subset defines an induced subgraph; an empty subset is NOT the full graph.
// Neither owner may be destroyed or replaced while this view or its ranges are used.
class GraphView {
private:
    Graph const* graph_;
    std::optional<std::span<VertexId const>> vertices_;

    auto FilterEdges(std::span<Edge const> edges) const {
        // Capture the selection, not this: ranges can outlive the view object,
        // but still borrow the underlying graph and selection storage.
        return edges | std::views::filter([vertices = vertices_](Edge const& edge) {
                   return !vertices || std::ranges::binary_search(*vertices, edge.neighbor);
               });
    }

public:
    GraphView(Graph const& graph) : graph_(&graph) {}

    // Precondition: vertices is sorted, unique and contained in graph. Owners
    // such as Neighborhoods establish this once, not on every pair check.
    GraphView(Graph const& graph, std::span<VertexId const> vertices)
        : graph_(&graph), vertices_(vertices) {}

    GraphView(Graph&&) = delete;
    GraphView(Graph const&&) = delete;
    GraphView(Graph&&, std::span<VertexId const>) = delete;
    GraphView(Graph const&&, std::span<VertexId const>) = delete;

    Graph const& Base() const noexcept {
        return *graph_;
    }

    std::size_t GetVertexCount() const noexcept {
        return vertices_ ? vertices_->size() : graph_->GetVertexCount();
    }

    std::size_t GetEntityCount() const noexcept {
        if (!vertices_) return graph_->GetEntityCount();
        if (vertices_->empty()) return 0;
        auto const end = std::ranges::lower_bound(*vertices_, graph_->Metadata().GetEntityCount());
        return static_cast<std::size_t>(end - vertices_->begin());
    }

    bool ContainsVertex(VertexId vertex) const noexcept {
        return vertices_ ? std::ranges::binary_search(*vertices_, vertex)
                         : graph_->ContainsVertex(vertex);
    }

    auto VertexIds() const {
        return std::views::iota(std::size_t{0}, GetVertexCount()) |
               std::views::transform([graph = graph_, vertices = vertices_](std::size_t row) {
                   if (vertices) return (*vertices)[row];
                   auto const& mapping = graph->Data().row_to_vertex;
                   return mapping.empty() ? static_cast<VertexId>(row) : mapping[row];
               });
    }

    auto EntityIds() const {
        return VertexIds() | std::views::take(static_cast<std::ptrdiff_t>(GetEntityCount()));
    }

    auto Outgoing(VertexId vertex) const {
        return FilterEdges(!vertices_ || ContainsVertex(vertex) ? graph_->Outgoing(vertex)
                                                                : std::span<Edge const>{});
    }

    auto Outgoing(VertexId vertex, PredicateId predicate) const {
        return FilterEdges(!vertices_ || ContainsVertex(vertex)
                                   ? graph_->Outgoing(vertex, predicate)
                                   : std::span<Edge const>{});
    }

    auto Incoming(VertexId vertex) const {
        return FilterEdges(!vertices_ || ContainsVertex(vertex) ? graph_->Incoming(vertex)
                                                                : std::span<Edge const>{});
    }

    auto Incoming(VertexId vertex, PredicateId predicate) const {
        return FilterEdges(!vertices_ || ContainsVertex(vertex)
                                   ? graph_->Incoming(vertex, predicate)
                                   : std::span<Edge const>{});
    }

    bool HasEdge(VertexId subject, PredicateId predicate, VertexId object) const {
        return (!vertices_ || (ContainsVertex(subject) && ContainsVertex(object))) &&
               graph_->HasEdge(subject, predicate, object);
    }

    // Properties use the original metadata, just as Graph's property accessors do.
    bool IsEntity(VertexId vertex) const {
        return graph_->IsEntity(vertex);
    }

    TypeId GetType(VertexId vertex) const {
        return graph_->GetType(vertex);
    }

    std::string const& GetLabel(VertexId vertex) const {
        return graph_->GetLabel(vertex);
    }
};

}  // namespace gk_compact
