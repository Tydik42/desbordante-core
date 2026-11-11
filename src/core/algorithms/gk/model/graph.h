#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace gk::model {

using EntityId = std::string;
using TypeId = std::string;
using PredicateId = std::string;

enum class NodeKind { kEntity, kValue };

struct Node {
    NodeKind kind;
    EntityId id_or_val;
    TypeId type;

    bool operator==(Node const& other) const {
        return kind == other.kind && id_or_val == other.id_or_val &&
               (kind == NodeKind::kValue || type == other.type);
    }
};

struct Triple {
    Node subject;
    PredicateId predicate;
    Node object;
};

using Graph = std::vector<Triple>;

}  // namespace gk::model

namespace gk {
using VertexPair = std::pair<model::Node, model::Node>;
}  // namespace gk

template <>
struct std::hash<gk::model::Node> {
    std::size_t operator()(gk::model::Node const& node) const {
        auto hash = std::hash<std::string>{}(node.id_or_val);
        hash ^= std::hash<int>{}(static_cast<int>(node.kind)) + (hash << 6U) + (hash >> 2U);
        if (node.kind == gk::model::NodeKind::kEntity)
            hash ^= std::hash<std::string>{}(node.type) + (hash << 6U) + (hash >> 2U);
        return hash;
    }
};
