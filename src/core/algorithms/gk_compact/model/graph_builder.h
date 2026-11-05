#pragma once

#include <unordered_map>

#include "core/algorithms/gk_compact/model/graph.h"

namespace gk_compact {

// Owns the temporary interning maps. Build consumes the builder and releases them;
// the finalized graph owns only dictionaries, vertex properties and two CSR indexes.
class GraphBuilder {
public:
    void AddTriple(gk::model::Triple const& triple);
    Graph Build() &&;

private:
    struct Triple {
        VertexId subject;
        PredicateId predicate;
        VertexId object;
        auto operator<=>(Triple const&) const = default;
    };

    GraphData data_;
    VertexData vertices_;
    std::vector<std::string> types_;
    std::vector<std::string> predicates_;
    std::vector<Triple> triples_;
    std::unordered_map<std::string, VertexId> entity_ids_;
    std::unordered_map<std::string, VertexId> value_ids_;
    std::unordered_map<std::string, TypeId> type_ids_;
    std::unordered_map<std::string, PredicateId> predicate_ids_;

    void CheckEntityType(gk::model::Node const& entity) const;
    VertexId InternNode(gk::model::Node const& node);
    std::vector<VertexId> OrderVertices();
    void BuildIndex(CsrData& index, bool incoming);
};

Graph BuildGraph(gk::model::Graph const& graph);

}  // namespace gk_compact
