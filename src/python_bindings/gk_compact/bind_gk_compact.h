#pragma once

#include <pybind11/pybind11.h>

namespace python_bindings {

// Call after BindGk: model types and primitive modules are shared with the baseline.
void BindGkCompact(pybind11::module_& main_module);

}  // namespace python_bindings
