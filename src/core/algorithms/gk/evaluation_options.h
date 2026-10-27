#pragma once

#include <cstddef>
#include <cstdint>

namespace gk {

struct EvaluationOptions {
    bool use_pairing_filter = false;
    // Reduction also computes P^Q, even when use_pairing_filter is false.
    bool use_subgraph_reduction = false;
};

// Aggregated across pair/key checks. Durations are monotonic-clock nanoseconds.
struct EvaluationStatistics {
    std::uint64_t neighborhood_ns = 0;
    std::uint64_t pairing_ns = 0;
    std::uint64_t reduction_ns = 0;
    std::uint64_t search_ns = 0;
    std::size_t pairing_check_count = 0;
    std::size_t filtered_pair_count = 0;
    std::size_t search_call_count = 0;
    // Sum over both neighborhoods, including repeated checks and duplicate input triples.
    std::size_t neighborhood_triple_count = 0;
    // Only surviving pairs with reduction enabled contribute here.
    std::size_t reduced_triple_count = 0;
};

}  // namespace gk
