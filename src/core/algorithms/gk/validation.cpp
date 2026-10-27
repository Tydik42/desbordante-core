#include "core/algorithms/gk/validation.h"

#include <algorithm>
#include <deque>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <magic_enum/magic_enum.hpp>

#include "core/util/logger.h"

namespace gk {
namespace {

using gk::model::Graph;
using gk::model::Node;
using gk::model::NodeKind;
using gk::model::Pattern;
using gk::model::PatternTriple;
using gk::model::Variable;
using gk::model::VarKind;

[[noreturn]] void ThrowInvalid(std::string const& message) {
    LOG_ERROR("Invalid graph-key descriptor: {}", message);
    throw std::invalid_argument("Invalid graph-key descriptor: " + message);
}

void CheckNode(Node const& node, std::string const& location) {
    if (node.id_or_val.empty()) {
        ThrowInvalid(location + " has an empty identifier or value");
    }

    if (!magic_enum::enum_contains(node.kind)) {
        ThrowInvalid(location + " has an unknown node kind");
    }
    if (node.kind == NodeKind::kEntity && node.type.empty()) {
        ThrowInvalid(location + " entity has an empty type");
    }
    if (node.kind == NodeKind::kValue && !node.type.empty()) {
        ThrowInvalid(location + " value must not have an entity type");
    }
}

void RegisterEntityType(std::unordered_map<std::string, std::string>& entity_types,
                        Node const& entity, std::string const& location) {
    auto [it, inserted] = entity_types.emplace(entity.id_or_val, entity.type);
    if (!inserted && it->second != entity.type) {
        ThrowInvalid("entity " + entity.id_or_val + " has inconsistent types at " + location);
    }
}

void CheckVariable(Variable const& variable, std::string const& location) {
    if (variable.name.empty()) ThrowInvalid(location + " has an empty name");

    if (!magic_enum::enum_contains(variable.kind)) {
        ThrowInvalid(location + " has an unknown variable kind");
    }
}

void AddPatternVariable(std::vector<Variable>& variables, Variable const& variable,
                        std::string const& location) {
    CheckVariable(variable, location);

    if (std::find(variables.begin(), variables.end(), variable) == variables.end()) {
        variables.push_back(variable);
    }
}

std::vector<Variable> ValidatePattern(Pattern const& pattern) {
    if (pattern.empty()) ThrowInvalid("pattern must contain at least one triple");

    std::vector<Variable> variables;
    for (std::size_t index = 0; index < pattern.size(); ++index) {
        PatternTriple const& triple = pattern[index];
        bool const is_duplicate =
                std::any_of(pattern.begin(), pattern.begin() + static_cast<std::ptrdiff_t>(index),
                            [&triple](PatternTriple const& previous) {
                                return previous.subject == triple.subject &&
                                       previous.predicate == triple.predicate &&
                                       previous.object == triple.object;
                            });
        if (is_duplicate) {
            ThrowInvalid("pattern contains a duplicate triple at index " + std::to_string(index));
        }
        if (triple.predicate.empty()) {
            ThrowInvalid("pattern triple " + std::to_string(index) + " has an empty predicate");
        }

        if (triple.subject.kind != VarKind::kEntity && triple.subject.kind != VarKind::kWildcard) {
            ThrowInvalid("pattern triple " + std::to_string(index) +
                         " subject must be an entity variable or wildcard");
        }

        AddPatternVariable(variables, triple.subject,
                           "pattern triple " + std::to_string(index) + " subject");
        AddPatternVariable(variables, triple.object,
                           "pattern triple " + std::to_string(index) + " object");
    }
    return variables;
}

std::size_t FindVariable(std::vector<Variable> const& variables, Variable const& needle) {
    auto it = std::find(variables.begin(), variables.end(), needle);
    if (it == variables.end()) ThrowInvalid("internal pattern variable lookup failed");
    return static_cast<std::size_t>(std::distance(variables.begin(), it));
}

}  // namespace

void CheckGraph(model::Graph const& graph) {
    LOG_DEBUG("Validating GK graph descriptor: {} triples", graph.size());

    std::unordered_map<std::string, std::string> entity_types;
    std::size_t value_nodes = 0;
    for (std::size_t index = 0; index < graph.size(); ++index) {
        gk::model::Triple const& triple = graph[index];
        std::string const prefix = "graph triple " + std::to_string(index);

        CheckNode(triple.subject, prefix + " subject");
        if (triple.subject.kind != NodeKind::kEntity) {
            ThrowInvalid(prefix + " subject must be an entity");
        }
        RegisterEntityType(entity_types, triple.subject, prefix + " subject");

        if (triple.predicate.empty()) ThrowInvalid(prefix + " has an empty predicate");

        CheckNode(triple.object, prefix + " object");
        if (triple.object.kind == NodeKind::kEntity) {
            RegisterEntityType(entity_types, triple.object, prefix + " object");
        } else {
            ++value_nodes;
        }
    }

    LOG_DEBUG("GK graph descriptor is valid: {} distinct entities, {} value nodes",
              entity_types.size(), value_nodes);
}

void CheckKey(model::GraphKey const& key) {
    LOG_DEBUG("Validating GK key descriptor: {} pattern triples, {} type entries, root='{}'",
              key.Q.size(), key.T_Q.size(), key.root_x.name);

    std::vector<Variable> const variables = ValidatePattern(key.Q);
    if (key.root_x.name.empty()) ThrowInvalid("key root must not be empty");
    if (key.root_x.kind != VarKind::kEntity) {
        ThrowInvalid("key root must be an entity variable");
    }

    Variable const* root = nullptr;
    for (Variable const& variable : variables) {
        if (variable == key.root_x) {
            root = &variable;
            break;
        }
    }
    if (root == nullptr) ThrowInvalid("key root must be an entity variable in the pattern");

    std::unordered_set<Variable> typed_variables;
    for (Variable const& variable : variables) {
        bool const requires_type =
                variable.kind == VarKind::kEntity || variable.kind == VarKind::kWildcard;
        auto it = key.T_Q.find(variable);
        if (requires_type) {
            if (it == key.T_Q.end() || it->second.empty()) {
                ThrowInvalid("missing type for pattern variable " + variable.name);
            }
            typed_variables.insert(variable);
        }
    }

    for (auto const& [variable, type] : key.T_Q) {
        CheckVariable(variable, "key type map variable");
        if (type.empty()) {
            ThrowInvalid("empty type for pattern variable " + variable.name);
        }
        if (variable.kind != VarKind::kEntity && variable.kind != VarKind::kWildcard) {
            ThrowInvalid("type is provided for a non-entity pattern variable " + variable.name);
        }
        if (!typed_variables.contains(variable)) {
            ThrowInvalid("type is provided for a variable outside the pattern " + variable.name);
        }
    }

    std::vector<std::vector<std::size_t>> adjacency(variables.size());
    for (PatternTriple const& triple : key.Q) {
        std::size_t const subject = FindVariable(variables, triple.subject);
        std::size_t const object = FindVariable(variables, triple.object);
        adjacency[subject].push_back(object);
        adjacency[object].push_back(subject);
    }

    std::size_t const root_index = FindVariable(variables, *root);
    std::unordered_set<std::size_t> reached;
    reached.reserve(variables.size());
    std::deque<std::size_t> pending;
    reached.insert(root_index);
    pending.push_back(root_index);
    while (!pending.empty()) {
        std::size_t const current = pending.front();
        pending.pop_front();
        for (std::size_t next : adjacency[current]) {
            if (reached.contains(next)) continue;
            reached.insert(next);
            pending.push_back(next);
        }
    }

    if (reached.size() != variables.size()) {
        ThrowInvalid("pattern must be connected with respect to the root");
    }

    LOG_DEBUG("GK key descriptor is valid: {} pattern variables, root='{}'", variables.size(),
              key.root_x.name);
}

std::vector<Node> CollectEntities(Graph const& graph) {
    std::vector<Node> entities;
    std::unordered_set<std::string> seen_ids;
    auto add_entity = [&entities, &seen_ids](Node const& node) {
        if (node.kind != NodeKind::kEntity) return;
        if (seen_ids.insert(node.id_or_val).second) {
            entities.push_back(node);
        }
    };

    for (auto const& triple : graph) {
        add_entity(triple.subject);
        add_entity(triple.object);
    }
    return entities;
}

}  // namespace gk
