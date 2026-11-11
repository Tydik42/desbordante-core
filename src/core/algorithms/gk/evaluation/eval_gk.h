#pragma once

#include <cstddef>

#include "core/algorithms/gk/model/equivalence_relation.h"
#include "core/algorithms/gk/model/graph_view.h"
#include "core/algorithms/gk/model/joint_mapping.h"
#include "core/algorithms/gk/model/key.h"

namespace gk {

struct EvalGKInput {
    // Graphs and roots borrow caller-owned storage for the duration of Evaluate.
    // Both roots must have the designated variable's type.
    GraphView first_graph;
    GraphView second_graph;
    model::Node const& first_root;
    model::Node const& second_root;
};

// Exact pair search only. The caller supplies a valid GraphKey and ready
// graphs. No options, localization, pairing, reduction or timers live here.
class EvalGK {
public:
    explicit EvalGK(GraphKey key);
    bool Evaluate(EvalGKInput const& input, EquivalenceRelation const& eq);

    TraversalOrder const& GetTraversalOrder() const& noexcept {
        return key_.traversal_order;
    }

    TraversalOrder const& GetTraversalOrder() const&& = delete;

private:
    GraphKey key_;
    JointMapping m_;

    bool SearchStep(std::size_t step, EvalGKInput const& input, EquivalenceRelation const& eq);
    bool IsFeasible(model::Variable const& u, model::Node const& v1, model::Node const& v2,
                    EquivalenceRelation const& eq) const;
};

}  // namespace gk
