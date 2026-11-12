#include "python_bindings/gk_compact/bind_gk_compact.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include "core/algorithms/gk_compact/model/graph.h"
#include "core/algorithms/gk_compact/model/graph_builder.h"
#include "core/algorithms/gk_compact/optimizations/evaluation.h"
#include "core/algorithms/gk_compact/parser/graph_parser.h"
#include "core/config/exceptions.h"
#include "core/config/names.h"
#include "python_bindings/bind_main_classes.h"
#include "python_bindings/py_util/bind_primitive.h"

#if DESBORDANTE_GK_COMPACT_VALIDATION_AVAILABLE
#include "core/algorithms/gk_compact/gk_validator/gk_validator.h"
#endif
#if DESBORDANTE_GK_COMPACT_CHASE_AVAILABLE
#include "core/algorithms/gk_compact/gk_deduplicator/gk_deduplicator.h"
#endif

namespace {

namespace py = pybind11;
using namespace gk_compact;

// Python owns these inputs and the relation. No borrowed C++ view escapes a call.
struct PythonPairing {
    std::shared_ptr<Graph> first;
    std::shared_ptr<Graph> second;
    std::optional<GraphKey> key;
    std::vector<gk::model::Variable> variables;
    PairingRelation relation;

    PatternNodeId FindVariable(gk::model::Variable const& variable) const {
        auto const found = std::ranges::find(variables, variable);
        if (found == variables.end())
            throw std::invalid_argument("Variable does not belong to this pairing's key");
        return static_cast<PatternNodeId>(found - variables.begin());
    }
};

void BindOptimizations(py::module_& module) {
    auto optimizations =
            module.def_submodule("optimizations", "Standalone compact GK transformations");
    py::class_<PythonPairing>(optimizations, "PairingRelation")
            .def_property_readonly(
                    "pair_count",
                    [](PythonPairing const& result) { return result.relation.pairs.size(); })
            .def_property_readonly("variables",
                                   [](PythonPairing const& result) { return result.variables; })
            .def(
                    "candidate_count",
                    [](PythonPairing const& result, gk::model::Variable const& variable) {
                        auto const index = result.FindVariable(variable);
                        return result.key ? result.relation.Candidates(index).size()
                                          : std::size_t{0};
                    },
                    py::arg("variable"))
            .def(
                    "get_candidates",
                    [](PythonPairing const& result, gk::model::Variable const& variable) {
                        auto const index = result.FindVariable(variable);
                        std::vector<std::pair<gk::model::Node, gk::model::Node>> pairs;
                        py::gil_scoped_release release;
                        if (result.key) {
                            auto const candidates = result.relation.Candidates(index);
                            pairs.reserve(candidates.size());
                            for (auto const& pair : candidates)
                                pairs.emplace_back(GetNode(result.first->Data(), pair.first),
                                                   GetNode(result.second->Data(), pair.second));
                        }
                        return pairs;
                    },
                    py::arg("variable"),
                    "Copy candidate node pairs to Python; may allocate a large list.")
            .def(
                    "can_pair",
                    [](PythonPairing const& result, std::string const& first,
                       std::string const& second) {
                        if (!result.key) return false;
                        auto const lhs = result.first->FindEntity(first);
                        auto const rhs = result.second->FindEntity(second);
                        return lhs && rhs && result.first->ContainsVertex(*lhs) &&
                               result.second->ContainsVertex(*rhs) &&
                               result.relation.Contains(result.key->root, *lhs, *rhs);
                    },
                    py::arg("first_root"), py::arg("second_root"),
                    "Necessary pairability only; a positive result does not replace exact EvalGK.");

    optimizations.def(
            "localize",
            [](Graph const& graph, std::string const& root, std::size_t radius) {
                py::gil_scoped_release release;
                auto const id = graph.FindEntity(root);
                if (!id || !graph.ContainsVertex(*id))
                    throw std::invalid_argument(
                            "Localization root must be an active entity of the graph");
                std::array<VertexId, 1> const roots{*id};
                auto const neighborhoods = PrepareNeighborhoods(graph, roots, radius);
                return std::make_shared<Graph>(Materialize(neighborhoods[0]));
            },
            py::arg("graph").none(false), py::arg("root"), py::arg("radius"),
            "Materialize the induced undirected-radius neighborhood, preserving original IDs and "
            "metadata.");

    optimizations.def(
            "compute_pairing",
            [](gk::model::GraphKeyDescriptor const& descriptor, std::shared_ptr<Graph> first,
               std::shared_ptr<Graph> second) {
                py::gil_scoped_release release;
                if (first->Data().metadata != second->Data().metadata)
                    throw std::invalid_argument(
                            "Pairing inputs must share metadata; derive them from the same "
                            "CompactGraph");
                PythonPairing result{std::move(first), std::move(second), std::nullopt, {}, {}};
                result.key = BuildGraphKey(descriptor, *result.first);
                result.variables.push_back(descriptor.root_x);
                for (auto const& triple : descriptor.Q)
                    for (auto const* variable : {&triple.subject, &triple.object})
                        if (std::ranges::find(result.variables, *variable) ==
                            result.variables.end())
                            result.variables.push_back(*variable);
                if (result.key)
                    result.relation = ComputePairing(*result.key, *result.first, *result.second);
                return result;
            },
            py::arg("key"), py::arg("first").none(false), py::arg("second").none(false),
            "Compute Eq-independent pairing for graphs sharing one metadata domain.");

    optimizations.def(
            "reduce_subgraphs",
            [](PythonPairing const& result) {
                py::gil_scoped_release release;
                SubgraphReduction reduction;
                reduction.Compute(*result.first, *result.second, result.relation);
                return std::pair{std::make_shared<Graph>(
                                         Materialize(GraphView(*result.first, reduction.First()))),
                                 std::make_shared<Graph>(Materialize(
                                         GraphView(*result.second, reduction.Second())))};
            },
            py::arg("pairing"),
            "Materialize both induced projections of a pairing. This is pair-specific, not a "
            "global graph replacement.");
}

template <typename Algorithm, typename BoundClass>
void BindExecution(BoundClass& cls) {
    cls.def(
            "load_data",
            [](Algorithm& algorithm, py::kwargs const& kwargs) {
                using namespace config::names;
                auto const possible = algorithm.GetPossibleOptions();
                auto const needed = algorithm.GetNeededOptions();
                // ConfigureAlgo reads only needed options; do not silently drop
                // explicit input arguments after loading or partial configuration.
                for (auto const& argument : kwargs) {
                    auto const name = argument.first.cast<std::string>();
                    if ((name != kGkGraphData && name != kGkGraphPath && name != kGkKeyData &&
                         name != kGkKeysData) ||
                        !possible.contains(name))
                        throw config::ConfigurationError("Unknown GK load_data option '" + name +
                                                         "'");
                    if (!needed.contains(name))
                        throw config::ConfigurationError(
                                "GK load_data option '" + name +
                                "' is already configured or unavailable; create a new algorithm "
                                "instance to change the input");
                }
                configure_algorithm::ConfigureAlgo(algorithm, kwargs);
                py::gil_scoped_release release;
                algorithm.LoadData();
            },
            "Configure and load input. To change a configured graph or key, create a new "
            "algorithm instance. Call without arguments to reload the same input.");
    cls.def("execute", [](Algorithm& algorithm, py::kwargs const& kwargs) {
        using namespace config::names;
        try {
            configure_algorithm::ConfigureAlgo(algorithm, kwargs);
            // The shared configurator reads only active options. Reject an
            // explicit request to enable an inactive stage instead of ignoring it.
            for (auto const* name :
                 {kGkUseLocalization, kGkUsePairingFilter, kGkUseSubgraphReduction}) {
                if (kwargs.contains(name) && !algorithm.OptionIsSet(name) &&
                    kwargs[name].cast<bool>())
                    throw config::ConfigurationError(
                            std::string("Inactive option '") + name +
                            "': " + std::string(algorithm.GetDescription(name)));
            }
        } catch (...) {
            // Configuration can fail before ExecuteInternal gets control.
            algorithm.UnsetOption(kGkUseLocalization);
            throw;
        }
        py::gil_scoped_release release;
        algorithm.Execute();
    });
}

}  // namespace

