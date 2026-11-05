#include "core/algorithms/gk_compact/model/graph_metadata.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace gk_compact {
namespace {

void CheckLabels(std::span<std::string const> labels) {
    if (labels.size() > std::numeric_limits<std::uint32_t>::max() ||
        !std::ranges::is_sorted(labels) ||
        std::adjacent_find(labels.begin(), labels.end()) != labels.end() ||
        std::ranges::any_of(labels, [](auto const& label) { return label.empty(); })) {
        throw std::invalid_argument(
                "GK compact metadata: labels must be nonempty, sorted and unique "
                "within the 32-bit ID space");
    }
}

}  // namespace

GraphMetadata::GraphMetadata(VertexData vertices, std::vector<std::string> types,
                             std::vector<std::string> predicates)
    : vertices_(std::move(vertices)), types_(std::move(types)), predicates_(std::move(predicates)) {
    auto const size = vertices_.labels.size();
    if (size > std::numeric_limits<VertexId>::max() || vertices_.types.size() != size ||
        vertices_.entity_count > size) {
        throw std::invalid_argument("GK compact metadata: invalid vertex arrays");
    }
    auto const labels = std::span<std::string const>(vertices_.labels);
    CheckLabels(labels.first(vertices_.entity_count));
    CheckLabels(labels.subspan(vertices_.entity_count));
    CheckLabels(types_);
    if (std::ranges::binary_search(types_, "-"))
        throw std::invalid_argument("GK compact metadata: '-' is reserved for value types in CSV");
    CheckLabels(predicates_);
    for (std::size_t vertex = 0; vertex < size; ++vertex) {
        auto const type = vertices_.types[vertex];
        if (vertex < vertices_.entity_count ? type >= types_.size() : type != kNoType)
            throw std::invalid_argument("GK compact metadata: invalid vertex type");
    }
}

std::optional<std::uint32_t> FindLabel(std::span<std::string const> labels,
                                       std::string_view label) {
    auto const found = std::ranges::lower_bound(labels, label);
    if (found == labels.end() || *found != label) return std::nullopt;
    return static_cast<std::uint32_t>(found - labels.begin());
}

std::optional<VertexId> FindEntity(VertexData const& vertices, std::string_view id) {
    return FindLabel(std::span<std::string const>(vertices.labels).first(vertices.entity_count),
                     id);
}

std::optional<VertexId> FindValue(VertexData const& vertices, std::string_view value) {
    auto const found = FindLabel(
            std::span<std::string const>(vertices.labels).subspan(vertices.entity_count), value);
    if (!found) return std::nullopt;
    return static_cast<VertexId>(vertices.entity_count + *found);
}

}  // namespace gk_compact
