#pragma once

#include <compare>
#include <cstddef>
#include <vector>

#include "core/algorithms/gk_compact/model/equivalence_relation.h"
#include "core/algorithms/gk_compact/model/graph_view.h"
#include "core/algorithms/gk_compact/model/joint_mapping.h"
#include "core/algorithms/gk_compact/model/key.h"

namespace gk_compact {

struct EvalGKInput {
    // Graphs sharing one metadata domain, both containing their root.
    GraphView first_graph;
    GraphView second_graph;
    VertexId first_root;
    VertexId second_root;
};

class EvalGK {
public:
    // Takes an already prepared key, including its constraint-first traversal. No input
    // validation, localization, filtering, reduction or instrumentation here.
    explicit EvalGK(GraphKey key);

    // Both graphs and Eq use the same metadata's entity numbering. Each call
    // resets the owned mapping without reallocating it. Full/selected inputs
    // are dispatched once, so full graphs use plain CSR spans throughout the
    // recursion. Roots have the key's root type and belong to their inputs.
    // Concurrent searches require separate evaluator instances.
    bool Evaluate(EvalGKInput const& input, EquivalenceRelation const& eq);

    TraversalOrder const& GetTraversalOrder() const& noexcept {
        return key_.traversal_order;
    }

    TraversalOrder const& GetTraversalOrder() const&& = delete;

private:
    template <typename FirstGraph, typename SecondGraph>
    bool SearchStep(std::size_t step, FirstGraph const& first_graph,
                    SecondGraph const& second_graph, EquivalenceRelation const& eq);
    // Candidate generation already enforces Eq for ordinary entity variables.
    bool IsFeasible(PatternNodeId variable, VertexId first, VertexId second,
                    Graph const& graph) const;

    struct EquivalentNeighbor {
        VertexId representative;
        VertexId vertex;
        auto operator<=>(EquivalentNeighbor const&) const = default;
    };

    struct EquivalentNeighbors {
        std::vector<EquivalentNeighbor> first;
        std::vector<EquivalentNeighbor> second;
    };

    GraphKey key_;
    JointMapping m_;
    // Allocated only for a nonidentity entity domain. Buffers belong to a
    // pattern node, so deeper recursive expansions cannot invalidate them.
    std::vector<EquivalentNeighbors> equivalent_neighbors_;
};

}  // namespace gk_compact
