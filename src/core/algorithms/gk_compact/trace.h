#pragma once

#include <type_traits>
#include <utility>

#include "core/util/logger.h"

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
#include <chrono>
#endif

namespace gk_compact {

// Time a caller-owned phase, not individual search steps. Non-TRACE builds
// compile directly to action(); runtime-disabled TRACE also skips the clocks.
template <typename Action>
auto TracePhase(char const* phase, Action&& action) {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    if (!util::logging::Initialize()->should_log(spdlog::level::trace))
        return std::forward<Action>(action)();
    auto const started = std::chrono::steady_clock::now();
    auto log_elapsed = [&] {
        auto const elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now() - started)
                                     .count();
        LOG_TRACE("Compact GK phase '{}': {} ns", phase, elapsed);
    };
    if constexpr (std::is_void_v<std::invoke_result_t<Action>>) {
        std::forward<Action>(action)();
        log_elapsed();
    } else {
        auto result = std::forward<Action>(action)();
        log_elapsed();
        return result;
    }
#else
    (void)phase;
    return std::forward<Action>(action)();
#endif
}

}  // namespace gk_compact
