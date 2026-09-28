#pragma once

#include <filesystem>
#include <istream>

#include "core/algorithms/gk/graph_descriptor.h"

namespace gk::parser {

model::Graph ReadGraph(std::istream& stream);
model::Graph ReadGraph(std::filesystem::path const& path);

}  // namespace gk::parser
