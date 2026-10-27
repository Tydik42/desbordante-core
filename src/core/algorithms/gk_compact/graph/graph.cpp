#include "core/algorithms/gk_compact/graph/graph.h"

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
                "GK compact graph: labels must be nonempty, sorted and unique "
                "within the 32-bit ID space");
    }
}

void CheckIndex(CsrData const& index, std::size_t vertices, std::size_t predicates) {
    if (index.offsets.size() != vertices + 1 || index.offsets.front() != 0 ||
        index.offsets.back() != index.edges.size() || !std::ranges::is_sorted(index.offsets)) {
        throw std::invalid_argument("GK compact graph: invalid CSR offsets");
    }
    for (auto const& edge : index.edges) {
        if (edge.neighbor >= vertices || edge.predicate >= predicates) {
            throw std::invalid_argument("GK compact graph: CSR edge has an invalid ID");
        }
    }
    for (std::size_t vertex = 0; vertex < vertices; ++vertex) {
        auto const edges = Adjacent(index, static_cast<VertexId>(vertex));
        if (!std::ranges::is_sorted(edges) ||
            std::adjacent_find(edges.begin(), edges.end()) != edges.end()) {
            throw std::invalid_argument(
                    "GK compact graph: adjacency lists must be sorted and "
                    "unique");
        }
    }
}

void Validate(GraphData const& data) {
    auto const& vertices = data.vertices;
    auto const size = vertices.labels.size();
    if (size > std::numeric_limits<VertexId>::max() || vertices.types.size() != size ||
        vertices.entity_count > size) {
        throw std::invalid_argument("GK compact graph: invalid vertex arrays");
    }
    auto const labels = std::span<std::string const>(vertices.labels);
    CheckLabels(labels.first(vertices.entity_count));
    CheckLabels(labels.subspan(vertices.entity_count));
    CheckLabels(data.types);
    CheckLabels(data.predicates);
    for (std::size_t vertex = 0; vertex < size; ++vertex) {
        auto const type = vertices.types[vertex];
        if (vertex < vertices.entity_count ? type >= data.types.size() : type != kNoType) {
            throw std::invalid_argument("GK compact graph: invalid vertex type");
        }
    }
    CheckIndex(data.outgoing, size, data.predicates.size());
    CheckIndex(data.incoming, size, data.predicates.size());
    if (data.incoming.edges.size() != data.outgoing.edges.size() ||
        data.input_triple_count < data.outgoing.edges.size()) {
        throw std::invalid_argument("GK compact graph: inconsistent edge counts");
    }
    for (std::size_t vertex = 0; vertex < size; ++vertex) {
        auto const id = static_cast<VertexId>(vertex);
        auto const edges = Adjacent(data.outgoing, id);
        if (vertex >= vertices.entity_count && !edges.empty()) {
            throw std::invalid_argument("GK compact graph: values cannot be triple subjects");
        }
        for (auto const& edge : edges) {
            if (!Contains(data.incoming, edge.neighbor, edge.predicate, id)) {
                throw std::invalid_argument("GK compact graph: CSR directions disagree");
            }
        }
    }
}

}  // namespace

Graph::Graph(GraphData data) : data_(std::move(data)) {
    Validate(data_);
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

std::span<Edge const> Adjacent(CsrData const& index, VertexId vertex) {
    auto const first = index.offsets.at(vertex);
    auto const last = index.offsets.at(static_cast<std::size_t>(vertex) + 1);
    return std::span<Edge const>(index.edges).subspan(first, last - first);
}

std::span<Edge const> Adjacent(CsrData const& index, VertexId vertex, PredicateId predicate) {
    auto const matches =
            std::ranges::equal_range(Adjacent(index, vertex), predicate, {}, &Edge::predicate);
    return {matches.begin(), matches.end()};
}

bool Contains(CsrData const& index, VertexId vertex, PredicateId predicate, VertexId neighbor) {
    return std::ranges::binary_search(Adjacent(index, vertex), Edge{predicate, neighbor});
}

gk::model::Node GetNode(GraphData const& data, VertexId vertex) {
    auto const type = data.vertices.types.at(vertex);
    auto const kind = type == kNoType ? gk::model::NodeKind::kValue : gk::model::NodeKind::kEntity;
    return {kind, data.vertices.labels.at(vertex), type == kNoType ? "" : data.types.at(type)};
}

}  // namespace gk_compact
