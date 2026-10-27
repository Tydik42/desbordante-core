#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/algorithms/gk/model/key.h"

namespace gk_compact {

using VertexId = std::uint32_t;
using PredicateId = std::uint32_t;
using TypeId = std::uint32_t;
using Offset = std::uint64_t;
inline constexpr TypeId kNoType = std::numeric_limits<TypeId>::max();

struct Edge {
    PredicateId predicate;
    VertexId neighbor;
    auto operator<=>(Edge const&) const = default;
};

// Entities occupy [0, entity_count), followed by values. Each range is sorted by
// its original string. Values have type kNoType; no separate kind array is needed.
struct VertexData {
    std::vector<std::string> labels;
    std::vector<TypeId> types;
    std::size_t entity_count = 0;
};

// Each adjacency list is sorted by (predicate, neighbor), without duplicates.
struct CsrData {
    std::vector<Offset> offsets{0};
    std::vector<Edge> edges;
};

// Mutable construction data, not a validated graph. Dictionaries are sorted and
// unique. Incoming and outgoing CSR indexes describe the same set of triples.
struct GraphData {
    VertexData vertices;
    std::vector<std::string> types;
    std::vector<std::string> predicates;
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

    GraphData const& Data() const& noexcept {
        return data_;
    }

    GraphData const& Data() const&& = delete;

    std::size_t GetVertexCount() const noexcept {
        return data_.vertices.labels.size();
    }

    std::size_t GetEntityCount() const noexcept {
        return data_.vertices.entity_count;
    }

    std::size_t GetEdgeCount() const noexcept {
        return data_.outgoing.edges.size();
    }

    std::size_t GetInputTripleCount() const noexcept {
        return data_.input_triple_count;
    }

private:
    GraphData data_;
};

// These queries require validated data from Graph::Data().
std::optional<std::uint32_t> FindLabel(std::span<std::string const> labels, std::string_view label);
std::optional<VertexId> FindEntity(VertexData const& vertices, std::string_view id);
std::optional<VertexId> FindValue(VertexData const& vertices, std::string_view value);

inline auto EntityIds(VertexData const& vertices) noexcept {
    return std::views::iota(VertexId{0}, static_cast<VertexId>(vertices.entity_count));
}

std::span<Edge const> Adjacent(CsrData const& index, VertexId vertex);
std::span<Edge const> Adjacent(CsrData const& index, VertexId vertex, PredicateId predicate);
bool Contains(CsrData const& index, VertexId vertex, PredicateId predicate, VertexId neighbor);

// Reconstruct strings only at the boundary, for results and diagnostics.
gk::model::Node GetNode(GraphData const& data, VertexId vertex);

}  // namespace gk_compact
