#include "core/algorithms/gk_compact/gk_validator/gk_validator.h"

#include <stdexcept>
#include <utility>

#include "core/algorithms/gk_compact/model/key.h"
#include "core/algorithms/gk_compact/optimizations/evaluation.h"
#include "core/algorithms/gk_compact/parser/graph_parser.h"
#include "core/algorithms/gk_compact/trace.h"
#include "core/config/gk/option.h"
#include "core/config/names_and_descriptions.h"
#include "core/util/logger.h"

namespace {

gk::ValidationResult Decode(gk_compact::Graph const& graph,
                            std::vector<gk_compact::VertexPair> const& pairs) {
    gk::ValidationResult result{pairs.empty(), {}};
    result.violations.reserve(pairs.size());
    for (auto const& [first, second] : pairs) {
        result.violations.emplace_back(graph.GetLabel(first), graph.GetLabel(second));
    }
    return result;
}

}  // namespace

namespace algos {

CompactGkValidator::CompactGkValidator() {
    RegisterOptions();
    MakeOptionsAvailable(
            {config::names::kGkGraphData, config::names::kGkGraphPath, config::names::kGkKeyData});
}

void CompactGkValidator::RegisterOptions() {
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

void CompactGkValidator::MakeExecuteOptsAvailable() {
    MakeOptionsAvailable({config::names::kGkUseLocalization});
}

void CompactGkValidator::LoadDataInternal() {
    using gk_compact::TracePhase;
    key_.reset();
    result_.reset();
    bool const has_graph = OptionIsSet(config::names::kGkGraphData);
    bool const has_path = OptionIsSet(config::names::kGkGraphPath);
    if (has_graph == has_path)
        throw config::ConfigurationError("Exactly one of gk_graph and gk_graph_path is required");
    if (has_path) {
        graph_ = TracePhase("source_load", [&] {
            return std::make_shared<gk_compact::Graph const>(
                    gk_compact::parser::ReadGraph(graph_path_));
        });
    }
    if (!graph_) throw config::ConfigurationError("gk_graph must not be null");
    auto const& graph = *graph_;
    key_ = TracePhase("key_prepare", [&] { return gk_compact::CompileKey(source_key_, graph); });
    LOG_DEBUG("Loaded compact GK validation data: {} vertices, {} edges, key prepared={}",
              graph.GetVertexCount(), graph.GetEdgeCount(), key_.has_value());
}

void CompactGkValidator::ResetState() {
    // Option::Unset clears configuration state, not its backing field.
    if (!OptionIsSet(config::names::kGkUsePairingFilter)) use_pairing_filter_ = false;
    if (!OptionIsSet(config::names::kGkUseSubgraphReduction)) use_subgraph_reduction_ = false;
    result_.reset();
}

void CompactGkValidator::ExecuteInternal() {
    using gk_compact::TracePhase;
    try {
        config::CheckGkOptimizationDependencies(use_localization_, use_pairing_filter_,
                                                use_subgraph_reduction_);
        LOG_INFO(
                "Starting compact GK validation: localization={}, pairing_filter={}, "
                "subgraph_reduction={}",
                use_localization_, use_pairing_filter_, use_subgraph_reduction_);
        TracePhase("validation", [&] {
            auto const& graph = *graph_;
            std::vector<gk_compact::VertexPair> violations;
            if (key_) {
                auto const roots = TracePhase("root_selection", [&] {
                    std::vector<gk_compact::VertexId> selected;
                    auto const type = key_->nodes[key_->root].type;
                    for (auto entity : graph.EntityIds()) {
                        if (graph.GetType(entity) == type) selected.push_back(entity);
                    }
                    return selected;
                });
                if (roots.size() > 1) {
                    std::optional<gk_compact::Neighborhoods> neighborhoods;
                    if (use_localization_)
                        neighborhoods.emplace(
                                gk_compact::PrepareNeighborhoods(graph, roots, *key_));
                    gk_compact::EquivalenceRelation const eq0(graph.Metadata().GetEntityCount());
                    gk_compact::EvalGK evaluator(*key_);
                    TracePhase("pair_checks", [&] {
                        for (std::size_t first = 0; first + 1 < roots.size(); ++first) {
                            auto const first_graph = neighborhoods ? (*neighborhoods)[first]
                                                                   : gk_compact::GraphView(graph);
                            for (std::size_t second = first + 1; second < roots.size(); ++second) {
                                auto const second_graph = neighborhoods
                                                                  ? (*neighborhoods)[second]
                                                                  : gk_compact::GraphView(graph);
                                if (gk_compact::EvaluatePair(evaluator, *key_,
                                                             {first_graph, second_graph,
                                                              roots[first], roots[second]},
                                                             eq0, use_pairing_filter_,
                                                             use_subgraph_reduction_))
                                    violations.emplace_back(roots[first], roots[second]);
                            }
                        }
                    });
                }
            }
            result_ = TracePhase("result_decode", [&] { return Decode(graph, violations); });
        });
        LOG_INFO("Compact GK validation finished: {} violations", result_->violations.size());
    } catch (...) {
        result_.reset();
        UnsetOption(config::names::kGkUseLocalization);
        throw;
    }
}

gk::ValidationResult const& CompactGkValidator::GetResult() const {
    if (!result_)
        throw std::logic_error("Compact GK validation has no successful execution result");
    return *result_;
}

}  // namespace algos
