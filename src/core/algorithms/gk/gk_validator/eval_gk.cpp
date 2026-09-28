#include "core/algorithms/gk/gk_validator/eval_gk.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/algorithms/gk/graph_descriptor.h"
#include "core/algorithms/gk/joint_mapping.h"
#include "core/util/logger.h"

namespace gk {

EvalGK::EvalGK(model::GraphKey key)
    : key_(std::move(key)), traversal_order_(BuildTraversalOrder(key_)) {
    LOG_DEBUG("Initialized EvalGK: {} pattern triples, {} traversal steps", key_.Q.size(),
              traversal_order_.size());
    for (std::size_t step = 0; step < traversal_order_.size(); ++step) {
        LOG_TRACE("EvalGK traversal step {} uses pattern triple {}", step, traversal_order_[step]);
    }
}

TraversalOrder EvalGK::BuildTraversalOrder(model::GraphKey const& key) {
    if (key.Q.empty()) {
        LOG_ERROR("Cannot build GK traversal order for an empty pattern");
        throw std::invalid_argument("EvalGK pattern must contain at least one triple");
    }

    TraversalOrder order;
    order.reserve(key.Q.size());

    std::unordered_set<std::size_t> used;
    used.reserve(key.Q.size());
    std::vector<model::Variable> reached{key.root_x};

    while (order.size() < key.Q.size()) {
        std::size_t next_index = key.Q.size();

        for (std::size_t index = 0; index < key.Q.size(); ++index) {
            if (used.contains(index)) continue;

            auto const& triple = key.Q[index];
            bool const subject_reached =
                    std::find(reached.begin(), reached.end(), triple.subject) != reached.end();
            bool const object_reached =
                    std::find(reached.begin(), reached.end(), triple.object) != reached.end();
            if (subject_reached || object_reached) {
                next_index = index;
                break;
            }
        }

        if (next_index == key.Q.size()) {
            LOG_ERROR("Cannot build GK traversal order: pattern is disconnected after {} triples",
                      order.size());
            throw std::invalid_argument("EvalGK pattern is disconnected from the root");
        }

        used.insert(next_index);
        order.push_back(next_index);

        auto const& triple = key.Q[next_index];
        if (std::find(reached.begin(), reached.end(), triple.subject) == reached.end()) {
            reached.push_back(triple.subject);
        }
        if (std::find(reached.begin(), reached.end(), triple.object) == reached.end()) {
            reached.push_back(triple.object);
        }
    }

    return order;
}

bool EvalGK::Evaluate(EvalGKInput const& input, EquivalenceRelation const& eq) const {
    auto const evaluate_started_at = std::chrono::steady_clock::now();
    auto const root_type = key_.T_Q.find(key_.root_x);
    if (root_type == key_.T_Q.end() || input.first_root.kind != model::NodeKind::kEntity ||
        input.second_root.kind != model::NodeKind::kEntity ||
        input.first_root.type != root_type->second || input.second_root.type != root_type->second) {
        LOG_TRACE("EvalGK rejected root pair ('{}', '{}') due to invalid root kind or type",
                  input.first_root.id_or_val, input.second_root.id_or_val);
        return false;
    }

    LOG_TRACE("EvalGK started for roots ('{}', '{}'), Eq size={}", input.first_root.id_or_val,
              input.second_root.id_or_val, eq.GetRelation().size());
    JointMapping m;
    m.Bind(input.first_root, input.second_root, key_.root_x);

    bool const result = SearchStep(0, input, m, eq);
    auto const evaluate_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - evaluate_started_at);
    LOG_TRACE("EvalGK finished for roots ('{}', '{}'): result={}, elapsed={} us",
              input.first_root.id_or_val, input.second_root.id_or_val, result,
              evaluate_elapsed.count());
    return result;
}

bool EvalGK::SearchStep(std::size_t step, EvalGKInput const& input, JointMapping& m,
                        EquivalenceRelation const& eq) const {
    if (step >= traversal_order_.size()) {
        LOG_TRACE("EvalGK found a complete embedding");
        return true;
    }

    auto const& [current_subject, predicate, current_object] = key_.Q[traversal_order_[step]];

    bool subject_bound = m.IsBound(current_subject);
    bool object_bound = m.IsBound(current_object);
    LOG_TRACE("EvalGK search step {}: pattern triple {}, subject_bound={}, object_bound={}", step,
              traversal_order_[step], subject_bound, object_bound);

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

            auto const type_it = key_.T_Q.find(u);
            if (type_it == key_.T_Q.end()) return false;

            if (v1.type != type_it->second || v2.type != type_it->second) return false;

            return u.kind == model::VarKind::kWildcard || u == key_.root_x ||
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
