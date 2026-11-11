#include "core/algorithms/gk_compact/optimizations/neighborhood.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace gk_compact {

std::size_t GetPatternRadius(GraphKey const& key) {
    // GraphKey already guarantees connectivity and valid node IDs.
    std::vector<std::vector<PatternNodeId>> adjacent(key.nodes.size());
    for (auto const& triple : key.triples) {
        adjacent[triple.subject].push_back(triple.object);
        adjacent[triple.object].push_back(triple.subject);
    }
    std::vector<std::size_t> distances(key.nodes.size(), key.nodes.size());
    std::vector<PatternNodeId> queue{key.root};
    distances[key.root] = 0;
    std::size_t radius = 0;
    for (std::size_t head = 0; head < queue.size(); ++head) {
        auto const vertex = queue[head];
        for (auto neighbor : adjacent[vertex]) {
            if (distances[neighbor] != key.nodes.size()) continue;
            distances[neighbor] = distances[vertex] + 1;
            radius = std::max(radius, distances[neighbor]);
            queue.push_back(neighbor);
        }
    }
    assert(queue.size() == key.nodes.size());
    return radius;
}

Neighborhoods::Neighborhoods(Graph const& input, std::span<VertexId const> roots,
                             std::size_t radius)
    : graph_(&input) {
    offsets_.reserve(roots.size() + 1);
    if (roots.empty()) return;

    // One temporary bit per original vertex, NOT one mask per neighborhood.
    // Reset only touched bits between BFS runs; reuse both buffers for every root.
    std::vector<std::uint64_t> visited((input.Metadata().GetVertexCount() + 63) / 64, 0);
    std::vector<VertexId> queue;
    auto enqueue = [&](VertexId vertex) {
        auto& word = visited[vertex / 64];
        auto const bit = std::uint64_t{1} << (vertex % 64);
        if (word & bit) return;
        word |= bit;
        queue.push_back(vertex);
    };
    for (auto root : roots) {
        if (!input.ContainsVertex(root))
            throw std::invalid_argument("Compact GK neighborhood root is not an active vertex");
        queue.clear();
        enqueue(root);
        std::size_t first = 0;
        for (std::size_t depth = 0; depth < radius && first < queue.size(); ++depth) {
            auto const last = queue.size();
            for (std::size_t head = first; head < last; ++head) {
                auto const vertex = queue[head];
                for (auto const& edge : input.Outgoing(vertex)) enqueue(edge.neighbor);
                for (auto const& edge : input.Incoming(vertex)) enqueue(edge.neighbor);
            }
            first = last;
        }
        for (auto vertex : queue) visited[vertex / 64] &= ~(std::uint64_t{1} << (vertex % 64));
        std::ranges::sort(queue);
        vertices_.insert(vertices_.end(), queue.begin(), queue.end());
        offsets_.push_back(vertices_.size());
    }
}

}  // namespace gk_compact
