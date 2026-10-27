#include "core/config/gk/option.h"

#include "core/config/names_and_descriptions.h"

namespace config {

CommonOption<bool> const kGkUseLocalizationOpt{names::kGkUseLocalization,
                                               descriptions::kDGkUseLocalization, false};
CommonOption<bool> const kGkUsePairingFilterOpt{names::kGkUsePairingFilter,
                                                descriptions::kDGkUsePairingFilter, false};
CommonOption<bool> const kGkUseSubgraphReductionOpt{names::kGkUseSubgraphReduction,
                                                    descriptions::kDGkUseSubgraphReduction, false};

void CheckGkOptimizationDependencies(bool localization, bool pairing, bool reduction) {
    if (reduction && !pairing)
        throw ConfigurationError("use_subgraph_reduction requires use_pairing_filter=true");
    if (pairing && !localization)
        throw ConfigurationError("use_pairing_filter requires use_localization=true");
}

}  // namespace config
