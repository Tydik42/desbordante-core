#include "logging.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <unordered_map>

#include <pybind11/detail/common.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#ifdef PYBIND11_HAS_SUBINTERPRETER_SUPPORT
#include <pybind11/subinterpreter.h>
#endif

#include <spdlog/details/null_mutex.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include "util/logger.h"

namespace {
namespace py = pybind11;

// A global flag set by the Python atexit handler to signal that the
// interpreter is shutting down. The logging sink checks this flag to prevent
// unsafe calls back into the Python C-API during this critical phase.
std::atomic<bool> g_main_interpreter_shutting_down{false};

constexpr int kPyTraceLevel = 5;
using InterpreterId = int64_t;

InterpreterId GetCurrentInterpreterId() {
#ifdef PYBIND11_HAS_SUBINTERPRETER_SUPPORT
    return py::subinterpreter::current().id();
#else
    return 0;
#endif
}

template <typename Mutex>
class MultiTenantPythonSink : public spdlog::sinks::base_sink<Mutex> {
public:
    MultiTenantPythonSink() = default;

    void RegisterLogger(py::object logger) {
        if (g_main_interpreter_shutting_down.load(std::memory_order_relaxed)) {
            return;
        }

        py::gil_scoped_acquire gil;
        InterpreterId id = GetCurrentInterpreterId();
        if (id < 0) return;

        std::unique_lock lock(registry_mutex_);
        interpreters_loggers_.try_emplace(id, std::move(logger));
    }

    void UnregisterLogger() {
        py::gil_scoped_acquire gil;
        InterpreterId id = GetCurrentInterpreterId();
        if (id < 0) return;

        std::unique_lock lock(registry_mutex_);
        interpreters_loggers_.erase(id);
    }

protected:
    void sink_it_(spdlog::details::log_msg const& msg) override {
        if (g_main_interpreter_shutting_down.load(std::memory_order_relaxed)) {
            return;
        }

        py::gil_scoped_acquire gil;

        InterpreterId id = GetCurrentInterpreterId();
        if (id < 0) return;

        py::object target_logger;
        {
            std::shared_lock lock(registry_mutex_);
            auto it = interpreters_loggers_.find(id);
            if (it == interpreters_loggers_.end()) {
                return;
            }
            target_logger = it->second;
        }

        int msg_py_level;
        switch (msg.level) {
            case spdlog::level::trace:
                msg_py_level = kPyTraceLevel;
                break;
            case spdlog::level::debug:
                msg_py_level = 10;
                break;
            case spdlog::level::info:
                msg_py_level = 20;
                break;
            case spdlog::level::warn:
                msg_py_level = 30;
                break;
            case spdlog::level::err:
                msg_py_level = 40;
                break;
            case spdlog::level::critical:
                msg_py_level = 50;
                break;
            default:
                msg_py_level = 0;
                break;
        }

        if (!target_logger.attr("isEnabledFor")(msg_py_level).cast<bool>()) {
            return;
        }
        std::string_view payload(msg.payload.data(), msg.payload.size());
        (void)target_logger.attr("log")(msg_py_level, payload);
    }

    void flush_() override {}

private:
    std::shared_mutex registry_mutex_;
    std::unordered_map<InterpreterId, py::object> interpreters_loggers_;
};

using PythonSink = MultiTenantPythonSink<spdlog::details::null_mutex>;

std::shared_ptr<PythonSink> GetGlobalPythonSink() {
    static std::shared_ptr<PythonSink> sink = std::make_shared<PythonSink>();
    return sink;
}

void SetupLoggingBridge() {
    py::gil_scoped_acquire gil;
    try {
        py::module_ logging = py::module_::import("logging");

        if (!py::hasattr(logging, "TRACE")) {
            logging.attr("addLevelName")(kPyTraceLevel, "TRACE");
            logging.add_object("TRACE", py::int_(kPyTraceLevel));
        }

        py::object py_logger = logging.attr("getLogger")("desbordante");
        if (py::len(py_logger.attr("handlers")) == 0) {
            py::object handler = logging.attr("NullHandler")();
            py_logger.attr("addHandler")(handler);
        }

        auto python_sink = GetGlobalPythonSink();
        python_sink->RegisterLogger(std::move(py_logger));

        ::util::logging::EnsureInitialized("desbordante", {std::move(python_sink)});

    } catch (py::error_already_set const& e) {
        py::print("ERROR: Error during Python logging setup:", e.what());
    }
}

void CleanupAtExit() {
#ifdef PYBIND11_HAS_SUBINTERPRETER_SUPPORT
    if (py::subinterpreter::current().id() == py::subinterpreter::main().id()) {
        g_main_interpreter_shutting_down.store(true, std::memory_order_relaxed);
    }
#else
    g_main_interpreter_shutting_down.store(true, std::memory_order_relaxed);
#endif
    if (auto sink = GetGlobalPythonSink()) {
        sink->UnregisterLogger();
    }
}

}  // namespace

namespace python_bindings {

void BindLogging(py::module_&) {
    SetupLoggingBridge();
    py::module_::import("atexit").attr("register")(py::cpp_function(CleanupAtExit));
}

}  // namespace python_bindings
