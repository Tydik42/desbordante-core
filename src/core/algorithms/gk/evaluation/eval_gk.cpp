#include "core/algorithms/gk/evaluation/eval_gk.h"

#include <algorithm>
#include <utility>

namespace gk {

EvalGK::EvalGK(CompiledKey key) : key_(std::move(key)) {}

bool EvalGK::Evaluate(EvalGKInput const& input, EquivalenceRelation const& eq) {
    m_.Reset();
    m_.Bind(input.first_root, input.second_root, key_.descriptor.root_x);
    return SearchStep(0, input, eq);
}

bool EvalGK::SearchStep(std::size_t step, EvalGKInput const& input, EquivalenceRelation const& eq) {
    if (step >= key_.traversal_order.size()) {
        return true;
    }

    auto const& [current_subject, predicate, current_object] =
            key_.descriptor.Q[key_.traversal_order[step]];

    bool subject_bound = m_.IsBound(current_subject);
    bool object_bound = m_.IsBound(current_object);

    // Case A
    if (subject_bound && object_bound) {
        auto const& [s1, s2] = m_.Get(current_subject);
        auto const& [o1, o2] = m_.Get(current_object);

        bool const graph1_has_edge = input.first_graph.HasEdge(s1, predicate, o1);
        bool const graph2_has_edge = input.second_graph.HasEdge(s2, predicate, o2);

        return (graph1_has_edge && graph2_has_edge) ? SearchStep(step + 1, input, eq) : false;
    }

    // Case B
    if (subject_bound && !object_bound) {
        auto const& [s1, s2] = m_.Get(current_subject);

        for (auto const& [inner_s1, p1, o1] : input.first_graph) {
            if (inner_s1 != s1 || p1 != predicate) continue;
            for (auto const& [inner_s2, p2, o2] : input.second_graph) {
                if (inner_s2 != s2 || p2 != predicate) continue;

                if (IsFeasible(current_object, o1, o2, eq)) {
                    m_.Bind(o1, o2, current_object);
                    if (SearchStep(step + 1, input, eq)) return true;
                    m_.Unbind(current_object);
                }
            }
        }

        return false;
    }

    // Case C (reverse B)
    if (!subject_bound && object_bound) {
        auto const& [o1, o2] = m_.Get(current_object);

        for (auto const& [s1, p1, inner_o1] : input.first_graph) {
            if (inner_o1 != o1 || p1 != predicate) continue;
            for (auto const& [s2, p2, inner_o2] : input.second_graph) {
                if (inner_o2 != o2 || p2 != predicate) continue;

                if (IsFeasible(current_subject, s1, s2, eq)) {
                    m_.Bind(s1, s2, current_subject);
                    if (SearchStep(step + 1, input, eq)) return true;
                    m_.Unbind(current_subject);
                }
            }
        }

        return false;
    }

    return false;
}

bool EvalGK::IsFeasible(model::Variable const& u, model::Node const& v1, model::Node const& v2,
                        EquivalenceRelation const& eq) const {
    if (!m_.IsInjective(u, v1, v2)) return false;

    bool const are_entities =
            v1.kind == model::NodeKind::kEntity && v2.kind == model::NodeKind::kEntity;
    bool const are_values =
            v1.kind == model::NodeKind::kValue && v2.kind == model::NodeKind::kValue;

    switch (u.kind) {
        case model::VarKind::kEntity:
        case model::VarKind::kWildcard: {
            if (!are_entities) return false;

            auto const& type = key_.descriptor.T_Q.at(u);
            if (v1.type != type || v2.type != type) return false;

            return u.kind == model::VarKind::kWildcard || u == key_.descriptor.root_x ||
                   eq.AreEquivalent(v1.id_or_val, v2.id_or_val);
        }

        case model::VarKind::kValue:
            return are_values && v1.id_or_val == v2.id_or_val;

        case model::VarKind::kConstant:
            return are_values && v1.id_or_val == u.name && v2.id_or_val == u.name;
    }

    return false;
}
}  // namespace gk
