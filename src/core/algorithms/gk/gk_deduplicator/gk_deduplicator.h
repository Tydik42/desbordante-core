#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include "core/algorithms/algorithm.h"
#include "core/algorithms/gk/gk_deduplicator/chase_engine.h"

namespace gk {

struct DeduplicationTimingStats {
    std::uint64_t load_source_ns = 0;
    std::uint64_t load_graph_check_ns = 0;
    std::uint64_t load_keys_check_ns = 0;
    std::uint64_t load_total_ns = 0;
    std::uint64_t execute_total_ns = 0;
    std::size_t graph_triple_count = 0;
    std::size_t key_count = 0;
    ChaseStatistics execution;
};

}  // namespace gk

namespace algos {

// Desbordante adapter. ChaseEngine owns the fixed-point computation; the adapter owns
// options, graph loading, validation and results, just as GkValidator does.
class GkDeduplicator : public Algorithm {
public:
    GkDeduplicator();

    gk::EquivalenceRelation::Classes const& GetClusters() const noexcept {
        return result_.clusters;
    }

    std::size_t GetClusterCount() const noexcept {
        return result_.clusters.size();
    }

    std::size_t GetMergeCount() const noexcept {
        return result_.statistics.merge_count;
    }

    gk::DeduplicationTimingStats const& GetTimingStats() const noexcept {
        return timing_stats_;
    }

private:
    gk::model::Graph graph_;
    std::filesystem::path graph_path_;
    std::vector<gk::model::GraphKey> keys_;
    std::optional<gk::ChaseEngine> engine_;
    gk::ChaseResult result_;
    gk::DeduplicationTimingStats timing_stats_;
    bool use_pairing_filter_ = false;
    bool use_subgraph_reduction_ = false;

    void RegisterOptions();
    void MakeExecuteOptsAvailable() final;
    void LoadDataInternal() final;
    void ExecuteInternal() final;
    void ResetState() final;
};

}  // namespace algos
