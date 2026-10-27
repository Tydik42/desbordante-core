#include "core/algorithms/gk/gk_validator/gk_validator.h"

#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/algorithms/gk/equivalence_relation.h"
#include "core/algorithms/gk/gk_validator/eval_gk.h"
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

void GkValidator::ResetState() {
    LOG_TRACE("Resetting GK validation result");
    result_.holds = true;
    result_.violations.clear();

    timing_stats_.execute_prepare_ns = 0;
    timing_stats_.execute_eval_ns = 0;
    timing_stats_.execute_finalize_ns = 0;
    timing_stats_.execute_total_ns = 0;
    timing_stats_.graph_entity_count = 0;
    timing_stats_.root_candidate_count = 0;
    timing_stats_.root_pair_count = 0;
    timing_stats_.checked_pair_count = 0;
    timing_stats_.violation_count = 0;
    timing_stats_.evaluation = {};
}

void GkValidator::RegisterOptions() {
    using namespace config::descriptions;
    using namespace config::names;

    RegisterOption(config::Option{&graph_, kGkGraphData, kDGkGraphData}.SetIsRequiredFunc(
            [this] { return !OptionIsSet(kGkGraphPath); }));
    RegisterOption(config::Option{&graph_path_, kGkGraphPath, kDGkGraphPath}.SetIsRequiredFunc(
            [this] { return !OptionIsSet(kGkGraphData); }));
    RegisterOption(config::Option{&key_, kGkKeyData, kDGkKeyData});
    RegisterOption(
            config::Option{&use_pairing_filter_, kGkUsePairingFilter, kDGkUsePairingFilter, false});
    RegisterOption(config::Option{&use_subgraph_reduction_, kGkUseSubgraphReduction,
                                  kDGkUseSubgraphReduction, false});
}

GkValidator::GkValidator() : Algorithm() {
    RegisterOptions();
    MakeOptionsAvailable(
            {config::names::kGkGraphData, config::names::kGkGraphPath, config::names::kGkKeyData});
}

void GkValidator::MakeExecuteOptsAvailable() {
    MakeOptionsAvailable(
            {config::names::kGkUsePairingFilter, config::names::kGkUseSubgraphReduction});
}

void GkValidator::LoadDataInternal() {
    auto const load_started_at = std::chrono::steady_clock::now();
    timing_stats_ = {};
    bool const has_graph = OptionIsSet(config::names::kGkGraphData);
    bool const has_graph_path = OptionIsSet(config::names::kGkGraphPath);

    LOG_DEBUG("Loading GK data: in-memory graph option set={}, graph path option set={}", has_graph,
              has_graph_path);
    if (has_graph == has_graph_path) {
        LOG_ERROR("GK data loading requires exactly one graph source");
        throw config::ConfigurationError(
                "Exactly one of gk_graph and gk_graph_path must be provided");
    }

    if (has_graph_path) {
        auto const source_started_at = std::chrono::steady_clock::now();
        LOG_DEBUG("Reading GK graph from '{}'", graph_path_.string());
        graph_ = gk::parser::ReadGraph(graph_path_);
        LOG_DEBUG("Read GK graph from file: {} triples", graph_.size());
        timing_stats_.load_source_ns = ElapsedNanoseconds(source_started_at);
    }

    auto const graph_check_started_at = std::chrono::steady_clock::now();
    gk::CheckGraph(graph_);
    timing_stats_.load_graph_check_ns = ElapsedNanoseconds(graph_check_started_at);

    auto const key_check_started_at = std::chrono::steady_clock::now();
    gk::CheckKey(key_);
    timing_stats_.load_key_check_ns = ElapsedNanoseconds(key_check_started_at);
    timing_stats_.graph_triple_count = graph_.size();
    timing_stats_.load_total_ns = ElapsedNanoseconds(load_started_at);

    LOG_DEBUG(
            "Loaded and validated GK data: {} graph triples, {} key triples, source={} ns, "
            "graph_check={} ns, key_check={} ns, total={} ns",
            graph_.size(), key_.Q.size(), timing_stats_.load_source_ns,
            timing_stats_.load_graph_check_ns, timing_stats_.load_key_check_ns,
            timing_stats_.load_total_ns);
}

