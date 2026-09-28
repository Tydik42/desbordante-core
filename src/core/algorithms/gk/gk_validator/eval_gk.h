#pragma once

#include <cstddef>
#include <vector>

#include "core/algorithms/gk/equivalence_relation.h"
#include "core/algorithms/gk/graph_descriptor.h"
#include "core/algorithms/gk/joint_mapping.h"

namespace gk {

// An execution order stores indices into GraphKey::Q.  It is derived from the key and is not part
// of the mathematical GraphKey itself.
using TraversalOrder = std::vector<std::size_t>;

struct EvalGKInput {
    // The two graph arguments may refer to the same full graph in the baseline and to two
    // different neighborhoods in a later execution mode. Their lifetime must cover one
    // Evaluate call.
    model::Graph const& first_graph;
    model::Graph const& second_graph;
    model::Node const& first_root;
    model::Node const& second_root;
};

class EvalGK {
public:
    explicit EvalGK(model::GraphKey key);

    // Evaluate one pair of root entities. Roots must be entities of T_Q[root_x]. The
    // implementation creates a fresh JointMapping for this call. Equivalence is supplied per
    // call, so one EvalGK object can be reused with different relations and from independent
    // parallel tasks.
    bool Evaluate(EvalGKInput const& input, EquivalenceRelation const& eq) const;

    TraversalOrder const& GetTraversalOrder() const noexcept {
        return traversal_order_;
    }

private:
    static TraversalOrder BuildTraversalOrder(model::GraphKey const& key);

    model::GraphKey key_;
    TraversalOrder traversal_order_;

    bool SearchStep(std::size_t step, EvalGKInput const& input, JointMapping& m,
                    EquivalenceRelation const& eq) const;
    bool IsFeasible(model::Variable const& u, model::Node const& v1, model::Node const& v2,
                    JointMapping const& m, EquivalenceRelation const& eq) const;
};

}  // namespace gk
