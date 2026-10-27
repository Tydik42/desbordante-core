#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "core/algorithms/gk/evaluation_options.h"
#include "core/algorithms/gk/graph_descriptor.h"

namespace gk {

struct PairingResult {
    bool pairable = false;
    // Populated only for pairable roots when reduction was requested. Own their data;
    // no references to temporary neighborhoods are retained by EvalGK.
    model::Graph first_graph;
    model::Graph second_graph;
};

// Proposition 9 / Section 4.2 of Keys for Graphs. This is a necessary-condition
// filter, not an embedding search: it deliberately ignores Eq and injectivity.
class PairingFilter {
public:
    explicit PairingFilter(model::GraphKey key);

    PairingResult Prepare(model::Graph const& first_graph, model::Graph const& second_graph,
                          model::Node const& first_root, model::Node const& second_root,
                          bool reduce, EvaluationStatistics& statistics) const;

private:
    model::GraphKey key_;
    std::vector<model::Variable> variables_;
    std::vector<std::pair<std::size_t, std::size_t>> endpoints_;
    std::size_t root_index_ = 0;
    std::size_t radius_ = 0;
};

}  // namespace gk
