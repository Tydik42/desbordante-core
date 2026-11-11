#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include "core/algorithms/algorithm.h"
#include "core/algorithms/gk/model/key.h"
#include "core/algorithms/gk/model/result_types.h"

namespace algos {

class GkValidator : public Algorithm {
public:
    GkValidator();

    bool Holds() const {
        return GetResult().holds;
    }

    std::vector<gk::EntityPair> const& GetViolations() const {
        return GetResult().violations;
    }

    gk::ValidationResult const& GetResult() const;

private:
    gk::model::Graph graph_;
    std::filesystem::path graph_path_;
    gk::model::GraphKeyDescriptor source_key_;
    std::optional<gk::GraphKey> key_;
    std::optional<gk::ValidationResult> result_;
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
