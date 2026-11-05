#include "python_bindings/gk_compact/bind_gk_compact.h"

#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include "core/algorithms/gk_compact/model/graph.h"
#include "core/algorithms/gk_compact/model/graph_builder.h"
#include "core/algorithms/gk_compact/parser/graph_parser.h"
#include "core/config/exceptions.h"
#include "core/config/names.h"
#include "python_bindings/bind_main_classes.h"
#include "python_bindings/py_util/bind_primitive.h"

#if DESBORDANTE_GK_COMPACT_VALIDATION_AVAILABLE
#include "core/algorithms/gk_compact/gk_validator/gk_validator.h"
#endif

namespace {

namespace py = pybind11;
using namespace gk_compact;

template <typename Algorithm, typename BoundClass>
void BindExecution(BoundClass& cls) {
    cls.def("load_data", [](Algorithm& algorithm, py::kwargs const& kwargs) {
        configure_algorithm::ConfigureAlgo(algorithm, kwargs);
        py::gil_scoped_release release;
        algorithm.LoadData();
    });
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
            .def("__repr__", [](Graph const& graph) {
                std::ostringstream out;
                out << "CompactGraph(vertices=" << graph.GetVertexCount()
                    << ", entities=" << graph.GetEntityCount() << ", edges=" << graph.GetEdgeCount()
                    << ", input_triples=" << graph.GetInputTripleCount() << ')';
                return out.str();
            });
    verification.attr("compact_validator_available") =
            bool(DESBORDANTE_GK_COMPACT_VALIDATION_AVAILABLE);

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
}

}  // namespace python_bindings
