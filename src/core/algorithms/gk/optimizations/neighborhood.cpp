#include "core/algorithms/gk/optimizations/neighborhood.h"

#include <algorithm>
#include <cassert>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace gk {

std::size_t GetPatternRadius(CompiledKey const& compiled) {
    auto const& key = compiled.descriptor;
    std::unordered_map<model::Variable, std::size_t> indices;
    std::vector<std::vector<std::size_t>> adjacency;
    auto intern = [&](model::Variable const& variable) {
        auto [it, inserted] = indices.emplace(variable, indices.size());
        if (inserted) adjacency.emplace_back();
        return it->second;
    };
    auto const root = intern(key.root_x);
    for (auto const& triple : key.Q) {
        auto const subject = intern(triple.subject), object = intern(triple.object);
        adjacency[subject].push_back(object);
        adjacency[object].push_back(subject);
    }
    std::vector<std::size_t> distances(indices.size(), indices.size());
    std::vector<std::size_t> queue{root};
    distances[root] = 0;
    std::size_t radius = 0;
    for (std::size_t head = 0; head < queue.size(); ++head) {
        auto const vertex = queue[head];
        for (auto next : adjacency[vertex]) {
            if (distances[next] != indices.size()) continue;
            distances[next] = distances[vertex] + 1;
            radius = std::max(radius, distances[next]);
            queue.push_back(next);
        }
    }
    assert(queue.size() == indices.size());
    return radius;
}

Neighborhoods::Neighborhoods(model::Graph const& graph, std::span<model::Node const* const> roots,
                             std::size_t radius) {
    graphs_.reserve(roots.size());
    for (auto const* root : roots) {
        std::unordered_set<model::Node> visited{*root}, frontier{*root};
        for (std::size_t depth = 0; depth < radius && !frontier.empty(); ++depth) {
            std::unordered_set<model::Node> next;
            for (auto const& triple : graph) {
                if (frontier.contains(triple.subject) && !visited.contains(triple.object))
                    next.insert(triple.object);
                if (frontier.contains(triple.object) && !visited.contains(triple.subject))
                    next.insert(triple.subject);
            }
            visited.insert(next.begin(), next.end());
            frontier = std::move(next);
        }
        auto& selected = graphs_.emplace_back();
        for (auto const& triple : graph)
            if (visited.contains(triple.subject) && visited.contains(triple.object))
                selected.push_back(triple);
    }
}

}  // namespace gk
