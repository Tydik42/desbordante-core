#include "core/algorithms/gk/optimizations/pairing_filter.h"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace gk {
namespace {

using namespace model;

struct PairHash {
    std::size_t operator()(VertexPair const& pair) const {
        auto const hash = std::hash<Node>{}(pair.first);
        return hash ^ (std::hash<Node>{}(pair.second) + (hash << 6U) + (hash >> 2U));
    }
};

using NodeSet = std::unordered_set<Node>;
using Relation = std::unordered_set<VertexPair, PairHash>;
using Adjacency = std::unordered_map<Node, std::unordered_map<PredicateId, std::vector<Node>>>;

struct GraphIndex {
    NodeSet nodes;
    Adjacency outgoing;
    Adjacency incoming;
};

GraphIndex IndexGraph(Graph const& graph) {
    GraphIndex result;
    for (auto const& triple : graph) {
        result.nodes.insert(triple.subject);
        result.nodes.insert(triple.object);
        result.outgoing[triple.subject][triple.predicate].push_back(triple.object);
        result.incoming[triple.object][triple.predicate].push_back(triple.subject);
    }
    return result;
}

bool Compatible(Variable const& variable, Node const& first, Node const& second,
                GraphKey const& key) {
    switch (variable.kind) {
        case VarKind::kEntity:
        case VarKind::kWildcard:
            return first.kind == NodeKind::kEntity && second.kind == NodeKind::kEntity &&
                   first.type == key.T_Q.at(variable) && second.type == first.type;
        case VarKind::kValue:
            return first.kind == NodeKind::kValue && second.kind == NodeKind::kValue &&
                   first == second;
        case VarKind::kConstant:
            return first.kind == NodeKind::kValue && second.kind == NodeKind::kValue &&
                   first.id_or_val == variable.name && second.id_or_val == variable.name;
    }
    return false;
}

bool HasSupport(VertexPair const& pair, PredicateId const& predicate, Adjacency const& first,
                Adjacency const& second, Relation const& neighbors) {
    auto const first_node = first.find(pair.first);
    auto const second_node = second.find(pair.second);
    if (first_node == first.end() || second_node == second.end()) return false;
    auto const first_edges = first_node->second.find(predicate);
    auto const second_edges = second_node->second.find(predicate);
    if (first_edges == first_node->second.end() || second_edges == second_node->second.end())
        return false;
    for (auto const& first_neighbor : first_edges->second) {
        for (auto const& second_neighbor : second_edges->second) {
            if (neighbors.contains({first_neighbor, second_neighbor})) return true;
        }
    }
    return false;
}

}  // namespace

PairingRelation ComputePairing(CompiledKey const& compiled, GraphView first_graph,
                               GraphView second_graph) {
    auto const& key = compiled.descriptor;
    std::vector<Variable> variables;
    std::vector<std::pair<std::size_t, std::size_t>> endpoints;
    auto index_of = [&](Variable const& variable) {
        auto const found = std::ranges::find(variables, variable);
        if (found != variables.end()) return static_cast<std::size_t>(found - variables.begin());
        variables.push_back(variable);
        return variables.size() - 1;
    };
    for (auto const& triple : key.Q) {
        auto const subject = index_of(triple.subject);
        auto const object = index_of(triple.object);
        endpoints.emplace_back(subject, object);
    }
    auto const first = IndexGraph(first_graph.Base());
    auto const second = IndexGraph(second_graph.Base());
    std::vector<Relation> relation(variables.size());
    for (std::size_t u = 0; u < variables.size(); ++u)
        for (auto const& left : first.nodes)
            for (auto const& right : second.nodes)
                if (Compatible(variables[u], left, right, key)) relation[u].insert({left, right});
    bool changed;
    do {
        changed = false;
        for (std::size_t u = 0; u < variables.size(); ++u) {
            for (auto it = relation[u].begin(); it != relation[u].end();) {
                bool supported = true;
                for (std::size_t t = 0; t < endpoints.size() && supported; ++t) {
                    auto const [subject, object] = endpoints[t];
                    auto const& predicate = key.Q[t].predicate;
                    if (subject == u)
                        supported = HasSupport(*it, predicate, first.outgoing, second.outgoing,
                                               relation[object]);
                    if (supported && object == u)
                        supported = HasSupport(*it, predicate, first.incoming, second.incoming,
                                               relation[subject]);
                }
                if (supported)
                    ++it;
                else {
                    it = relation[u].erase(it);
                    changed = true;
                }
            }
        }
    } while (changed);
    PairingRelation result;
    for (std::size_t u = 0; u < variables.size(); ++u)
        result.pairs.emplace(variables[u],
                             std::vector<VertexPair>(relation[u].begin(), relation[u].end()));
    return result;
}

}  // namespace gk
