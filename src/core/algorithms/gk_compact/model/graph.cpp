#include "core/algorithms/gk_compact/model/graph.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace gk_compact {
namespace {

std::optional<VertexId> FindRow(GraphData const& data, VertexId vertex) noexcept {
    if (data.row_to_vertex.empty()) {
        if (data.outgoing.offsets.empty() || vertex >= data.outgoing.offsets.size() - 1)
            return std::nullopt;
        return vertex;
    }
    auto const found = std::ranges::lower_bound(data.row_to_vertex, vertex);
    if (found == data.row_to_vertex.end() || *found != vertex) return std::nullopt;
    return static_cast<VertexId>(found - data.row_to_vertex.begin());
}

void CheckIndex(CsrData const& index, GraphData const& data, std::size_t vertices,
                std::size_t predicates) {
    if (index.offsets.empty()) {
        if (vertices != 0 || !index.edges.empty())
            throw std::invalid_argument("GK compact graph: missing CSR offsets");
        return;
    }
    if (index.offsets.size() != vertices + 1 || index.offsets.front() != 0 ||
        index.offsets.back() != index.edges.size() || !std::ranges::is_sorted(index.offsets)) {
        throw std::invalid_argument("GK compact graph: invalid CSR offsets");
    }
    for (auto const& edge : index.edges) {
        if (!FindRow(data, edge.neighbor) || edge.predicate >= predicates) {
            throw std::invalid_argument("GK compact graph: CSR edge has an invalid ID");
        }
    }
    for (std::size_t vertex = 0; vertex < vertices; ++vertex) {
        auto const edges = AdjacentRow(index, vertex);
        if (!std::ranges::is_sorted(edges) ||
            std::adjacent_find(edges.begin(), edges.end()) != edges.end()) {
            throw std::invalid_argument(
                    "GK compact graph: adjacency lists must be sorted and "
                    "unique");
        }
    }
}

void Validate(GraphData const& data) {
    if (!data.metadata) throw std::invalid_argument("GK compact graph: metadata must not be null");
    auto const& vertices = data.metadata->Vertices();
    auto const size = data.outgoing.offsets.empty() ? 0 : data.outgoing.offsets.size() - 1;
    if (size > vertices.labels.size())
        throw std::invalid_argument("GK compact graph: too many CSR rows");
    auto const& mapping = data.row_to_vertex;
    if (!mapping.empty() && (mapping.size() != size || !std::ranges::is_sorted(mapping) ||
                             std::adjacent_find(mapping.begin(), mapping.end()) != mapping.end() ||
                             mapping.back() >= vertices.labels.size()))
        throw std::invalid_argument("GK compact graph: invalid row-to-vertex mapping");
    auto const predicate_count = data.metadata->Predicates().size();
    CheckIndex(data.outgoing, data, size, predicate_count);
    CheckIndex(data.incoming, data, size, predicate_count);
    if (data.incoming.edges.size() != data.outgoing.edges.size() ||
        data.input_triple_count < data.outgoing.edges.size()) {
        throw std::invalid_argument("GK compact graph: inconsistent edge counts");
    }
    for (std::size_t vertex = 0; vertex < size; ++vertex) {
        auto const row = vertex;
        auto const id = mapping.empty() ? static_cast<VertexId>(row) : mapping[row];
        auto const edges = AdjacentRow(data.outgoing, row);
        if (id >= vertices.entity_count && !edges.empty()) {
            throw std::invalid_argument("GK compact graph: values cannot be triple subjects");
        }
        for (auto const& edge : edges) {
            if (!ContainsAtRow(data.incoming, *FindRow(data, edge.neighbor), edge.predicate, id)) {
                throw std::invalid_argument("GK compact graph: CSR directions disagree");
            }
        }
    }
}

}  // namespace

Graph::Graph(GraphData data) : data_(std::move(data)) {
    Validate(data_);
}

