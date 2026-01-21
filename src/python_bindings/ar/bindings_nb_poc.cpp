#include <nanobind/nanobind.h>

#include "python_bindings/ar/bind_ar_nanobind.h"

namespace nb = nanobind;

NB_MODULE(desbordante_ar_poc, m) {
    m.doc() = "Desbordante AR module PoC with nanobind";
    python_bindings::BindArNanobind(m);
}
