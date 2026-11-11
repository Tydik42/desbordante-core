#pragma once

#include <algorithm>
#include <ranges>

#include "core/algorithms/gk/model/graph.h"

namespace gk {

// Borrows a ready string graph, including a materialized neighborhood/reduction.
// The owner must outlive this view and every range obtained from it.
class GraphView {
public:
    GraphView(model::Graph const& graph) : graph_(&graph) {}

    GraphView(model::Graph&&) = delete;
    GraphView(model::Graph const&&) = delete;

    model::Graph const& Base() const noexcept {
        return *graph_;
    }

    auto begin() const {
        return graph_->begin();
    }

    auto end() const {
        return graph_->end();
    }

    auto Outgoing(model::Node const& vertex, model::PredicateId const& predicate) const {
        return *graph_ | std::views::filter([vertex, predicate](auto const& triple) {
            return triple.subject == vertex && triple.predicate == predicate;
        });
    }

    auto Incoming(model::Node const& vertex, model::PredicateId const& predicate) const {
        return *graph_ | std::views::filter([vertex, predicate](auto const& triple) {
            return triple.object == vertex && triple.predicate == predicate;
        });
    }

    bool HasEdge(model::Node const& subject, model::PredicateId const& predicate,
                 model::Node const& object) const {
        return std::ranges::any_of(*graph_, [&](auto const& triple) {
            return triple.subject == subject && triple.predicate == predicate &&
                   triple.object == object;
        });
    }

private:
    model::Graph const* graph_;
};

}  // namespace gk