void GkValidator::ExecuteInternal() {
    auto const execute_started_at = std::chrono::steady_clock::now();
    LOG_INFO(
            "Starting GK validation: graph triples={}, key triples={}, pairing_filter={}, "
            "subgraph_reduction={}",
            graph_.size(), key_.Q.size(), use_pairing_filter_, use_subgraph_reduction_);

    auto const prepare_started_at = std::chrono::steady_clock::now();
    std::vector<gk::model::Node> const entities = gk::CollectEntities(graph_);
    timing_stats_.graph_entity_count = entities.size();
    std::string const& root_type = key_.T_Q.at(key_.root_x);
    std::vector<gk::model::Node const*> root_candidates;
    root_candidates.reserve(entities.size());
    for (gk::model::Node const& entity : entities) {
        if (entity.type == root_type) root_candidates.push_back(&entity);
    }
    timing_stats_.root_candidate_count = root_candidates.size();

    std::vector<gk::model::EntityId> entity_ids;
    entity_ids.reserve(entities.size());
    for (gk::model::Node const& entity : entities) entity_ids.push_back(entity.id_or_val);

    gk::EquivalenceRelation const eq0(entity_ids);
    gk::EvalGK eval(key_, {use_pairing_filter_, use_subgraph_reduction_});
    std::size_t const root_pair_count =
            root_candidates.size() < 2 ? 0
                                       : root_candidates.size() * (root_candidates.size() - 1) / 2;
    timing_stats_.root_pair_count = root_pair_count;
    timing_stats_.execute_prepare_ns = ElapsedNanoseconds(prepare_started_at);
    LOG_DEBUG(
            "Prepared GK validation: {} graph entities, {} root candidates, {} root pairs, "
            "{} Eq0 classes",
            entities.size(), root_candidates.size(), root_pair_count, eq0.GetClassCount());

    auto const eval_started_at = std::chrono::steady_clock::now();
    std::size_t checked_pairs = 0;
    for (std::size_t first_index = 0; first_index < root_candidates.size(); ++first_index) {
        gk::model::Node const& first = *root_candidates[first_index];
        for (std::size_t second_index = first_index + 1; second_index < root_candidates.size();
             ++second_index) {
            gk::model::Node const& second = *root_candidates[second_index];
            ++checked_pairs;
            LOG_TRACE("Evaluating GK root pair ({}, {})", first.id_or_val, second.id_or_val);
            gk::EvalGKInput const input{graph_, graph_, first, second};
            bool const is_violation = eval.Evaluate(input, eq0, &timing_stats_.evaluation);
            LOG_TRACE("GK root pair ({}, {}) result: {}", first.id_or_val, second.id_or_val,
                      is_violation ? "violation" : "no violation");
            if (is_violation) {
                result_.violations.emplace_back(first.id_or_val, second.id_or_val);
            }
        }
    }
    timing_stats_.checked_pair_count = checked_pairs;
    timing_stats_.execute_eval_ns = ElapsedNanoseconds(eval_started_at);

    auto const finalize_started_at = std::chrono::steady_clock::now();
    result_.holds = result_.violations.empty();
    timing_stats_.violation_count = result_.violations.size();
    timing_stats_.execute_finalize_ns = ElapsedNanoseconds(finalize_started_at);
    timing_stats_.execute_total_ns = ElapsedNanoseconds(execute_started_at);
    LOG_DEBUG(
            "GK evaluation: {} pairing checks, {} filtered pairs, {} exact searches; "
            "neighborhood={} ns, pairing={} ns, reduction={} ns, search={} ns",
            timing_stats_.evaluation.pairing_check_count,
            timing_stats_.evaluation.filtered_pair_count,
            timing_stats_.evaluation.search_call_count, timing_stats_.evaluation.neighborhood_ns,
            timing_stats_.evaluation.pairing_ns, timing_stats_.evaluation.reduction_ns,
            timing_stats_.evaluation.search_ns);
    LOG_INFO(
            "GK validation complete: checked {} root pairs, found {} violations, holds={}, "
            "prepare={} ns, eval={} ns, finalize={} ns, total={} ns",
            checked_pairs, result_.violations.size(), result_.holds,
            timing_stats_.execute_prepare_ns, timing_stats_.execute_eval_ns,
            timing_stats_.execute_finalize_ns, timing_stats_.execute_total_ns);
}

}  // namespace algos
