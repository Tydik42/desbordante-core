#include "core/algorithms/gk/optimizations/subgraph_reduction.h"

#include <unordered_set>

namespace gk {

model::Graph ReduceSubgraph(GraphView neighborhood, PairingRelation const& pairing,
                            PairingProjection projection) {
    std::unordered_set<model::Node> retained;
    for (auto const& [variable, candidates] : pairing.pairs)
        for (auto const& pair : candidates)
            retained.insert(projection == PairingProjection::kFirst ? pair.first : pair.second);
    model::Graph result;
    for (auto const& triple : neighborhood)
        if (retained.contains(triple.subject) && retained.contains(triple.object))
            result.push_back(triple);
    return result;
}

}  // namespace gk
