#include "core/algorithms/gk_compact/evaluation/eval_gk.h"

#include <utility>

namespace gk_compact {

EvalGK::EvalGK(CompiledKey key) : key_(std::move(key)), m_(key_.nodes.size()) {}

bool EvalGK::Evaluate(EvalGKInput const& input, EquivalenceRelation const& eq) {
    m_.Reset();
    m_.Bind(key_.root, input.first_root, input.second_root);
    return SearchStep(0, input, eq);
}

bool EvalGK::SearchStep(std::size_t step, EvalGKInput const& input, EquivalenceRelation const& eq) {
    if (step == key_.traversal_order.size()) return true;
    auto const& triple = key_.triples[key_.traversal_order[step]];
    bool const subject_bound = m_.IsBound(triple.subject);
    bool const object_bound = m_.IsBound(triple.object);
    if (subject_bound && object_bound) {
        auto const [first_subject, second_subject] = m_.Get(triple.subject);
        auto const [first_object, second_object] = m_.Get(triple.object);
        return input.first_graph.HasEdge(first_subject, triple.predicate, first_object) &&
               input.second_graph.HasEdge(second_subject, triple.predicate, second_object) &&
               SearchStep(step + 1, input, eq);
    }

    // The traversal guarantees that at least one endpoint is already assigned.
    bool const forward = subject_bound;
    auto const variable = forward ? triple.object : triple.subject;
    auto const [first_anchor, second_anchor] = m_.Get(forward ? triple.subject : triple.object);
    auto try_pair = [&](VertexId first, VertexId second) {
        if (!IsFeasible(variable, first, second, input.first_graph, eq)) return false;
        m_.Bind(variable, first, second);
        if (SearchStep(step + 1, input, eq)) return true;
        m_.Unbind(variable);
        return false;
    };

    using gk::model::VarKind;
    auto const& node = key_.nodes[variable];
    if (node.kind == VarKind::kConstant) {
        auto const value = *node.constant;
        bool const first_present =
                forward ? input.first_graph.HasEdge(first_anchor, triple.predicate, value)
                        : input.first_graph.HasEdge(value, triple.predicate, first_anchor);
        if (!first_present) return false;
        bool const second_present =
                forward ? input.second_graph.HasEdge(second_anchor, triple.predicate, value)
                        : input.second_graph.HasEdge(value, triple.predicate, second_anchor);
        return second_present && try_pair(value, value);
    }

    auto first_edges = forward ? input.first_graph.Outgoing(first_anchor, triple.predicate)
                               : input.first_graph.Incoming(first_anchor, triple.predicate);
    auto second_edges = forward ? input.second_graph.Outgoing(second_anchor, triple.predicate)
                                : input.second_graph.Incoming(second_anchor, triple.predicate);
    // Predicate-specific adjacency lists remain sorted by original neighbor ID,
    // including when GraphView filters them. Equal-value and identity-Eq
    // assignments therefore need an intersection, not a Cartesian product.
    if (node.kind == VarKind::kValue || (node.kind == VarKind::kEntity && eq.IsIdentity())) {
        auto first = first_edges.begin();
        auto second = second_edges.begin();
        while (first != first_edges.end() && second != second_edges.end()) {
            if (first->neighbor < second->neighbor) {
                ++first;
            } else if (second->neighbor < first->neighbor) {
                ++second;
            } else {
                if (try_pair(first->neighbor, second->neighbor)) return true;
                ++first;
                ++second;
            }
        }
        return false;
    }

    for (auto const& first_edge : first_edges) {
        for (auto const& second_edge : second_edges) {
            if (try_pair(first_edge.neighbor, second_edge.neighbor)) return true;
        }
    }
    return false;
}

bool EvalGK::IsFeasible(PatternNodeId variable, VertexId first, VertexId second,
                        GraphView const& graph, EquivalenceRelation const& eq) const {
    using gk::model::VarKind;
    auto const& node = key_.nodes[variable];
    switch (node.kind) {
        case VarKind::kEntity:
        case VarKind::kWildcard:
            if (graph.GetType(first) != node.type || graph.GetType(second) != node.type)
                return false;
            if (node.kind == VarKind::kEntity && first != second &&
                !eq.AreEquivalent(first, second))
                return false;
            break;
        case VarKind::kValue:
            if (first != second || graph.IsEntity(first)) return false;
            break;
        case VarKind::kConstant:
            if (first != *node.constant || second != *node.constant) return false;
            break;
    }
    return m_.IsInjective(first, second);
}

}  // namespace gk_compact
