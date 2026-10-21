#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/algorithms/gk/equivalence_relation.h"
#include "core/algorithms/gk/gk_validator/eval_gk.h"
#include "core/algorithms/gk/graph_descriptor.h"

namespace gk {

struct ChaseStatistics {
    std::uint64_t prepare_ns = 0;
    std::uint64_t eval_ns = 0;
    std::uint64_t finalize_ns = 0;
    std::uint64_t total_ns = 0;
    std::size_t entity_count = 0;
    std::size_t root_pair_count = 0;     // Potential pairs in one pass, not a materialized list.
    std::size_t checked_pair_count = 0;  // Non-equivalent pairs visited across all passes.
    std::size_t eval_call_count = 0;
    std::size_t merge_count = 0;
    std::size_t pass_count = 0;  // Includes the final pass without merges, if any.
};

struct ChaseResult {
    EquivalenceRelation::Classes clusters;  // Includes singleton entity classes, never values.
    ChaseStatistics statistics;
};

class ChaseEngine {
public:
    // Validate and own prepared keys. No reference to the caller's key container is retained.
    explicit ChaseEngine(std::vector<model::GraphKey> const& keys);

    // The graph must have passed CheckGraph. Every call starts from Eq_0 and leaves the
    // original graph intact; this object can be reused for independent executions.
    ChaseResult Run(model::Graph const& graph) const;

private:
    struct Rule {
        model::TypeId root_type;
        EvalGK evaluator;
    };

    std::vector<Rule> rules_;
};

}  // namespace gk
