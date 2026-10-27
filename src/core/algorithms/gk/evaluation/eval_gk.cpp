#include "core/algorithms/gk/evaluation/eval_gk.h"

#include <algorithm>
#include <utility>

namespace gk {

EvalGK::EvalGK(CompiledKey key) : key_(std::move(key)) {}

bool EvalGK::Evaluate(EvalGKInput const& input, EquivalenceRelation const& eq) {
    m_.Reset();
    m_.Bind(input.first_root, input.second_root, key_.descriptor.root_x);
    return SearchStep(0, input, m_, eq);
}

bool EvalGK::SearchStep(std::size_t step, EvalGKInput const& input, JointMapping& m,
                        EquivalenceRelation const& eq) const {
    if (step >= key_.traversal_order.size()) {
        return true;
    }

    auto const& [current_subject, predicate, current_object] =
            key_.descriptor.Q[key_.traversal_order[step]];

    bool subject_bound = m.IsBound(current_subject);
    bool object_bound = m.IsBound(current_object);

    // Case A
    if (subject_bound && object_bound) {
        auto const& [s1, s2] = m.Get(current_subject);
        auto const& [o1, o2] = m.Get(current_object);

        bool graph1_has_edge = std::any_of(
                input.first_graph.begin(), input.first_graph.end(), [&](auto const& triplet) {
                    return triplet.subject == s1 && triplet.predicate == predicate &&
                           triplet.object == o1;
                });

        bool graph2_has_edge = std::any_of(
                input.second_graph.begin(), input.second_graph.end(), [&](auto const& triplet) {
                    return triplet.subject == s2 && triplet.predicate == predicate &&
                           triplet.object == o2;
                });

        return (graph1_has_edge && graph2_has_edge) ? SearchStep(step + 1, input, m, eq) : false;
    }

    // Case B
    if (subject_bound && !object_bound) {
        auto const& [s1, s2] = m.Get(current_subject);

        for (auto const& [inner_s1, p1, o1] : input.first_graph) {
            if (inner_s1 != s1 || p1 != predicate) continue;
            for (auto const& [inner_s2, p2, o2] : input.second_graph) {
                if (inner_s2 != s2 || p2 != predicate) continue;

                if (IsFeasible(current_object, o1, o2, m, eq)) {
                    m.Bind(o1, o2, current_object);
                    if (SearchStep(step + 1, input, m, eq)) return true;
                    m.Unbind(current_object);
                }
            }
        }

        return false;
    }

    // Case C (reverse B)
    if (!subject_bound && object_bound) {
        auto const& [o1, o2] = m.Get(current_object);

        for (auto const& [s1, p1, inner_o1] : input.first_graph) {
            if (inner_o1 != o1 || p1 != predicate) continue;
            for (auto const& [s2, p2, inner_o2] : input.second_graph) {
                if (inner_o2 != o2 || p2 != predicate) continue;

                if (IsFeasible(current_subject, s1, s2, m, eq)) {
                    m.Bind(s1, s2, current_subject);
                    if (SearchStep(step + 1, input, m, eq)) return true;
                    m.Unbind(current_subject);
                }
            }
        }

        return false;
    }

    return false;
}

bool EvalGK::IsFeasible(model::Variable const& u, model::Node const& v1, model::Node const& v2,
                        JointMapping const& m, EquivalenceRelation const& eq) const {
    if (!m.IsInjective(u, v1, v2)) return false;

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
