#pragma once

#include "core/config/common_option.h"

namespace config {

extern CommonOption<bool> const kGkUseLocalizationOpt;
extern CommonOption<bool> const kGkUsePairingFilterOpt;
extern CommonOption<bool> const kGkUseSubgraphReductionOpt;

// For direct calls that do not pass through Algorithm's dependent options.
void CheckGkOptimizationDependencies(bool localization, bool pairing, bool reduction);

}  // namespace config
