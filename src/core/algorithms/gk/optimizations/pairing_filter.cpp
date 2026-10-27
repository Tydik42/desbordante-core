#include "core/algorithms/gk/optimizations/pairing_filter.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "core/util/logger.h"

namespace gk {
namespace {

using namespace model;

std::uint64_t Elapsed(std::chrono::steady_clock::time_point start) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                              std::chrono::steady_clock::now() - start)
                                              .count());
}

struct NodePair {
    Node first;
    Node second;
    bool operator==(NodePair const&) const = default;
};

struct PairHash {
    std::size_t operator()(NodePair const& pair) const {
        auto const hash = std::hash<Node>{}(pair.first);
        return hash ^ (std::hash<Node>{}(pair.second) + (hash << 6U) + (hash >> 2U));
    }
};

using NodeSet = std::unordered_set<Node>;
using Relation = std::unordered_set<NodePair, PairHash>;
using Adjacency = std::unordered_map<Node, std::unordered_map<PredicateId, std::vector<Node>>>;

struct GraphIndex {
    Graph const* graph;
    NodeSet nodes;
    Adjacency outgoing;
    Adjacency incoming;
};

GraphIndex IndexGraph(Graph const& graph) {
    GraphIndex result;
    result.graph = &graph;
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

bool HasSupport(NodePair const& pair, PredicateId const& predicate, Adjacency const& first,
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

Graph Reduce(Graph graph, NodeSet const& retained) {
    std::erase_if(graph, [&](Triple const& triple) {
        return !retained.contains(triple.subject) || !retained.contains(triple.object);
    });
    return graph;
}

}  // namespace

PairingFilter::PairingFilter(model::GraphKey key) : key_(std::move(key)) {
    auto index_of = [this](model::Variable const& variable) {
        auto const found = std::find(variables_.begin(), variables_.end(), variable);
        if (found != variables_.end()) return static_cast<std::size_t>(found - variables_.begin());
        variables_.push_back(variable);
        return variables_.size() - 1;
    };
    for (auto const& triple : key_.Q) {
        auto const subject = index_of(triple.subject);
        auto const object = index_of(triple.object);
        endpoints_.emplace_back(subject, object);
    }
    root_index_ = index_of(key_.root_x);
}

PairingResult PairingFilter::Prepare(model::Graph const& first_graph,
                                     model::Graph const& second_graph,
                                     model::Node const& first_root, model::Node const& second_root,
                                     bool reduce, EvaluationStatistics& statistics) const {
    auto const pairing_started = std::chrono::steady_clock::now();
    auto const first = IndexGraph(first_graph);
    auto const second = IndexGraph(second_graph);
    ++statistics.pairing_check_count;
    std::vector<Relation> relation(variables_.size());
    for (std::size_t u = 0; u < variables_.size(); ++u) {
        for (auto const& first_node : first.nodes) {
            for (auto const& second_node : second.nodes) {
                if (Compatible(variables_[u], first_node, second_node, key_))
                    relation[u].insert({first_node, second_node});
            }
        }
    }
    // Greatest fixed point of the structural support constraints. Unlike EvalGK,
    // this relaxation imposes neither injectivity nor current entity equivalence.
    bool changed;
    do {
        changed = false;
        for (std::size_t u = 0; u < variables_.size(); ++u) {
            for (auto it = relation[u].begin(); it != relation[u].end();) {
                bool supported = true;
                for (std::size_t t = 0; t < endpoints_.size() && supported; ++t) {
                    auto const [subject, object] = endpoints_[t];
                    auto const& predicate = key_.Q[t].predicate;
                    if (subject == u)
                        supported = HasSupport(*it, predicate, first.outgoing, second.outgoing,
                                               relation[object]);
                    if (supported && object == u)
                        supported = HasSupport(*it, predicate, first.incoming, second.incoming,
                                               relation[subject]);
                }
                if (supported) {
                    ++it;
                } else {
                    it = relation[u].erase(it);
                    changed = true;
                }
            }
        }
    } while (changed && relation[root_index_].contains({first_root, second_root}));
    PairingResult result;
    result.pairable = relation[root_index_].contains({first_root, second_root});
    auto const pairing_elapsed = Elapsed(pairing_started);
    statistics.pairing_ns += pairing_elapsed;
    LOG_TRACE("GK pairing: roots ('{}', '{}'), pairable={}, {} ns", first_root.id_or_val,
              second_root.id_or_val, result.pairable, pairing_elapsed);
    if (!result.pairable) {
        ++statistics.filtered_pair_count;
        LOG_TRACE("GK pairing rejected roots ('{}', '{}')", first_root.id_or_val,
                  second_root.id_or_val);
        return result;
    }
    if (reduce) {
        auto const reduction_started = std::chrono::steady_clock::now();
        NodeSet retained_first;
        NodeSet retained_second;
        for (auto const& candidates : relation) {
            for (auto const& pair : candidates) {
                retained_first.insert(pair.first);
                retained_second.insert(pair.second);
            }
        }
        result.first_graph = Reduce(*first.graph, retained_first);
        result.second_graph = Reduce(*second.graph, retained_second);
        statistics.reduced_triple_count += result.first_graph.size() + result.second_graph.size();
        auto const reduction_elapsed = Elapsed(reduction_started);
        statistics.reduction_ns += reduction_elapsed;
        LOG_TRACE("GK reduction: {} and {} triples, {} ns", result.first_graph.size(),
                  result.second_graph.size(), reduction_elapsed);
    }
    return result;
}

}  // namespace gk
