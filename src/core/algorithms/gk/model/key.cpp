#include "core/algorithms/gk/model/key.h"

#include <algorithm>
#include <cassert>
#include <deque>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

#include <magic_enum/magic_enum.hpp>

#include "core/util/logger.h"

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
#include <chrono>
#endif

namespace gk {
namespace {

using gk::model::Pattern;
using gk::model::PatternTriple;
using gk::model::Variable;
using gk::model::VarKind;

[[noreturn]] void ThrowInvalid(std::string const& message) {
    LOG_ERROR("Invalid graph-key descriptor: {}", message);
    throw std::invalid_argument("Invalid graph-key descriptor: " + message);
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

TraversalOrder BuildTraversalOrder(model::GraphKeyDescriptor const& key) {
    TraversalOrder order;
    order.reserve(key.Q.size());

    std::unordered_set<std::size_t> used;
    used.reserve(key.Q.size());
    std::vector<model::Variable> reached{key.root_x};

    while (order.size() < key.Q.size()) {
        std::size_t next_index = key.Q.size();

        for (std::size_t index = 0; index < key.Q.size(); ++index) {
            if (used.contains(index)) continue;

            auto const& triple = key.Q[index];
            bool const subject_reached =
                    std::find(reached.begin(), reached.end(), triple.subject) != reached.end();
            bool const object_reached =
                    std::find(reached.begin(), reached.end(), triple.object) != reached.end();
            if (subject_reached && object_reached) {
                next_index = index;
                break;
            }
        }

        if (next_index == key.Q.size()) {
            for (std::size_t index = 0; index < key.Q.size(); ++index) {
                if (used.contains(index)) continue;
                auto const& triple = key.Q[index];
                if (std::find(reached.begin(), reached.end(), triple.subject) != reached.end() ||
                    std::find(reached.begin(), reached.end(), triple.object) != reached.end()) {
                    next_index = index;
                    break;
                }
            }
        }

        // The descriptor check has already established connectivity and nonempty Q.
        assert(next_index < key.Q.size());

        used.insert(next_index);
        order.push_back(next_index);

        auto const& triple = key.Q[next_index];
        if (std::find(reached.begin(), reached.end(), triple.subject) == reached.end()) {
            reached.push_back(triple.subject);
        }
        if (std::find(reached.begin(), reached.end(), triple.object) == reached.end()) {
            reached.push_back(triple.object);
        }
    }

    return order;
}

}  // namespace

void model::GraphKeyDescriptor::Check() const {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    bool const key_check_trace = util::logging::Initialize()->should_log(spdlog::level::trace);
    auto const key_check_started = key_check_trace ? std::chrono::steady_clock::now()
                                                   : std::chrono::steady_clock::time_point{};
#endif

    LOG_DEBUG("Validating GK key descriptor: {} pattern triples, {} type entries, root='{}'",
              Q.size(), T_Q.size(), root_x.name);

    std::vector<Variable> const variables = ValidatePattern(Q);
    if (root_x.name.empty()) ThrowInvalid("key root must not be empty");
    if (root_x.kind != VarKind::kEntity) {
        ThrowInvalid("key root must be an entity variable");
    }

    Variable const* root = nullptr;
    for (Variable const& variable : variables) {
        if (variable == root_x) {
            root = &variable;
            break;
        }
    }
    if (root == nullptr) ThrowInvalid("key root must be an entity variable in the pattern");

    std::unordered_set<Variable> typed_variables;
    for (Variable const& variable : variables) {
        bool const requires_type =
                variable.kind == VarKind::kEntity || variable.kind == VarKind::kWildcard;
        auto it = T_Q.find(variable);
        if (requires_type) {
            if (it == T_Q.end() || it->second.empty()) {
                ThrowInvalid("missing type for pattern variable " + variable.name);
            }
            typed_variables.insert(variable);
        }
    }

    for (auto const& [variable, type] : T_Q) {
        CheckVariable(variable, "key type map variable");
        if (type.empty() || type == "-") {
            ThrowInvalid("invalid type for pattern variable " + variable.name);
        }
        if (variable.kind != VarKind::kEntity && variable.kind != VarKind::kWildcard) {
            ThrowInvalid("type is provided for a non-entity pattern variable " + variable.name);
        }
        if (!typed_variables.contains(variable)) {
            ThrowInvalid("type is provided for a variable outside the pattern " + variable.name);
        }
    }

    std::vector<std::vector<std::size_t>> adjacency(variables.size());
    for (PatternTriple const& triple : Q) {
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
              root_x.name);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    if (key_check_trace)
        LOG_TRACE("GK phase 'key_validation': {} ns",
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - key_check_started)
                          .count());
#endif
}

GraphKey BuildGraphKey(model::GraphKeyDescriptor key) {
    key.Check();
    auto order = BuildTraversalOrder(key);
    return {std::move(key), std::move(order)};
}

}  // namespace gk
