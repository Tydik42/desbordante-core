#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/list.h>

#include "core/algorithms/algorithm.h"
#include "core/algorithms/algo_factory.h"
#include "core/algorithms/association_rules/ar.h"
#include "core/algorithms/association_rules/mining_algorithms.h"
#include "core/algorithms/association_rules/ar_algorithm_enums.h"
#include "python_bindings/infrastructure/option_converter.h"
#include "python_bindings/infrastructure/nb_helpers.h"

namespace python_bindings {
namespace nb = nanobind;

inline ModuleConverter CreateArConverter() {
    ModuleConverter conv;
    
    RegisterBasicTypes(conv);
    
    conv.Register<algos::InputFormat>([](std::string_view name, void* handle) -> boost::any {
        auto str = NbCast<std::string>(name, handle);
        auto opt = algos::InputFormat::_from_string_nocase_nothrow(str.c_str());
        if (!opt) {
            throw std::runtime_error(std::string("Invalid InputFormat for \"") + 
                                   std::string(name) + "\"");
        }
        return *opt;
    });
    
    return conv;
}

inline void ConfigureAlgo(algos::Algorithm& algo, nb::kwargs const& kwargs, 
                         IConverter const& converter) {
    algos::ConfigureFromFunction(algo, [&](std::string_view option_name) -> boost::any {
        if (!kwargs.contains(option_name.data())) {
            return boost::any{};
        }
        auto type_idx = algo.GetTypeIndex(option_name);
        nb::handle h = kwargs[option_name.data()];
        return converter.Convert(option_name, type_idx, h.ptr());
    });
}

inline void BindArNanobind(nb::module_& main_module) {
    using namespace algos;
    using model::ArIDs;
    using model::ARStrings;
    
    auto ar_module = main_module.def_submodule("ar");
    
    static ModuleConverter ar_converter = CreateArConverter();
    
    nb::class_<ARStrings>(ar_module, "ARStrings")
        .def("__str__", &ARStrings::ToString)
        .def_ro("left", &ARStrings::left)
        .def_ro("right", &ARStrings::right)
        .def_ro("confidence", &ARStrings::confidence)
        .def_ro("support", &ARStrings::support);
    
    nb::class_<ArIDs>(ar_module, "ArIDs")
        .def_ro("left", &ArIDs::left)
        .def_ro("right", &ArIDs::right)
        .def_ro("confidence", &ArIDs::confidence)
        .def_ro("support", &ArIDs::support);
    
    nb::class_<Algorithm>(ar_module, "Algorithm")
        .def("load_data", [](Algorithm& algo, nb::kwargs kwargs) {
            ConfigureAlgo(algo, kwargs, ar_converter);
            algo.LoadData();
        })
        .def("execute", [](Algorithm& algo, nb::kwargs kwargs) {
            ConfigureAlgo(algo, kwargs, ar_converter);
            algo.Execute();
        })
        .def("get_possible_options", &Algorithm::GetPossibleOptions)
        .def("get_description", &Algorithm::GetDescription, nb::arg("option_name"));
    
    nb::class_<ARAlgorithm, Algorithm>(ar_module, "ArAlgorithm")
        .def("get_ars", &ARAlgorithm::GetArStringsList)
        .def("get_itemnames", &ARAlgorithm::GetItemNamesVector)
        .def("get_ar_ids", &ARAlgorithm::GetArIDsList);
    
    auto algos_module = ar_module.def_submodule("algorithms");
    nb::class_<Apriori, ARAlgorithm>(algos_module, "Apriori")
        .def(nb::init<>());
    
    algos_module.attr("Default") = nb::type<Apriori>();
}

} // namespace python_bindings
