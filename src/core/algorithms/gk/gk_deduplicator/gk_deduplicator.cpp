#include "core/algorithms/gk/gk_deduplicator/gk_deduplicator.h"

#include <stdexcept>

#include "core/algorithms/gk/model/validation.h"
#include "core/algorithms/gk/parser/graph_parser.h"
#include "core/config/gk/option.h"
#include "core/config/names_and_descriptions.h"
#include "core/util/logger.h"

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
#include <chrono>
#endif

namespace algos {

GkDeduplicator::GkDeduplicator() {
    RegisterOptions();
    MakeOptionsAvailable(
            {config::names::kGkGraphData, config::names::kGkGraphPath, config::names::kGkKeysData});
}

void GkDeduplicator::RegisterOptions() {
    using namespace config::names;
    using namespace config::descriptions;
    RegisterOption(config::Option{&graph_, kGkGraphData, kDGkGraphData}.SetIsRequiredFunc(
            [this] { return !OptionIsSet(kGkGraphPath); }));
    RegisterOption(config::Option{&graph_path_, kGkGraphPath, kDGkGraphPath}.SetIsRequiredFunc(
            [this] { return !OptionIsSet(kGkGraphData); }));
    RegisterOption(config::Option{&source_keys_, kGkKeysData, kDGkKeysData});
    RegisterOption(config::kGkUseLocalizationOpt(&use_localization_)
                           .SetConditionalOpts({{[](bool enabled) { return enabled; },
                                                 {kGkUsePairingFilter}}}));
    RegisterOption(config::kGkUsePairingFilterOpt(&use_pairing_filter_)
                           .SetConditionalOpts({{[](bool enabled) { return enabled; },
                                                 {kGkUseSubgraphReduction}}}));
    RegisterOption(config::kGkUseSubgraphReductionOpt(&use_subgraph_reduction_));
}

void GkDeduplicator::MakeExecuteOptsAvailable() {
    MakeOptionsAvailable({config::names::kGkUseLocalization});
}

void GkDeduplicator::LoadDataInternal() {
    UnsetOption(config::names::kGkUseLocalization);
    engine_.reset();
    result_.reset();
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    bool const load_trace = util::logging::Initialize()->should_log(spdlog::level::trace);
    auto const load_started =
            load_trace ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
#endif
    bool const has_graph = OptionIsSet(config::names::kGkGraphData);
    bool const has_path = OptionIsSet(config::names::kGkGraphPath);
    if (has_graph == has_path)
        throw config::ConfigurationError("Exactly one of gk_graph and gk_graph_path is required");
    if (has_path)
        graph_ = gk::parser::ReadGraph(graph_path_);
    else
        gk::CheckGraph(graph_);
    engine_.emplace(source_keys_);
    LOG_DEBUG("Loaded GK deduplication data: {} triples, {} keys", graph_.size(),
              source_keys_.size());
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    if (load_trace)
        LOG_TRACE("GK phase 'load': {} ns", std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                    std::chrono::steady_clock::now() - load_started)
                                                    .count());
#endif
}

void GkDeduplicator::ResetState() {
    if (!OptionIsSet(config::names::kGkUsePairingFilter)) use_pairing_filter_ = false;
    if (!OptionIsSet(config::names::kGkUseSubgraphReduction)) use_subgraph_reduction_ = false;
    result_.reset();
}

void GkDeduplicator::ExecuteInternal() {
    try {
        if (!engine_)
            throw std::logic_error("GK deduplication data has not been loaded successfully");
        config::CheckGkOptimizationDependencies(use_localization_, use_pairing_filter_,
                                                use_subgraph_reduction_);
        LOG_INFO(
                "Starting GK deduplication: localization={}, pairing_filter={}, "
                "subgraph_reduction={}",
                use_localization_, use_pairing_filter_, use_subgraph_reduction_);
        auto run = engine_->Run(graph_, use_localization_, use_pairing_filter_,
                                use_subgraph_reduction_);
        result_ = gk::DeduplicationResult{std::move(run.clusters), run.merge_count};
        LOG_INFO("GK deduplication finished: {} classes, {} merges", result_->clusters.size(),
                 result_->merge_count);
    } catch (...) {
        result_.reset();
        UnsetOption(config::names::kGkUseLocalization);
        throw;
    }
}

gk::DeduplicationResult const& GkDeduplicator::GetResult() const {
    if (!result_) throw std::logic_error("GK deduplication has no successful execution result");
    return *result_;
}

}  // namespace algos
