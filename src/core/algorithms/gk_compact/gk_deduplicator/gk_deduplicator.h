#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "core/algorithms/algorithm.h"
#include "core/algorithms/gk/model/result_types.h"
#include "core/algorithms/gk_compact/gk_deduplicator/chase_engine.h"

namespace algos {

class CompactGkDeduplicator : public Algorithm {
public:
    CompactGkDeduplicator();

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
    std::shared_ptr<gk_compact::Graph const> graph_;
    std::filesystem::path graph_path_;
    std::vector<gk::model::GraphKeyDescriptor> source_keys_;
    std::optional<gk_compact::ChaseEngine> engine_;
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
