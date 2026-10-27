#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
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
        if (kind != other.kind || id_or_val != other.id_or_val) return false;

        // Types describe entities.  Data values are compared by their value only, as required by
        // the specification's =_D relation.
        return kind == NodeKind::kValue || type == other.type;
    }
};

struct Triple {
    Node subject;
    PredicateId predicate;
    Node object;
};

using Graph = std::vector<Triple>;

enum class VarKind { kEntity, kValue, kConstant, kWildcard };

struct Variable {
    VarKind kind;
    std::string name;

    bool operator==(Variable const& other) const {
        return kind == other.kind && name == other.name;
    }
};

}  // namespace gk::model

// TODO: workaround for joint mapping
template <>
struct std::hash<gk::model::Variable> {
    std::size_t operator()(gk::model::Variable const& v) const {
        std::size_t const name_hash = std::hash<std::string>{}(v.name);
        std::size_t const kind_hash = std::hash<int>{}(static_cast<int>(v.kind));
        return name_hash ^ (kind_hash + static_cast<std::size_t>(0x9e3779b9) + (name_hash << 6U) +
                            (name_hash >> 2U));
    }
};

namespace gk::model {
struct PatternTriple {
    Variable subject;
    PredicateId predicate;
    Variable object;
};

using Pattern = std::vector<PatternTriple>;

struct GraphKey {
    Pattern Q;
    std::unordered_map<Variable, TypeId> T_Q;
    Variable root_x;  // distinguished entity variable x in V_Q^E
};
}  // namespace gk::model
