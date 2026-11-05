#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "core/algorithms/algorithm.h"
#include "core/algorithms/gk/model/result_types.h"
#include "core/algorithms/gk_compact/model/graph.h"
#include "core/algorithms/gk_compact/model/key.h"

namespace algos {

class CompactGkValidator : public Algorithm {
public:
    CompactGkValidator();

    bool Holds() const {
        return GetResult().holds;
    }

    std::vector<gk::EntityPair> const& GetViolations() const {
        return GetResult().violations;
    }

    gk::ValidationResult const& GetResult() const;

private:
    std::shared_ptr<gk_compact::Graph const> graph_;
    std::filesystem::path graph_path_;
    gk::model::GraphKey source_key_;
    std::optional<gk_compact::CompiledKey> key_;
    std::optional<gk::ValidationResult> result_;
    // An absent compiled key also represents a valid load with no embeddings.
    bool data_ready_ = false;
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
