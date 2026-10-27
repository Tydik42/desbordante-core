#include "core/algorithms/gk/gk_deduplicator/gk_deduplicator.h"

#include <chrono>
#include <stdexcept>

#include "core/algorithms/gk/parser/gk_graph_parser.h"
#include "core/algorithms/gk/validation.h"
#include "core/config/names_and_descriptions.h"
#include "core/util/logger.h"

namespace {

std::uint64_t ElapsedNanoseconds(std::chrono::steady_clock::time_point started_at) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                              std::chrono::steady_clock::now() - started_at)
                                              .count());
}

}  // namespace

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
    RegisterOption(config::Option{&keys_, kGkKeysData, kDGkKeysData});
    RegisterOption(
            config::Option{&use_pairing_filter_, kGkUsePairingFilter, kDGkUsePairingFilter, false});
    RegisterOption(config::Option{&use_subgraph_reduction_, kGkUseSubgraphReduction,
                                  kDGkUseSubgraphReduction, false});
}

void GkDeduplicator::MakeExecuteOptsAvailable() {
    MakeOptionsAvailable(
            {config::names::kGkUsePairingFilter, config::names::kGkUseSubgraphReduction});
}

void GkDeduplicator::LoadDataInternal() {
    auto const started_at = std::chrono::steady_clock::now();
    engine_.reset();
    result_ = {};
    timing_stats_ = {};
    bool const has_graph = OptionIsSet(config::names::kGkGraphData);
    bool const has_path = OptionIsSet(config::names::kGkGraphPath);
    if (has_graph == has_path) {
        throw config::ConfigurationError(
                "Exactly one of gk_graph and gk_graph_path must be provided");
    }
    if (has_path) {
        auto const source_started_at = std::chrono::steady_clock::now();
        LOG_DEBUG("Reading GK deduplication graph from '{}'", graph_path_.string());
        graph_ = gk::parser::ReadGraph(graph_path_);
        timing_stats_.load_source_ns = ElapsedNanoseconds(source_started_at);
    }
    auto const graph_started_at = std::chrono::steady_clock::now();
    gk::CheckGraph(graph_);
    timing_stats_.load_graph_check_ns = ElapsedNanoseconds(graph_started_at);
    auto const keys_started_at = std::chrono::steady_clock::now();
    engine_.emplace(keys_);
    timing_stats_.load_keys_check_ns = ElapsedNanoseconds(keys_started_at);
    timing_stats_.graph_triple_count = graph_.size();
    timing_stats_.key_count = keys_.size();
    timing_stats_.load_total_ns = ElapsedNanoseconds(started_at);
    LOG_DEBUG("Loaded GK deduplication data: {} graph triples, {} keys, elapsed={} ns",
              graph_.size(), keys_.size(), timing_stats_.load_total_ns);
}

void GkDeduplicator::ResetState() {
    result_ = {};
    timing_stats_.execution = {};
    timing_stats_.execute_total_ns = 0;
}

void GkDeduplicator::ExecuteInternal() {
    auto const started_at = std::chrono::steady_clock::now();
    if (!engine_) throw std::logic_error("GK deduplication data has not been loaded successfully");
    LOG_INFO(
            "Starting GK deduplication: {} graph triples, {} keys, pairing_filter={}, "
            "subgraph_reduction={}",
            graph_.size(), keys_.size(), use_pairing_filter_, use_subgraph_reduction_);
    result_ = engine_->Run(graph_, {use_pairing_filter_, use_subgraph_reduction_});
    timing_stats_.execution = result_.statistics;
    timing_stats_.execute_total_ns = ElapsedNanoseconds(started_at);
    LOG_INFO("GK deduplication complete: {} classes, {} merges, {} passes, elapsed={} ns",
             GetClusterCount(), GetMergeCount(), result_.statistics.pass_count,
             timing_stats_.execute_total_ns);
}

}  // namespace algos
