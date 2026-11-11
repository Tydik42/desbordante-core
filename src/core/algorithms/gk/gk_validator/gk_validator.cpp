#include "core/algorithms/gk/gk_validator/gk_validator.h"

#include <stdexcept>
#include <utility>

#include "core/algorithms/gk/model/equivalence_relation.h"
#include "core/algorithms/gk/model/graph_builder.h"
#include "core/algorithms/gk/optimizations/evaluation.h"
#include "core/algorithms/gk/parser/graph_parser.h"
#include "core/config/gk/option.h"
#include "core/config/names_and_descriptions.h"
#include "core/util/logger.h"

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
#include <chrono>
#endif

namespace algos {

GkValidator::GkValidator() {
    RegisterOptions();
    MakeOptionsAvailable(
            {config::names::kGkGraphData, config::names::kGkGraphPath, config::names::kGkKeyData});
}

void GkValidator::RegisterOptions() {
    using namespace config::names;
    using namespace config::descriptions;
    RegisterOption(config::Option{&graph_, kGkGraphData, kDGkGraphData}.SetIsRequiredFunc(
            [this] { return !OptionIsSet(kGkGraphPath); }));
    RegisterOption(config::Option{&graph_path_, kGkGraphPath, kDGkGraphPath}.SetIsRequiredFunc(
            [this] { return !OptionIsSet(kGkGraphData); }));
    RegisterOption(config::Option{&source_key_, kGkKeyData, kDGkKeyData});
    RegisterOption(config::kGkUseLocalizationOpt(&use_localization_)
                           .SetConditionalOpts({{[](bool enabled) { return enabled; },
                                                 {kGkUsePairingFilter}}}));
    RegisterOption(config::kGkUsePairingFilterOpt(&use_pairing_filter_)
                           .SetConditionalOpts({{[](bool enabled) { return enabled; },
                                                 {kGkUseSubgraphReduction}}}));
    RegisterOption(config::kGkUseSubgraphReductionOpt(&use_subgraph_reduction_));
}

void GkValidator::MakeExecuteOptsAvailable() {
    MakeOptionsAvailable({config::names::kGkUseLocalization});
}

void GkValidator::LoadDataInternal() {
    UnsetOption(config::names::kGkUseLocalization);
    data_ready_ = false;
    key_.reset();
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
    key_ = gk::BuildGraphKey(source_key_);
    data_ready_ = true;
    LOG_DEBUG("Loaded GK validation data: {} triples, {} pattern triples", graph_.size(),
              source_key_.Q.size());
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    if (load_trace)
        LOG_TRACE("GK phase 'load': {} ns", std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                    std::chrono::steady_clock::now() - load_started)
                                                    .count());
#endif
}

void GkValidator::ResetState() {
    if (!OptionIsSet(config::names::kGkUsePairingFilter)) use_pairing_filter_ = false;
    if (!OptionIsSet(config::names::kGkUseSubgraphReduction)) use_subgraph_reduction_ = false;
    result_.reset();
}

void GkValidator::ExecuteInternal() {
    try {
        if (!data_ready_)
            throw std::logic_error("GK validation data has not been loaded successfully");
        config::CheckGkOptimizationDependencies(use_localization_, use_pairing_filter_,
                                                use_subgraph_reduction_);
        LOG_INFO(
                "Starting GK validation: localization={}, pairing_filter={}, subgraph_reduction={}",
                use_localization_, use_pairing_filter_, use_subgraph_reduction_);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        bool const validation_trace = util::logging::Initialize()->should_log(spdlog::level::trace);
        auto const validation_started = validation_trace ? std::chrono::steady_clock::now()
                                                         : std::chrono::steady_clock::time_point{};
#endif
        auto const entities = gk::CollectEntities(graph_);
        std::vector<gk::model::Node const*> roots;
        auto const& type = key_->descriptor.T_Q.at(key_->descriptor.root_x);
        std::vector<gk::model::EntityId> entity_ids;
        entity_ids.reserve(entities.size());
        for (auto const& entity : entities) {
            entity_ids.push_back(entity.id_or_val);
            if (entity.type == type) roots.push_back(&entity);
        }
        std::vector<gk::EntityPair> violations;
        if (roots.size() > 1) {
            std::optional<gk::Neighborhoods> neighborhoods;
            if (use_localization_)
                neighborhoods.emplace(gk::PrepareNeighborhoods(graph_, roots, *key_));
            gk::EquivalenceRelation const eq0(entity_ids);
            gk::EvalGK evaluator(*key_);
            for (std::size_t first = 0; first + 1 < roots.size(); ++first) {
                auto const first_graph =
                        neighborhoods ? (*neighborhoods)[first] : gk::GraphView(graph_);
                for (std::size_t second = first + 1; second < roots.size(); ++second) {
                    auto const second_graph =
                            neighborhoods ? (*neighborhoods)[second] : gk::GraphView(graph_);
                    if (gk::EvaluatePair(evaluator, *key_,
                                         {first_graph, second_graph, *roots[first], *roots[second]},
                                         eq0, use_pairing_filter_, use_subgraph_reduction_))
                        violations.emplace_back(roots[first]->id_or_val, roots[second]->id_or_val);
                }
            }
        }
        bool const holds = violations.empty();
        result_ = gk::ValidationResult{holds, std::move(violations)};
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (validation_trace)
            LOG_TRACE("GK phase 'validation': {} ns",
                      std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::steady_clock::now() - validation_started)
                              .count());
#endif
        LOG_INFO("GK validation finished: {} violations", result_->violations.size());
    } catch (...) {
        result_.reset();
        UnsetOption(config::names::kGkUseLocalization);
        throw;
    }
}

gk::ValidationResult const& GkValidator::GetResult() const {
    if (!result_) throw std::logic_error("GK validation has no successful execution result");
    return *result_;
}

}  // namespace algos
