#pragma once

#include <filesystem>
#include <istream>

#include "core/algorithms/gk/graph_descriptor.h"

namespace gk::parser {

// UTF-8 CSV with the six GK columns in their prescribed order. Quoted commas, doubled
// quotes and multiline fields are supported; field contents are preserved verbatim.
model::Graph ReadGraph(std::istream& stream);
model::Graph ReadGraph(std::filesystem::path const& path);

}  // namespace gk::parser
