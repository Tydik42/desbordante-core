#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "core/algorithms/gk_compact/model/graph_view.h"
#include "core/algorithms/gk_compact/model/key.h"

namespace gk_compact {

// P^Q grouped by pattern node. offsets has key.nodes.size() + 1 elements,
// starts at zero and ends at pairs.size(). Each range is sorted and unique.
// Empty ranges are allowed. All pair components are original graph vertex IDs.
struct PairingRelation {
    std::vector<Offset> offsets{0};
    std::vector<VertexPair> pairs;

    std::span<VertexPair const> Candidates(PatternNodeId node) const {
        auto const first = offsets.at(node);
        auto const last = offsets.at(static_cast<std::size_t>(node) + 1);
        return std::span<VertexPair const>(pairs).subspan(first, last - first);
    }

    bool Contains(PatternNodeId node, VertexId first, VertexId second) const {
        return std::ranges::binary_search(Candidates(node), VertexPair{first, second});
    }
};

// Immutable preparation, built once per key. Borrows a stable GraphKey.
// Entity/wildcard nodes of the same type share a domain; value nodes share one
// domain, while constants have singleton domains. Entity domains form a sorted
// prefix. Incident edges do not depend on a pair of roots or Eq.
struct PairingPlan {
    GraphKey const& key;
    std::vector<PatternNode> domain_specs;
    std::vector<std::size_t> node_domains;
    std::vector<std::vector<std::size_t>> incident;
    std::size_t entity_domains = 0;
};

PairingPlan BuildPairingPlan(GraphKey const& key);
PairingPlan BuildPairingPlan(GraphKey&&) = delete;
PairingPlan BuildPairingPlan(GraphKey const&&) = delete;

// Sorted candidate IDs per shared domain, collected once per neighborhood.
// Borrows graph/selection storage through graph; both owners must stay alive.
// Domain indices are local to the plan used to build this object.
struct PairingDomain {
    GraphView graph;
    std::vector<Offset> offsets{0};
    std::vector<VertexId> vertices;

    std::span<VertexId const> Candidates(std::size_t domain) const {
        return std::span<VertexId const>(vertices).subspan(offsets[domain],
                                                           offsets[domain + 1] - offsets[domain]);
    }
};

PairingDomain BuildPairingDomain(PairingPlan const& plan, GraphView graph);

// Pair-specific fixed point, independent of Eq. Reuses buffers between calls,
// including calls for different keys; does not cache relations for root pairs.
// One instance per sequential execution, not shared between concurrent workers.
class PairingFilter {
public:
    // Inputs must have been prepared with plan. The result is borrowed and is
    // overwritten by the next Compute; consume it before reusing this filter.
    PairingRelation const& Compute(PairingPlan const& plan, PairingDomain const& first,
                                   PairingDomain const& second);

    PairingRelation TakeRelation() && {
        return std::move(relation_);
    }

private:
    enum class CandidateState : std::uint8_t { kRemoved, kLive, kQueued };

    PairingRelation relation_;
    std::vector<CandidateState> states_;
    std::vector<VertexId> common_values_;
    // A bounded ring: a live candidate occurs at most once in the queue.
    std::vector<Offset> queue_;
    std::vector<Offset> live_counts_;
    std::size_t queue_head_ = 0;
    std::size_t queue_tail_ = 0;
    std::size_t pending_ = 0;

    void Clear(std::size_t nodes);
    void Initialize(PairingPlan const& plan, PairingDomain const& first,
                    PairingDomain const& second);
    bool ContainsLive(PatternNodeId node, VertexPair pair) const;
    std::optional<Offset> FindCandidate(PatternNodeId node, VertexPair pair) const;
    void Enqueue(PatternNodeId node, VertexPair pair);
    void NotifyDependents(PairingPlan const& plan, PatternNodeId node, VertexPair pair,
                          GraphView first, GraphView second);
    bool HasSupport(VertexPair pair, PredicateId predicate, PatternNodeId neighbor, bool outgoing,
                    GraphView first, GraphView second) const;
};

// Greatest fixed point of typed/value-compatible pairs with two-sided edge
// support. Deliberately independent of Eq and injectivity, so Chase merges do
// not invalidate it. The caller tests Contains(key.root, first_root, second_root).
// Pairability is only necessary: a surviving pair still requires exact EvalGK.
PairingRelation ComputePairing(GraphKey const& key, GraphView first_neighborhood,
                               GraphView second_neighborhood);

}  // namespace gk_compact
