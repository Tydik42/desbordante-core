#pragma once

#include <algorithm>
#include <span>
#include <unordered_map>
#include <vector>

#include "core/algorithms/gk/model/graph_view.h"
#include "core/algorithms/gk/model/key.h"

namespace gk {

struct PairingRelation {
    std::unordered_map<model::Variable, std::vector<VertexPair>> pairs;

    std::span<VertexPair const> Candidates(model::Variable const& node) const {
        auto const found = pairs.find(node);
        return found == pairs.end() ? std::span<VertexPair const>{} : found->second;
    }

    bool Contains(model::Variable const& node, model::Node const& first,
                  model::Node const& second) const {
        auto candidates = Candidates(node);
        return std::ranges::find(candidates, VertexPair{first, second}) != candidates.end();
    }
};

// Greatest structural-support relation; independent of Eq and injectivity.
PairingRelation ComputePairing(GraphKey const& key, GraphView first, GraphView second);

}  // namespace gk
