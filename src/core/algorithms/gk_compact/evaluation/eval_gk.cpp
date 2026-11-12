#include "core/algorithms/gk_compact/evaluation/eval_gk.h"

#include <algorithm>
#include <utility>

namespace gk_compact {
namespace {

Graph const& BaseGraph(Graph const& graph) {
    return graph;
}

Graph const& BaseGraph(GraphView const& graph) {
    return graph.Base();
}

// The anchor is already bound in m and therefore belongs to its input.
// Static overloads keep full-graph traversal on plain contiguous CSR spans.
auto Adjacent(Graph const& graph, VertexId anchor, PredicateId predicate, bool forward) {
    return forward ? graph.Outgoing(anchor, predicate) : graph.Incoming(anchor, predicate);
}

auto Adjacent(GraphView const& graph, VertexId anchor, PredicateId predicate, bool forward) {
    return forward ? graph.OutgoingFromMember(anchor, predicate)
                   : graph.IncomingToMember(anchor, predicate);
}

}  // namespace

EvalGK::EvalGK(GraphKey key) : key_(std::move(key)), m_(key_.nodes.size()) {}

template <typename FirstGraph, typename SecondGraph>
bool EvalGK::SearchStep(std::size_t step, FirstGraph const& first_graph,
                        SecondGraph const& second_graph, EquivalenceRelation const& eq) {
    if (step == key_.traversal_order.size()) return true;
    auto const& triple = key_.triples[key_.traversal_order[step]];
    bool const subject_bound = m_.IsBound(triple.subject);
    bool const object_bound = m_.IsBound(triple.object);
    if (subject_bound && object_bound) {
        auto const [first_subject, second_subject] = m_.Get(triple.subject);
        auto const [first_object, second_object] = m_.Get(triple.object);
        // Both endpoints were obtained from this input (or supplied as valid
        // roots). Their membership is already established, even after reduction.
        return BaseGraph(first_graph).HasEdge(first_subject, triple.predicate, first_object) &&
               BaseGraph(second_graph).HasEdge(second_subject, triple.predicate, second_object) &&
               SearchStep(step + 1, first_graph, second_graph, eq);
    }

    // The traversal guarantees that at least one endpoint is already assigned.
    bool const forward = subject_bound;
    auto const variable = forward ? triple.object : triple.subject;
    auto const [first_anchor, second_anchor] = m_.Get(forward ? triple.subject : triple.object);
    auto try_pair = [&](VertexId first, VertexId second) {
        if (!IsFeasible(variable, first, second, BaseGraph(first_graph))) return false;
        m_.Bind(variable, first, second);
        if (SearchStep(step + 1, first_graph, second_graph, eq)) return true;
        m_.Unbind(variable);
        return false;
    };

    using gk::model::VarKind;
    auto const& node = key_.nodes[variable];
    if (node.kind == VarKind::kConstant) {
        auto const value = *node.constant;
        // Unlike bound endpoints, the constant may be outside a selected view.
        // Keep the membership-aware public query for this unbound candidate.
        bool const first_present =
                forward ? first_graph.HasEdge(first_anchor, triple.predicate, value)
                        : first_graph.HasEdge(value, triple.predicate, first_anchor);
        if (!first_present) return false;
        bool const second_present =
                forward ? second_graph.HasEdge(second_anchor, triple.predicate, value)
                        : second_graph.HasEdge(value, triple.predicate, second_anchor);
        return second_present && try_pair(value, value);
    }

    auto first_edges = Adjacent(first_graph, first_anchor, triple.predicate, forward);
    auto second_edges = Adjacent(second_graph, second_anchor, triple.predicate, forward);
    // Predicate-specific adjacency lists remain sorted by original neighbor ID,
    // including when GraphView filters them. Equal-value and identity-Eq
    // assignments therefore need an intersection, not a Cartesian product.
    if (node.kind == VarKind::kValue ||
        (node.kind == VarKind::kEntity && eq.IsIdentity(node.type))) {
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

    if (node.kind == VarKind::kEntity) {
        if (equivalent_neighbors_.empty()) equivalent_neighbors_.resize(key_.nodes.size());
        auto& groups = equivalent_neighbors_[variable];
        auto collect = [&](auto& edges, std::vector<EquivalentNeighbor>& neighbors) {
            neighbors.clear();
            for (auto const& edge : edges) {
                if (BaseGraph(first_graph).GetType(edge.neighbor) != node.type) continue;
                neighbors.push_back({eq.GetRepresentative(edge.neighbor), edge.neighbor});
            }
            std::ranges::sort(neighbors);
        };
        collect(first_edges, groups.first);
        collect(second_edges, groups.second);
        auto first = groups.first.begin(), second = groups.second.begin();
        while (first != groups.first.end() && second != groups.second.end()) {
            if (first->representative < second->representative) {
                ++first;
            } else if (second->representative < first->representative) {
                ++second;
            } else {
                auto const representative = first->representative;
                auto first_end = first, second_end = second;
                while (first_end != groups.first.end() &&
                       first_end->representative == representative)
                    ++first_end;
                while (second_end != groups.second.end() &&
                       second_end->representative == representative)
                    ++second_end;
                for (auto left = first; left != first_end; ++left)
                    for (auto right = second; right != second_end; ++right)
                        if (try_pair(left->vertex, right->vertex)) return true;
                first = first_end;
                second = second_end;
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

bool EvalGK::Evaluate(EvalGKInput const& input, EquivalenceRelation const& eq) {
    m_.Reset();
    m_.Bind(key_.root, input.first_root, input.second_root);
    // Four concrete instantiations, no virtual interface. Full sides use plain
    // CSR spans, not filtering iterators. Mixed views are valid inputs too.
    if (input.first_graph.IsFull()) {
        if (input.second_graph.IsFull())
            return SearchStep(0, input.first_graph.Base(), input.second_graph.Base(), eq);
        return SearchStep(0, input.first_graph.Base(), input.second_graph, eq);
    }
    if (input.second_graph.IsFull())
        return SearchStep(0, input.first_graph, input.second_graph.Base(), eq);
    return SearchStep(0, input.first_graph, input.second_graph, eq);
}

bool EvalGK::IsFeasible(PatternNodeId variable, VertexId first, VertexId second,
                        Graph const& graph) const {
    using gk::model::VarKind;
    auto const& node = key_.nodes[variable];
    switch (node.kind) {
        case VarKind::kEntity:
        case VarKind::kWildcard:
            if (graph.GetType(first) != node.type || graph.GetType(second) != node.type)
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
