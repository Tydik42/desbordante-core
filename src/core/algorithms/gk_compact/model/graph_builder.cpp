#include "core/algorithms/gk_compact/model/graph_builder.h"

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <utility>

#include "core/algorithms/gk/model/validation.h"
#include "core/util/logger.h"

namespace gk_compact {
namespace {

std::uint32_t CheckedId(std::size_t count) {
    if (count >= std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("GK compact graph exhausted its 32-bit ID space");
    }
    return static_cast<std::uint32_t>(count);
}

std::uint32_t InternLabel(std::string const& label,
                          std::unordered_map<std::string, std::uint32_t>& lookup,
                          std::vector<std::string>& labels) {
    if (auto const found = lookup.find(label); found != lookup.end()) return found->second;
    auto const id = CheckedId(labels.size());
    labels.push_back(label);
    lookup.emplace(label, id);
    return id;
}

// Sort strings in place. Temporary permutations are discarded before CSR construction.
std::vector<std::uint32_t> OrderLabels(std::vector<std::string>& labels) {
    std::vector<std::uint32_t> order(labels.size());
    std::iota(order.begin(), order.end(), 0U);
    std::ranges::sort(order, {}, [&](auto id) -> auto const& { return labels[id]; });
    std::vector<std::uint32_t> remap(order.size());
    for (std::size_t i = 0; i < order.size(); ++i) remap[order[i]] = static_cast<std::uint32_t>(i);
    order = remap;
    for (std::size_t i = 0; i < order.size(); ++i) {
        while (order[i] != i) {
            auto const next = order[i];
            std::swap(labels[i], labels[next]);
            std::swap(order[i], order[next]);
        }
    }
    return remap;
}

}  // namespace

void GraphBuilder::CheckEntityType(gk::model::Node const& entity) const {
    if (entity.kind != gk::model::NodeKind::kEntity) return;
    auto const found = entity_ids_.find(entity.id_or_val);
    if (found != entity_ids_.end() && types_.at(vertices_.types.at(found->second)) != entity.type) {
        throw std::invalid_argument("GK compact graph: entity " + entity.id_or_val +
                                    " has inconsistent types");
    }
}

VertexId GraphBuilder::InternNode(gk::model::Node const& node) {
    bool const entity = node.kind == gk::model::NodeKind::kEntity;
    auto& lookup = entity ? entity_ids_ : value_ids_;
    if (auto const found = lookup.find(node.id_or_val); found != lookup.end()) return found->second;
    auto const vertex = CheckedId(vertices_.labels.size());
    auto const type = entity ? InternLabel(node.type, type_ids_, types_) : kNoType;
    vertices_.types.push_back(type);
    vertices_.labels.push_back(node.id_or_val);
    if (entity) ++vertices_.entity_count;
    lookup.emplace(node.id_or_val, vertex);
    return vertex;
}

void GraphBuilder::AddTriple(gk::model::Triple const& triple) {
    gk::CheckTriple(triple, data_.input_triple_count);
    CheckEntityType(triple.subject);
    CheckEntityType(triple.object);
    if (triple.object.kind == gk::model::NodeKind::kEntity &&
        triple.subject.id_or_val == triple.object.id_or_val &&
        triple.subject.type != triple.object.type) {
        throw std::invalid_argument("GK compact graph: subject and object disagree on entity type");
    }
    auto const subject = InternNode(triple.subject);
    auto const object = InternNode(triple.object);
    auto const predicate = InternLabel(triple.predicate, predicate_ids_, predicates_);
    triples_.push_back({subject, predicate, object});
    ++data_.input_triple_count;
}

std::vector<VertexId> GraphBuilder::OrderVertices() {
    auto& vertices = vertices_;
    std::vector<VertexId> order(vertices.labels.size());
    std::iota(order.begin(), order.end(), 0U);
    std::ranges::sort(order, [&](auto lhs, auto rhs) {
        bool const lhs_value = vertices.types[lhs] == kNoType;
        bool const rhs_value = vertices.types[rhs] == kNoType;
        if (lhs_value != rhs_value) return lhs_value < rhs_value;
        return vertices.labels[lhs] < vertices.labels[rhs];
    });
    std::vector<VertexId> remap(order.size());
    for (std::size_t i = 0; i < order.size(); ++i) remap[order[i]] = static_cast<VertexId>(i);
    order = remap;
    for (std::size_t i = 0; i < order.size(); ++i) {
        while (order[i] != i) {
            auto const next = order[i];
            std::swap(vertices.labels[i], vertices.labels[next]);
            std::swap(vertices.types[i], vertices.types[next]);
            std::swap(order[i], order[next]);
        }
    }
    return remap;
}

void GraphBuilder::BuildIndex(CsrData& index, bool incoming) {
    index.offsets.assign(vertices_.labels.size() + 1, 0);
    auto from = [incoming](Triple const& triple) {
        return incoming ? triple.object : triple.subject;
    };
    auto to = [incoming](Triple const& triple) {
        return incoming ? triple.subject : triple.object;
    };
    for (auto const& triple : triples_) ++index.offsets[static_cast<std::size_t>(from(triple)) + 1];
    std::partial_sum(index.offsets.begin(), index.offsets.end(), index.offsets.begin());
    index.edges.resize(triples_.size());
    auto next = index.offsets;
    for (auto const& triple : triples_) {
        index.edges[next[from(triple)]++] = {triple.predicate, to(triple)};
    }
    for (std::size_t vertex = 0; vertex < vertices_.labels.size(); ++vertex) {
        auto edges = std::span<Edge>(index.edges)
                             .subspan(index.offsets[vertex],
                                      index.offsets[vertex + 1] - index.offsets[vertex]);
        std::ranges::sort(edges);
    }
}

Graph GraphBuilder::Build() && {
    {
        auto const types = OrderLabels(types_);
        auto const predicates = OrderLabels(predicates_);
        for (auto& type : vertices_.types) {
            if (type != kNoType) type = types[type];
        }
        auto const vertices = OrderVertices();
        for (auto& triple : triples_) {
            triple = {vertices[triple.subject], predicates[triple.predicate],
                      vertices[triple.object]};
        }
    }
    decltype(entity_ids_){}.swap(entity_ids_);
    decltype(value_ids_){}.swap(value_ids_);
    decltype(type_ids_){}.swap(type_ids_);
    decltype(predicate_ids_){}.swap(predicate_ids_);

    std::ranges::sort(triples_);
    auto const duplicates = std::ranges::unique(triples_);
    triples_.erase(duplicates.begin(), duplicates.end());
    BuildIndex(data_.outgoing, false);
    BuildIndex(data_.incoming, true);
    data_.metadata = std::make_shared<GraphMetadata const>(std::move(vertices_), std::move(types_),
                                                           std::move(predicates_));
    Graph result(std::move(data_));
    LOG_DEBUG("Built compact GK graph: {} vertices, {} entities, {} unique edges, {} input triples",
              result.GetVertexCount(), result.GetEntityCount(), result.GetEdgeCount(),
              result.GetInputTripleCount());
    *this = GraphBuilder{};
    return result;
}

Graph BuildGraph(gk::model::Graph const& graph) {
    GraphBuilder builder;
    for (auto const& triple : graph) builder.AddTriple(triple);
    return std::move(builder).Build();
}

}  // namespace gk_compact
