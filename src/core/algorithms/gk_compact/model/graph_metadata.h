#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gk_compact {

using VertexId = std::uint32_t;
using PredicateId = std::uint32_t;
using TypeId = std::uint32_t;
using Offset = std::uint64_t;
inline constexpr TypeId kNoType = std::numeric_limits<TypeId>::max();

// Entities occupy [0, entity_count), followed by values. Both label ranges are
// sorted and unique; values have type kNoType. IDs belong to this metadata domain.
struct VertexData {
    std::vector<std::string> labels;
    std::vector<TypeId> types;
    std::size_t entity_count = 0;
};

// Validates dictionaries and vertex properties once. Graphs share a const
// instance, keeping one numbering without copying strings or type arrays.
class GraphMetadata {
public:
    explicit GraphMetadata(VertexData vertices = {}, std::vector<std::string> types = {},
                           std::vector<std::string> predicates = {});

    GraphMetadata(GraphMetadata const&) = delete;
    GraphMetadata& operator=(GraphMetadata const&) = delete;

    std::size_t GetVertexCount() const noexcept {
        return vertices_.labels.size();
    }

    std::size_t GetEntityCount() const noexcept {
        return vertices_.entity_count;
    }

    VertexData const& Vertices() const& noexcept {
        return vertices_;
    }

    VertexData const& Vertices() const&& = delete;

    std::vector<std::string> const& Types() const& noexcept {
        return types_;
    }

    std::vector<std::string> const& Types() const&& = delete;

    std::vector<std::string> const& Predicates() const& noexcept {
        return predicates_;
    }

    std::vector<std::string> const& Predicates() const&& = delete;

private:
    VertexData vertices_;
    std::vector<std::string> types_;
    std::vector<std::string> predicates_;
};

// Queries require validated metadata. No builder interning maps are retained.
std::optional<std::uint32_t> FindLabel(std::span<std::string const> labels, std::string_view label);
std::optional<VertexId> FindEntity(VertexData const& vertices, std::string_view id);
std::optional<VertexId> FindValue(VertexData const& vertices, std::string_view value);

inline auto EntityIds(VertexData const& vertices) noexcept {
    return std::views::iota(VertexId{0}, static_cast<VertexId>(vertices.entity_count));
}

}  // namespace gk_compact
