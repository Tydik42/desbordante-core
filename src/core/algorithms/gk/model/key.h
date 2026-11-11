#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/algorithms/gk/model/graph.h"

namespace gk::model {

enum class VarKind { kEntity, kValue, kConstant, kWildcard };

struct Variable {
    VarKind kind;
    std::string name;
    bool operator==(Variable const&) const = default;
};

}  // namespace gk::model

template <>
struct std::hash<gk::model::Variable> {
    std::size_t operator()(gk::model::Variable const& variable) const {
        auto const name = std::hash<std::string>{}(variable.name);
        auto const kind = std::hash<int>{}(static_cast<int>(variable.kind));
        return name ^ (kind + static_cast<std::size_t>(0x9e3779b9) + (name << 6U) + (name >> 2U));
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
    Variable root_x;
};

}  // namespace gk::model

namespace gk {

using TraversalOrder = std::vector<std::size_t>;

// Prepared once at the boundary. Storage remains string-based; only the
// execution order is derived. Ready constraints precede further expansion.
struct CompiledKey {
    model::GraphKey descriptor;
    TraversalOrder traversal_order;
};

CompiledKey CompileKey(model::GraphKey key);

}  // namespace gk