namespace python_bindings {

void BindGkCompact(py::module_& main_module) {
    auto verification = main_module.attr("gk_verification").cast<py::module_>();
    auto deduplication = main_module.attr("gk_deduplication").cast<py::module_>();

    py::class_<Graph, std::shared_ptr<Graph>>(verification, "CompactGraph")
            .def_static(
                    "from_csv",
                    [](std::filesystem::path const& path) {
                        py::gil_scoped_release release;
                        return std::make_shared<Graph>(gk_compact::parser::ReadGraph(path));
                    },
                    py::arg("path"))
            .def_static(
                    "from_triples",
                    [](py::iterable const& triples) {
                        GraphBuilder builder;
                        for (auto triple : triples)
                            builder.AddTriple(triple.cast<gk::model::Triple>());
                        return std::make_shared<Graph>(std::move(builder).Build());
                    },
                    py::arg("triples"))
            .def_property_readonly("vertex_count", &Graph::GetVertexCount)
            .def_property_readonly("entity_count", &Graph::GetEntityCount)
            .def_property_readonly("edge_count", &Graph::GetEdgeCount)
            .def_property_readonly("input_triple_count", &Graph::GetInputTripleCount)
            .def(
                    "get_vertices",
                    [](Graph const& graph) {
                        py::gil_scoped_release release;
                        std::vector<gk::model::Node> nodes;
                        nodes.reserve(graph.GetVertexCount());
                        for (auto vertex : graph.VertexIds())
                            nodes.push_back(GetNode(graph.Data(), vertex));
                        return nodes;
                    },
                    "Copy active vertices, including values, to Python.")
            .def("__repr__", [](Graph const& graph) {
                std::ostringstream out;
                out << "CompactGraph(vertices=" << graph.GetVertexCount()
                    << ", entities=" << graph.GetEntityCount() << ", edges=" << graph.GetEdgeCount()
                    << ", input_triples=" << graph.GetInputTripleCount() << ')';
                return out.str();
            });
    deduplication.attr("CompactGraph") = verification.attr("CompactGraph");
    BindOptimizations(verification);
    deduplication.attr("optimizations") = verification.attr("optimizations");
    verification.attr("compact_validator_available") =
            bool(DESBORDANTE_GK_COMPACT_VALIDATION_AVAILABLE);
    deduplication.attr("compact_deduplicator_available") =
            bool(DESBORDANTE_GK_COMPACT_CHASE_AVAILABLE);

    // Register in the existing algorithms modules. BindPrimitiveNoBase would
    // unintentionally replace their Default algorithms.
#if DESBORDANTE_GK_COMPACT_VALIDATION_AVAILABLE
    auto verification_algorithms = verification.attr("algorithms").cast<py::module_>();
    auto validator = detail::RegisterAlgorithm<algos::CompactGkValidator, algos::Algorithm>(
            verification_algorithms, "CompactGkValidator");
    BindExecution<algos::CompactGkValidator>(validator);
    validator.def_property_readonly("holds", &algos::CompactGkValidator::Holds)
            .def("get_violations", [](algos::CompactGkValidator const& algorithm) {
                return algorithm.GetViolations();
            });
#endif
#if DESBORDANTE_GK_COMPACT_CHASE_AVAILABLE
    auto dedup_algorithms = deduplication.attr("algorithms").cast<py::module_>();
    auto deduplicator = detail::RegisterAlgorithm<algos::CompactGkDeduplicator, algos::Algorithm>(
            dedup_algorithms, "CompactGkDeduplicator");
    BindExecution<algos::CompactGkDeduplicator>(deduplicator);
    deduplicator
            .def("get_clusters",
                 [](algos::CompactGkDeduplicator const& algorithm) {
                     return algorithm.GetClusters();
                 })
            .def_property_readonly("cluster_count", &algos::CompactGkDeduplicator::GetClusterCount)
            .def_property_readonly("merge_count", &algos::CompactGkDeduplicator::GetMergeCount);
#endif
}

}  // namespace python_bindings
