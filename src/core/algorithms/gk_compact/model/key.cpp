#include "core/algorithms/gk_compact/model/key.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "core/util/logger.h"

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
#include <chrono>
#endif

namespace gk_compact {
namespace {

std::size_t ComputeRadius(GraphKey const& key) {
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

TraversalOrder BuildTraversalOrder(GraphKey& key) {
    TraversalOrder order;
    order.reserve(key.triples.size());
    std::vector<std::uint8_t> reached(key.nodes.size(), 0);
    std::vector<std::uint8_t> used(key.triples.size(), 0);
    std::vector<std::size_t> depths(key.nodes.size(), 0);
    reached[key.root] = 1;
    while (order.size() < key.triples.size()) {
        std::size_t index = 0;
        // Check all constraints between reached nodes before expanding again.
        // This also schedules root/self loops as soon as their node is reached.
        for (; index < key.triples.size(); ++index) {
            auto const& triple = key.triples[index];
            if (!used[index] && reached[triple.subject] && reached[triple.object]) break;
        }
        if (index == key.triples.size()) {
            index = 0;
            for (; index < key.triples.size(); ++index) {
                auto const& triple = key.triples[index];
                if (!used[index] && (reached[triple.subject] || reached[triple.object])) break;
            }
        }
        // The descriptor check established connectivity before numeric preparation.
        assert(index < key.triples.size());
        used[index] = 1;
        auto const& triple = key.triples[index];
        if (!reached[triple.subject]) depths[triple.subject] = depths[triple.object] + 1;
        if (!reached[triple.object]) depths[triple.object] = depths[triple.subject] + 1;
        key.expansion_depth =
                std::max({key.expansion_depth, depths[triple.subject], depths[triple.object]});
        reached[key.triples[index].subject] = 1;
        reached[key.triples[index].object] = 1;
        order.push_back(index);
    }
    return order;
}

}  // namespace

std::optional<GraphKey> BuildGraphKey(gk::model::GraphKeyDescriptor const& key,
                                      Graph const& graph) {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    bool const trace = util::logging::Initialize()->should_log(spdlog::level::trace);
    auto const started =
            trace ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
#endif
    key.Check();
    auto build = [&]() -> std::optional<GraphKey> {
        GraphKey result;
        std::unordered_map<gk::model::Variable, PatternNodeId> indices;
        auto intern = [&](gk::model::Variable const& variable) -> std::optional<PatternNodeId> {
            if (auto const it = indices.find(variable); it != indices.end()) return it->second;
            PatternNode node{variable.kind, kNoType, std::nullopt};
            switch (variable.kind) {
                case gk::model::VarKind::kEntity:
                case gk::model::VarKind::kWildcard: {
                    auto const type = graph.FindType(key.T_Q.at(variable));
                    if (!type) return std::nullopt;
                    node.type = *type;
                    break;
                }
                case gk::model::VarKind::kConstant:
                    node.constant = graph.FindValue(variable.name);
                    if (!node.constant) return std::nullopt;
                    break;
                case gk::model::VarKind::kValue:
                    break;
            }
            if (result.nodes.size() >= std::numeric_limits<PatternNodeId>::max()) {
                throw std::length_error("Compact GK key exhausted its 32-bit node ID space");
            }
            auto const id = static_cast<PatternNodeId>(result.nodes.size());
            result.nodes.push_back(node);
            indices.emplace(variable, id);
            return id;
        };

        auto const root = intern(key.root_x);
        if (!root) return std::nullopt;
        result.root = *root;
        result.triples.reserve(key.Q.size());
        for (auto const& triple : key.Q) {
            auto const predicate = graph.FindPredicate(triple.predicate);
            if (!predicate) return std::nullopt;
            auto const subject = intern(triple.subject);
            auto const object = intern(triple.object);
            if (!subject || !object) return std::nullopt;
            result.triples.push_back({*subject, *predicate, *object});
        }
        result.traversal_order = BuildTraversalOrder(result);
        result.radius = ComputeRadius(result);
        return result;
    };
    auto result = build();
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    if (trace)
        LOG_TRACE(
                "Compact GK phase 'key_preparation': {} ns, present={}, radius={}, "
                "expansion_depth={}",
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - started)
                        .count(),
                result.has_value(), result ? result->radius : 0,
                result ? result->expansion_depth : 0);
#endif
    return result;
}

}  // namespace gk_compact
