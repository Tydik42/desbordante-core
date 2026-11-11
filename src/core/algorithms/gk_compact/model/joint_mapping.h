#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

#include "core/algorithms/gk_compact/model/key.h"

namespace gk_compact {

// Fixed pattern-node domain; search operations reuse the constructor's buffer.
// Node IDs must be within that domain. Bind requires an unassigned node and
// original graph IDs (not Eq representatives); feasibility is checked by EvalGK.
class JointMapping {
public:
    using Mapping = std::pair<VertexId, VertexId>;

    explicit JointMapping(std::size_t node_count) : mapping_(node_count, kUnbound) {}

    void Reset() noexcept {
        std::ranges::fill(mapping_, kUnbound);
    }

    bool IsBound(PatternNodeId node) const noexcept {
        assert(node < mapping_.size());
        return mapping_[node].first != kUnboundVertex;
    }

    // Return the pair by value: callers do not borrow storage or depend on its
    // layout. This also permits two projection arrays without changing the API.
    Mapping Get(PatternNodeId node) const noexcept {
        assert(IsBound(node));
        return mapping_[node];
    }

    void Bind(PatternNodeId node, VertexId first, VertexId second) noexcept {
        assert(!IsBound(node));
        assert(first != kUnboundVertex && second != kUnboundVertex);
        mapping_[node] = {first, second};
    }

    void Unbind(PatternNodeId node) noexcept {
        assert(IsBound(node));
        mapping_[node] = kUnbound;
    }

    // Test a new assignment for an unbound node. Injectivity is independent in
    // the two projections: first may equal an existing second, and vice versa.
    bool IsInjective(VertexId first, VertexId second) const noexcept {
        assert(first != kUnboundVertex && second != kUnboundVertex);
        for (auto const& [mapped_first, mapped_second] : mapping_) {
            if (mapped_first == first || mapped_second == second) return false;
        }
        return true;
    }

private:
    static constexpr VertexId kUnboundVertex = std::numeric_limits<VertexId>::max();
    static constexpr Mapping kUnbound{kUnboundVertex, kUnboundVertex};

    std::vector<Mapping> mapping_;
};

}  // namespace gk_compact
