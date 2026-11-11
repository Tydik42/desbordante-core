#include "core/algorithms/gk_compact/gk_deduplicator/gk_deduplicator.h"

#include <stdexcept>
#include <utility>

#include "core/algorithms/gk_compact/model/key.h"
#include "core/algorithms/gk_compact/parser/graph_parser.h"
#include "core/config/gk/option.h"
#include "core/config/names_and_descriptions.h"
#include "core/util/logger.h"

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
#include <chrono>
#endif

namespace {

gk::DeduplicationResult Decode(gk_compact::Graph const& graph,
                               std::vector<std::vector<gk_compact::VertexId>> clusters,
                               std::size_t merges) {
    gk::DeduplicationResult result{{}, merges};
    result.clusters.reserve(clusters.size());
    for (auto const& cluster : clusters) {
        auto& decoded = result.clusters.emplace_back();
        decoded.reserve(cluster.size());
        for (auto vertex : cluster) {
            decoded.push_back(graph.GetLabel(vertex));
        }
    }
    return result;
}

}  // namespace

namespace algos {

CompactGkDeduplicator::CompactGkDeduplicator() {
    RegisterOptions();
    MakeOptionsAvailable(
            {config::names::kGkGraphData, config::names::kGkGraphPath, config::names::kGkKeysData});
}

void CompactGkDeduplicator::RegisterOptions() {
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

void CompactGkDeduplicator::MakeExecuteOptsAvailable() {
    MakeOptionsAvailable({config::names::kGkUseLocalization});
}

void CompactGkDeduplicator::LoadDataInternal() {
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
        graph_ = std::make_shared<gk_compact::Graph const>(
                gk_compact::parser::ReadGraph(graph_path_));
    if (!graph_) throw config::ConfigurationError("gk_graph must not be null");
    std::vector<gk_compact::GraphKey> keys;
    keys.reserve(source_keys_.size());
    for (auto const& key : source_keys_)
        if (auto graph_key = gk_compact::BuildGraphKey(key, *graph_))
            keys.push_back(std::move(*graph_key));
    engine_.emplace(std::move(keys));
    LOG_DEBUG("Loaded compact GK deduplication data: {} vertices, {} edges, {} keys",
              graph_->GetVertexCount(), graph_->GetEdgeCount(), source_keys_.size());
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    if (load_trace)
        LOG_TRACE("Compact GK phase 'load': {} ns",
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - load_started)
                          .count());
#endif
}

void CompactGkDeduplicator::ResetState() {
    if (!OptionIsSet(config::names::kGkUsePairingFilter)) use_pairing_filter_ = false;
    if (!OptionIsSet(config::names::kGkUseSubgraphReduction)) use_subgraph_reduction_ = false;
    result_.reset();
}

void CompactGkDeduplicator::ExecuteInternal() {
    try {
        if (!engine_)
            throw std::logic_error(
                    "Compact GK deduplication data has not been loaded successfully");
        config::CheckGkOptimizationDependencies(use_localization_, use_pairing_filter_,
                                                use_subgraph_reduction_);
        LOG_INFO(
                "Starting compact GK deduplication: localization={}, pairing_filter={}, "
                "subgraph_reduction={}",
                use_localization_, use_pairing_filter_, use_subgraph_reduction_);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        bool const deduplication_trace =
                util::logging::Initialize()->should_log(spdlog::level::trace);
        auto const deduplication_started = deduplication_trace
                                                   ? std::chrono::steady_clock::now()
                                                   : std::chrono::steady_clock::time_point{};
#endif
        auto run = engine_->Run(*graph_, use_localization_, use_pairing_filter_,
                                use_subgraph_reduction_);
        result_ = Decode(*graph_, std::move(run.clusters), run.merge_count);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (deduplication_trace)
            LOG_TRACE("Compact GK phase 'deduplication': {} ns",
                      std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::steady_clock::now() - deduplication_started)
                              .count());
#endif
        LOG_INFO("Compact GK deduplication finished: {} classes, {} merges",
                 result_->clusters.size(), result_->merge_count);
    } catch (...) {
        result_.reset();
        UnsetOption(config::names::kGkUseLocalization);
        throw;
    }
}

gk::DeduplicationResult const& CompactGkDeduplicator::GetResult() const {
    if (!result_)
        throw std::logic_error("Compact GK deduplication has no successful execution result");
    return *result_;
}

}  // namespace algos
