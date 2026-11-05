#pragma once

#include <cstddef>

#include "core/algorithms/gk_compact/equivalence_relation.h"
#include "core/algorithms/gk_compact/joint_mapping.h"
#include "core/algorithms/gk_compact/model/graph_view.h"
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
    explicit EvalGK(CompiledKey key);

    // Both graphs and Eq use the same metadata's entity numbering. Each call
    // resets the owned mapping without reallocating it. Roots have the key's
    // root type. Concurrent searches require separate evaluator instances.
    bool Evaluate(EvalGKInput const& input, EquivalenceRelation const& eq);

    TraversalOrder const& GetTraversalOrder() const& noexcept {
        return key_.traversal_order;
    }

    TraversalOrder const& GetTraversalOrder() const&& = delete;

private:
    bool SearchStep(std::size_t step, EvalGKInput const& input, EquivalenceRelation const& eq);
    bool IsFeasible(PatternNodeId variable, VertexId first, VertexId second, GraphView const& graph,
                    EquivalenceRelation const& eq) const;

    CompiledKey key_;
    JointMapping m_;
};

}  // namespace gk_compact
