#include "core/algorithms/gk_compact/model/key.h"

#include <cassert>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "core/algorithms/gk/validation.h"

namespace gk_compact {
namespace {

TraversalOrder BuildTraversalOrder(CompiledKey const& key) {
    TraversalOrder order;
    order.reserve(key.triples.size());
    std::vector<std::uint8_t> reached(key.nodes.size(), 0);
    std::vector<std::uint8_t> used(key.triples.size(), 0);
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
        // CheckKey established connectivity before numeric preparation.
        assert(index < key.triples.size());
        used[index] = 1;
        reached[key.triples[index].subject] = 1;
        reached[key.triples[index].object] = 1;
        order.push_back(index);
    }
    return order;
}

}  // namespace

std::optional<CompiledKey> CompileKey(gk::model::GraphKey const& key, Graph const& graph) {
    gk::CheckKey(key);
    CompiledKey result;
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
    return result;
}

}  // namespace gk_compact
