#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include "option_converter.h"

namespace python_bindings {
namespace nb = nanobind;

template <typename T>
T NbCast(std::string_view option_name, void* handle_ptr) {
    try {
        nb::handle h(static_cast<PyObject*>(handle_ptr));
        return nb::cast<T>(h);
    } catch (nb::cast_error const&) {
        throw std::runtime_error(std::string("Unable to cast for option \"") +
                               std::string(option_name) + "\"");
    }
}

template<typename T>
IConverter::ConvertFunc MakeBasicConverter() {
    return [](std::string_view name, void* handle) -> boost::any {
        return NbCast<T>(name, handle);
    };
}

inline void RegisterBasicTypes(ModuleConverter& conv) {
    conv.Register<bool>(MakeBasicConverter<bool>());
    conv.Register<int>(MakeBasicConverter<int>());
    conv.Register<unsigned int>(MakeBasicConverter<unsigned int>());
    conv.Register<double>(MakeBasicConverter<double>());
    conv.Register<std::string>(MakeBasicConverter<std::string>());
}

} // namespace python_bindings
