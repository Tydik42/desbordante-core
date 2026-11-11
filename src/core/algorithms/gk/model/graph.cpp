#include "core/algorithms/gk/model/graph.h"

#include <unordered_set>

namespace gk {

using model::Graph;
using model::Node;
using model::NodeKind;

std::vector<Node> CollectEntities(Graph const& graph) {
    std::vector<Node> entities;
    std::unordered_set<std::string> seen_ids;
    auto add_entity = [&entities, &seen_ids](Node const& node) {
        if (node.kind != NodeKind::kEntity) return;
        if (seen_ids.insert(node.id_or_val).second) {
            entities.push_back(node);
        }
    };

    for (auto const& triple : graph) {
        add_entity(triple.subject);
        add_entity(triple.object);
    }
    return entities;
}

}  // namespace gk
