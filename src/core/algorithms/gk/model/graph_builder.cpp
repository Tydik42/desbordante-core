#include "core/algorithms/gk/model/graph_builder.h"

#include <stdexcept>
#include <utility>

#include "core/algorithms/gk/model/validation.h"

namespace gk {

void GraphBuilder::AddTriple(model::Triple const& triple) {
    CheckTriple(triple, graph_.size());
    auto const& subject = triple.subject;
    auto const& object = triple.object;
    if (object.kind == model::NodeKind::kEntity && subject.id_or_val == object.id_or_val &&
        subject.type != object.type)
        throw std::invalid_argument("GK graph: subject and object disagree on entity type");
    for (auto const* node : {&subject, &object}) {
        if (node->kind != model::NodeKind::kEntity) continue;
        auto const found = types_.find(node->id_or_val);
        if (found != types_.end() && found->second != node->type)
            throw std::invalid_argument("GK graph: entity " + node->id_or_val +
                                        " has inconsistent types");
    }
    for (auto const* node : {&subject, &object})
        if (node->kind == model::NodeKind::kEntity) types_.emplace(node->id_or_val, node->type);
    graph_.push_back(triple);
}

model::Graph GraphBuilder::Build() && {
    auto result = std::move(graph_);
    *this = GraphBuilder{};
    return result;
}

}  // namespace gk
