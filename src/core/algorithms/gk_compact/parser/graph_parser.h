#pragma once

#include <filesystem>
#include <istream>

#include "core/algorithms/gk_compact/graph/graph.h"

namespace gk_compact::parser {

// The same six-column CSV as gk::parser::ReadGraph, directly fed into the numeric builder.
Graph ReadGraph(std::istream& stream);
Graph ReadGraph(std::filesystem::path const& path);

}  // namespace gk_compact::parser
