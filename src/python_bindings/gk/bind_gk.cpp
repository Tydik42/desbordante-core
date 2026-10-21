#include "python_bindings/gk/bind_gk.h"

#include <pybind11/pybind11.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <magic_enum/magic_enum.hpp>
#include <pybind11/stl.h>

#include "core/algorithms/gk/gk_deduplicator/gk_deduplicator.h"
#include "core/algorithms/gk/gk_validator/gk_validator.h"
#include "core/algorithms/gk/graph_descriptor.h"
#include "python_bindings/py_util/bind_primitive.h"

namespace {

namespace py = pybind11;
using gk::model::GraphKey;
using gk::model::Pattern;
using gk::model::PatternTriple;
using gk::model::TypeId;
using gk::model::Variable;

template <typename Enum>
std::string EnumName(Enum value) {
    return std::string(magic_enum::enum_name(value));
}

std::string FormatVariable(Variable const& variable) {
    std::ostringstream stream;
    stream << "Variable(kind=" << EnumName(variable.kind) << ", name=" << std::quoted(variable.name)
           << ')';
    return stream.str();
}

std::string FormatNode(gk::model::Node const& node) {
    std::ostringstream stream;
    stream << "Node(kind=" << EnumName(node.kind) << ", id_or_val=" << std::quoted(node.id_or_val)
           << ", type=" << std::quoted(node.type) << ')';
    return stream.str();
}

std::string FormatTriple(gk::model::Triple const& triple) {
    std::ostringstream stream;
    stream << "Triple(subject=" << FormatNode(triple.subject)
           << ", predicate=" << std::quoted(triple.predicate)
           << ", object=" << FormatNode(triple.object) << ')';
    return stream.str();
}

std::string FormatPatternTriple(PatternTriple const& triple) {
    std::ostringstream stream;
    stream << "PatternTriple(subject=" << FormatVariable(triple.subject)
           << ", predicate=" << std::quoted(triple.predicate)
           << ", object=" << FormatVariable(triple.object) << ')';
    return stream.str();
}

bool VariableLess(Variable const& lhs, Variable const& rhs) {
    int const lhs_kind = static_cast<int>(lhs.kind);
    int const rhs_kind = static_cast<int>(rhs.kind);
    if (lhs_kind != rhs_kind) return lhs_kind < rhs_kind;
    return lhs.name < rhs.name;
}

bool PatternTripleLess(PatternTriple const& lhs, PatternTriple const& rhs) {
    if (lhs.subject != rhs.subject) return VariableLess(lhs.subject, rhs.subject);
    if (lhs.predicate != rhs.predicate) return lhs.predicate < rhs.predicate;
    if (lhs.object != rhs.object) return VariableLess(lhs.object, rhs.object);
    return false;
}

void AppendField(std::string& result, std::string_view field) {
    result += std::to_string(field.size());
    result.push_back(':');
    result.append(field);
}

void AppendVariable(std::string& result, Variable const& variable) {
    result += std::to_string(static_cast<int>(variable.kind));
    result.push_back(':');
    AppendField(result, variable.name);
}

std::string CanonicalGraphKey(GraphKey const& key) {
    std::string result;

    result += "root";
    AppendVariable(result, key.root_x);

    std::vector<PatternTriple> pattern = key.Q;
    std::sort(pattern.begin(), pattern.end(), PatternTripleLess);
    result += "pattern";
    for (PatternTriple const& triple : pattern) {
        AppendVariable(result, triple.subject);
        AppendField(result, triple.predicate);
        AppendVariable(result, triple.object);
    }

    std::vector<std::pair<Variable, TypeId>> types(key.T_Q.begin(), key.T_Q.end());
    std::sort(types.begin(), types.end(),
              [](auto const& lhs, auto const& rhs) { return VariableLess(lhs.first, rhs.first); });
    result += "types";
    for (auto const& [variable, type] : types) {
        AppendVariable(result, variable);
        AppendField(result, type);
    }

    return result;
}

std::string FormatGraphKey(GraphKey const& key) {
    std::vector<PatternTriple> pattern = key.Q;
    std::sort(pattern.begin(), pattern.end(), PatternTripleLess);

    std::vector<std::pair<Variable, TypeId>> types(key.T_Q.begin(), key.T_Q.end());
    std::sort(types.begin(), types.end(),
              [](auto const& lhs, auto const& rhs) { return VariableLess(lhs.first, rhs.first); });

    std::ostringstream stream;
    stream << "GraphKey(root=" << FormatVariable(key.root_x) << ", pattern=[";
    for (std::size_t index = 0; index < pattern.size(); ++index) {
        if (index != 0) stream << ", ";
        stream << FormatPatternTriple(pattern[index]);
    }
    stream << "], types={";
    for (std::size_t index = 0; index < types.size(); ++index) {
        if (index != 0) stream << ", ";
        stream << FormatVariable(types[index].first) << ": " << std::quoted(types[index].second);
    }
    stream << "})";
    return stream.str();
}

py::tuple SerializeVariable(Variable const& variable) {
    return py::make_tuple(static_cast<int>(variable.kind), variable.name);
}

Variable DeserializeVariable(py::handle state) {
    py::tuple variable_state = state.cast<py::tuple>();
    if (variable_state.size() != 2) {
        throw std::runtime_error("Invalid variable state in GraphKey pickle.");
    }
    return Variable{static_cast<gk::model::VarKind>(variable_state[0].cast<int>()),
                    variable_state[1].cast<std::string>()};
}

py::tuple SerializeGraphKey(GraphKey const& key) {
    py::list pattern_state;
    for (PatternTriple const& triple : key.Q) {
        pattern_state.append(py::make_tuple(SerializeVariable(triple.subject), triple.predicate,
                                            SerializeVariable(triple.object)));
    }

    std::vector<std::pair<Variable, TypeId>> types(key.T_Q.begin(), key.T_Q.end());
    std::sort(types.begin(), types.end(),
              [](auto const& lhs, auto const& rhs) { return VariableLess(lhs.first, rhs.first); });

    py::list type_state;
    for (auto const& [variable, type] : types) {
        type_state.append(py::make_tuple(SerializeVariable(variable), type));
    }

    return py::make_tuple(1, pattern_state, type_state, SerializeVariable(key.root_x));
}

GraphKey DeserializeGraphKey(py::tuple state) {
    if (state.size() != 4) {
        throw std::runtime_error("Invalid state for GraphKey pickle.");
    }
    if (state[0].cast<int>() != 1) {
        throw std::runtime_error("Unsupported GraphKey pickle version.");
    }

    py::list pattern_state = state[1].cast<py::list>();
    Pattern pattern;
    pattern.reserve(pattern_state.size());
    for (py::handle item : pattern_state) {
        py::tuple triple_state = item.cast<py::tuple>();
        if (triple_state.size() != 3) {
            throw std::runtime_error("Invalid pattern state in GraphKey pickle.");
        }
        pattern.push_back(PatternTriple{DeserializeVariable(triple_state[0]),
                                        triple_state[1].cast<std::string>(),
                                        DeserializeVariable(triple_state[2])});
    }

    py::list type_state = state[2].cast<py::list>();
    std::unordered_map<Variable, TypeId> types;
    for (py::handle item : type_state) {
        py::tuple entry = item.cast<py::tuple>();
        if (entry.size() != 2) {
            throw std::runtime_error("Invalid type map state in GraphKey pickle.");
        }
        types.emplace(DeserializeVariable(entry[0]), entry[1].cast<TypeId>());
    }

    return GraphKey{std::move(pattern), std::move(types), DeserializeVariable(state[3])};
}

}  // namespace

