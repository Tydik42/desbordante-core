#include "logging.h"

#include <spdlog/spdlog.h>
#include <pybind11/pybind11.h>

#include <unordered_map>

namespace {
namespace py = pybind11;
}  // namespace

namespace python_bindings {
void BindLogging(py::module_& main_module) {
    auto logging_module = main_module.def_submodule("logging");

    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    spdlog::set_level(spdlog::level::off);  // Default value

    auto set_log_level = [](const std::string& level) {
        static const std::unordered_map<std::string, spdlog::level::level_enum> kLevels = {
            {"trace", spdlog::level::trace},
            {"debug", spdlog::level::debug},
            {"info", spdlog::level::info},
            {"warn", spdlog::level::warn},
            {"error", spdlog::level::err},
            {"off", spdlog::level::off}
        };

        if (const auto it = kLevels.find(level); it != kLevels.end()) {
            spdlog::set_level(it->second);
        } else {
            throw py::value_error(
                "Invalid log level. Available options: "
                "'trace', 'debug', 'info', 'warn', 'error', 'off'"
            );
        }
    };

    logging_module.def("set_level", set_log_level, py::arg("level"),
        R"(Set the logging verbosity level.

        Args:
            level (str): Logging level. Available options:
                - 'trace'
                - 'debug'
                - 'info'
                - 'warn'
                - 'error'
                - 'off'      - default
        )");

    main_module.attr("logging") = logging_module;
}

}  // namespace python_bindings