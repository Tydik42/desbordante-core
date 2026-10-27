#pragma once

#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

#include "core/algorithms/algorithm.h"
#include "core/algorithms/gk/evaluation_options.h"
#include "core/algorithms/gk/graph_descriptor.h"

namespace gk {

using EntityPair = std::pair<model::EntityId, model::EntityId>;

struct ValidationResult {
    bool holds = true;
    std::vector<EntityPair> violations;
};

// Diagnostics for the last load and/or execution.  Durations are monotonic-clock nanoseconds;
// they are intended for profiling the PoC, not as a public wall-clock contract.
struct ValidationTimingStats {
    std::uint64_t load_source_ns = 0;
    std::uint64_t load_graph_check_ns = 0;
    std::uint64_t load_key_check_ns = 0;
    std::uint64_t load_total_ns = 0;

    std::uint64_t execute_prepare_ns = 0;
    std::uint64_t execute_eval_ns = 0;
    std::uint64_t execute_finalize_ns = 0;
    std::uint64_t execute_total_ns = 0;

    std::size_t graph_triple_count = 0;
    std::size_t graph_entity_count = 0;
    std::size_t root_candidate_count = 0;
    std::size_t root_pair_count = 0;
    std::size_t checked_pair_count = 0;
    std::size_t violation_count = 0;
    EvaluationStatistics evaluation;
};

}  // namespace gk

namespace algos {

// Desbordante adapter for the GK validation algorithm.
//
// The class owns the algorithm lifecycle, input options, descriptor checks, and the result.  The
// semantic part of validation is deliberately outside this class: ExecuteInternal is expected to
// create/use EvalGK and store its result here.
class GkValidator : public Algorithm {
private:
    gk::model::Graph graph_;
    std::filesystem::path graph_path_;
    gk::model::GraphKey key_;
    gk::ValidationResult result_{false, {}};
    gk::ValidationTimingStats timing_stats_;
    bool use_pairing_filter_ = false;
    bool use_subgraph_reduction_ = false;

    void RegisterOptions();
    void MakeExecuteOptsAvailable() final;
    void LoadDataInternal() final;
    void ExecuteInternal() final;
    void ResetState() final;

public:
    GkValidator();

    bool Holds() const noexcept {
        return result_.holds;
    }

    std::vector<gk::EntityPair> const& GetViolations() const noexcept {
        return result_.violations;
    }

    gk::ValidationResult const& GetResult() const noexcept {
        return result_;
    }

    gk::ValidationTimingStats const& GetTimingStats() const noexcept {
        return timing_stats_;
    }
};

}  // namespace algos