namespace python_bindings {

void BindGk(pybind11::module_& main_module) {
    using namespace pybind11::literals;
    using namespace gk::model;

    auto gk_module = main_module.def_submodule("gk_verification");

    py::enum_<NodeKind>(gk_module, "NodeKind")
            .value("ENTITY", NodeKind::kEntity)
            .value("VALUE", NodeKind::kValue);

    py::enum_<VarKind>(gk_module, "VarKind")
            .value("ENTITY", VarKind::kEntity)
            .value("VALUE", VarKind::kValue)
            .value("CONSTANT", VarKind::kConstant)
            .value("WILDCARD", VarKind::kWildcard);

    py::class_<Node>(gk_module, "Node")
            .def(py::init([](NodeKind kind, EntityId id_or_val) {
                     return Node{kind, std::move(id_or_val), {}};
                 }),
                 "kind"_a, "id_or_val"_a)
            .def(py::init([](NodeKind kind, EntityId id_or_val, TypeId type) {
                     return Node{kind, std::move(id_or_val), std::move(type)};
                 }),
                 "kind"_a, "id_or_val"_a, "type"_a)
            .def_readonly("kind", &Node::kind)
            .def_readonly("id_or_val", &Node::id_or_val)
            .def_readonly("type", &Node::type)
            .def("__repr__", &FormatNode)
            .def("__eq__", [](Node const& lhs, Node const& rhs) { return lhs == rhs; });

    py::class_<Variable>(gk_module, "Variable")
            .def(py::init([](VarKind kind, std::string name) {
                     return Variable{kind, std::move(name)};
                 }),
                 "kind"_a, "name"_a)
            .def_readonly("kind", &Variable::kind)
            .def_readonly("name", &Variable::name)
            .def("__repr__", &FormatVariable)
            .def("__eq__", [](Variable const& lhs, Variable const& rhs) { return lhs == rhs; })
            .def("__hash__",
                 [](Variable const& variable) { return std::hash<Variable>{}(variable); });

    py::class_<Triple>(gk_module, "Triple")
            .def(py::init([](Node subject, PredicateId predicate, Node object) {
                     return Triple{std::move(subject), std::move(predicate), std::move(object)};
                 }),
                 "subject"_a, "predicate"_a, "object"_a)
            .def_readonly("subject", &Triple::subject)
            .def_readonly("predicate", &Triple::predicate)
            .def_readonly("object", &Triple::object)
            .def("__repr__", &FormatTriple)
            .def("__eq__", [](Triple const& lhs, Triple const& rhs) {
                return lhs.subject == rhs.subject && lhs.predicate == rhs.predicate &&
                       lhs.object == rhs.object;
            });

    py::class_<PatternTriple>(gk_module, "PatternTriple")
            .def(py::init([](Variable subject, PredicateId predicate, Variable object) {
                     return PatternTriple{std::move(subject), std::move(predicate),
                                          std::move(object)};
                 }),
                 "subject"_a, "predicate"_a, "object"_a)
            .def_readonly("subject", &PatternTriple::subject)
            .def_readonly("predicate", &PatternTriple::predicate)
            .def_readonly("object", &PatternTriple::object)
            .def("__repr__", &FormatPatternTriple)
            .def("__eq__", [](PatternTriple const& lhs, PatternTriple const& rhs) {
                return lhs.subject == rhs.subject && lhs.predicate == rhs.predicate &&
                       lhs.object == rhs.object;
            });

    py::class_<GraphKey> graph_key(gk_module, "GraphKey");
    graph_key
            .def(py::init([](Pattern pattern, std::unordered_map<Variable, TypeId> types,
                             Variable root) {
                     return GraphKey{std::move(pattern), std::move(types), std::move(root)};
                 }),
                 "pattern"_a, "types"_a, "root"_a)
            .def_property_readonly("pattern", [](GraphKey const& key) { return key.Q; })
            .def_property_readonly("types", [](GraphKey const& key) { return key.T_Q; })
            .def_property_readonly("root", [](GraphKey const& key) { return key.root_x; })
            .def("to_string", &FormatGraphKey)
            .def("__str__", &FormatGraphKey)
            .def("__repr__", &FormatGraphKey)
            .def("__eq__",
                 [](GraphKey const& lhs, GraphKey const& rhs) {
                     return CanonicalGraphKey(lhs) == CanonicalGraphKey(rhs);
                 })
            .def("__hash__",
                 [](GraphKey const& key) {
                     return std::hash<std::string>{}(CanonicalGraphKey(key));
                 })
            .def(py::pickle(&SerializeGraphKey, &DeserializeGraphKey));

    py::class_<gk::ValidationTimingStats>(gk_module, "ValidationTimingStats")
            .def_readonly("load_source_ns", &gk::ValidationTimingStats::load_source_ns)
            .def_readonly("load_graph_check_ns", &gk::ValidationTimingStats::load_graph_check_ns)
            .def_readonly("load_key_check_ns", &gk::ValidationTimingStats::load_key_check_ns)
            .def_readonly("load_total_ns", &gk::ValidationTimingStats::load_total_ns)
            .def_readonly("execute_prepare_ns", &gk::ValidationTimingStats::execute_prepare_ns)
            .def_readonly("execute_eval_ns", &gk::ValidationTimingStats::execute_eval_ns)
            .def_readonly("execute_finalize_ns", &gk::ValidationTimingStats::execute_finalize_ns)
            .def_readonly("execute_total_ns", &gk::ValidationTimingStats::execute_total_ns)
            .def_readonly("graph_triple_count", &gk::ValidationTimingStats::graph_triple_count)
            .def_readonly("graph_entity_count", &gk::ValidationTimingStats::graph_entity_count)
            .def_readonly("root_candidate_count", &gk::ValidationTimingStats::root_candidate_count)
            .def_readonly("root_pair_count", &gk::ValidationTimingStats::root_pair_count)
            .def_readonly("checked_pair_count", &gk::ValidationTimingStats::checked_pair_count)
            .def_readonly("violation_count", &gk::ValidationTimingStats::violation_count);

    BindPrimitiveNoBase<algos::GkValidator>(gk_module, "GkValidator")
            .def_property_readonly("holds", &algos::GkValidator::Holds)
            .def("get_violations",
                 [](algos::GkValidator const& validator) { return validator.GetViolations(); })
            .def_property_readonly("timing_stats", [](algos::GkValidator const& validator) {
                return validator.GetTimingStats();
            });

    auto dedup_module = main_module.def_submodule("gk_deduplication");
    // Both algorithms accept the exact same descriptors, not separately registered copies.
    for (auto const* name :
         {"NodeKind", "VarKind", "Node", "Variable", "Triple", "PatternTriple", "GraphKey"}) {
        dedup_module.attr(name) = gk_module.attr(name);
    }
    py::class_<gk::ChaseStatistics>(dedup_module, "ChaseStatistics")
            .def_readonly("prepare_ns", &gk::ChaseStatistics::prepare_ns)
            .def_readonly("eval_ns", &gk::ChaseStatistics::eval_ns)
            .def_readonly("finalize_ns", &gk::ChaseStatistics::finalize_ns)
            .def_readonly("total_ns", &gk::ChaseStatistics::total_ns)
            .def_readonly("entity_count", &gk::ChaseStatistics::entity_count)
            .def_readonly("root_pair_count", &gk::ChaseStatistics::root_pair_count)
            .def_readonly("checked_pair_count", &gk::ChaseStatistics::checked_pair_count)
            .def_readonly("eval_call_count", &gk::ChaseStatistics::eval_call_count)
            .def_readonly("merge_count", &gk::ChaseStatistics::merge_count)
            .def_readonly("pass_count", &gk::ChaseStatistics::pass_count);
    py::class_<gk::DeduplicationTimingStats>(dedup_module, "DeduplicationTimingStats")
            .def_readonly("load_source_ns", &gk::DeduplicationTimingStats::load_source_ns)
            .def_readonly("load_graph_check_ns", &gk::DeduplicationTimingStats::load_graph_check_ns)
            .def_readonly("load_keys_check_ns", &gk::DeduplicationTimingStats::load_keys_check_ns)
            .def_readonly("load_total_ns", &gk::DeduplicationTimingStats::load_total_ns)
            .def_readonly("execute_total_ns", &gk::DeduplicationTimingStats::execute_total_ns)
            .def_readonly("graph_triple_count", &gk::DeduplicationTimingStats::graph_triple_count)
            .def_readonly("key_count", &gk::DeduplicationTimingStats::key_count)
            .def_readonly("execution", &gk::DeduplicationTimingStats::execution);
    BindPrimitiveNoBase<algos::GkDeduplicator>(dedup_module, "GkDeduplicator")
            .def("get_clusters",
                 [](algos::GkDeduplicator const& deduplicator) {
                     return deduplicator.GetClusters();
                 })
            .def_property_readonly("cluster_count", &algos::GkDeduplicator::GetClusterCount)
            .def_property_readonly("merge_count", &algos::GkDeduplicator::GetMergeCount)
            .def_property_readonly("timing_stats", [](algos::GkDeduplicator const& deduplicator) {
                return deduplicator.GetTimingStats();
            });
}

}  // namespace python_bindings