Graph::Graph(Graph&& other) noexcept : data_(std::move(other.data_)) {
    other.data_.metadata = data_.metadata;
    other.data_.row_to_vertex.clear();
    other.data_.outgoing.offsets.clear();
    other.data_.outgoing.edges.clear();
    other.data_.incoming.offsets.clear();
    other.data_.incoming.edges.clear();
    other.data_.input_triple_count = 0;
}

Graph& Graph::operator=(Graph const& other) {
    if (this != &other) {
        Graph copy(other);
        std::swap(data_, copy.data_);
    }
    return *this;
}

Graph& Graph::operator=(Graph&& other) noexcept {
    if (this != &other) {
        Graph moved(std::move(other));
        std::swap(data_, moved.data_);
    }
    return *this;
}

std::span<Edge const> Graph::Outgoing(VertexId vertex) const& {
    auto const row = FindRow(data_, vertex);
    return row ? AdjacentRow(data_.outgoing, *row) : std::span<Edge const>{};
}

std::span<Edge const> Graph::Outgoing(VertexId vertex, PredicateId predicate) const& {
    auto const row = FindRow(data_, vertex);
    return row ? AdjacentRow(data_.outgoing, *row, predicate) : std::span<Edge const>{};
}

std::span<Edge const> Graph::Incoming(VertexId vertex) const& {
    auto const row = FindRow(data_, vertex);
    return row ? AdjacentRow(data_.incoming, *row) : std::span<Edge const>{};
}

std::span<Edge const> Graph::Incoming(VertexId vertex, PredicateId predicate) const& {
    auto const row = FindRow(data_, vertex);
    return row ? AdjacentRow(data_.incoming, *row, predicate) : std::span<Edge const>{};
}

bool Graph::HasEdge(VertexId subject, PredicateId predicate, VertexId object) const {
    auto const row = FindRow(data_, subject);
    return row && ContainsAtRow(data_.outgoing, *row, predicate, object);
}

bool Graph::ContainsVertex(VertexId vertex) const noexcept {
    return FindRow(data_, vertex).has_value();
}

bool Graph::IsEntity(VertexId vertex) const {
    return GetType(vertex) != kNoType;
}

TypeId Graph::GetType(VertexId vertex) const {
    return Metadata().Vertices().types.at(vertex);
}

std::string const& Graph::GetLabel(VertexId vertex) const& {
    return Metadata().Vertices().labels.at(vertex);
}

std::optional<TypeId> Graph::FindType(std::string_view label) const {
    return FindLabel(Metadata().Types(), label);
}

std::optional<PredicateId> Graph::FindPredicate(std::string_view label) const {
    return FindLabel(Metadata().Predicates(), label);
}

std::optional<VertexId> Graph::FindEntity(std::string_view label) const {
    return gk_compact::FindEntity(Metadata().Vertices(), label);
}

std::optional<VertexId> Graph::FindValue(std::string_view label) const {
    return gk_compact::FindValue(Metadata().Vertices(), label);
}

std::span<Edge const> AdjacentRow(CsrData const& index, std::size_t row) {
    auto const first = index.offsets.at(row);
    auto const last = index.offsets.at(row + 1);
    return std::span<Edge const>(index.edges).subspan(first, last - first);
}

std::span<Edge const> AdjacentRow(CsrData const& index, std::size_t row, PredicateId predicate) {
    auto const matches =
            std::ranges::equal_range(AdjacentRow(index, row), predicate, {}, &Edge::predicate);
    return {matches.begin(), matches.end()};
}

bool ContainsAtRow(CsrData const& index, std::size_t row, PredicateId predicate,
                   VertexId neighbor) {
    return std::ranges::binary_search(AdjacentRow(index, row), Edge{predicate, neighbor});
}

gk::model::Node GetNode(GraphData const& data, VertexId vertex) {
    auto const& metadata = *data.metadata;
    auto const type = metadata.Vertices().types.at(vertex);
    auto const kind = type == kNoType ? gk::model::NodeKind::kValue : gk::model::NodeKind::kEntity;
    return {kind, metadata.Vertices().labels.at(vertex),
            type == kNoType ? "" : metadata.Types().at(type)};
}

}  // namespace gk_compact
