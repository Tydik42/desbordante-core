#pragma once

#include <cstddef>
#include <cstdint>

namespace gk {

// Aggregated by preparation and pair-check callers, outside exact EvalGK.
// Durations are monotonic-clock nanoseconds.
struct EvaluationStatistics {
    std::uint64_t neighborhood_ns = 0;
    std::uint64_t pairing_ns = 0;
    std::uint64_t reduction_ns = 0;
    std::uint64_t search_ns = 0;
    std::size_t pairing_check_count = 0;
    std::size_t filtered_pair_count = 0;
    std::size_t search_call_count = 0;
    // Stored triples across cached root/key neighborhoods, counted once when
    // built. Duplicate input triples are retained by the string representation.
    std::size_t neighborhood_triple_count = 0;
    // Only surviving pairs with reduction enabled contribute here.
    std::size_t reduced_triple_count = 0;
};

}  // namespace gk
