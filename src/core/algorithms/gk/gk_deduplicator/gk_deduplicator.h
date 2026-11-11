#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include "core/algorithms/algorithm.h"
#include "core/algorithms/gk/gk_deduplicator/chase_engine.h"
#include "core/algorithms/gk/model/result_types.h"

namespace algos {

class GkDeduplicator : public Algorithm {
public:
    GkDeduplicator();

    std::vector<std::vector<std::string>> const& GetClusters() const {
        return GetResult().clusters;
    }

    std::size_t GetClusterCount() const {
        return GetResult().clusters.size();
    }

    std::size_t GetMergeCount() const {
        return GetResult().merge_count;
    }

    gk::DeduplicationResult const& GetResult() const;

private:
    gk::model::Graph graph_;
    std::filesystem::path graph_path_;
    std::vector<gk::model::GraphKey> source_keys_;
    std::optional<gk::ChaseEngine> engine_;
    std::optional<gk::DeduplicationResult> result_;
    bool use_localization_ = false;
    bool use_pairing_filter_ = false;
    bool use_subgraph_reduction_ = false;

    void RegisterOptions();
    void MakeExecuteOptsAvailable() final;
    void LoadDataInternal() final;
    void ExecuteInternal() final;
    void ResetState() final;
};

}  // namespace algos
