#include "core/algorithms/gk/model/graph_builder.h"

#include <stdexcept>
#include <string>
#include <utility>

#include <magic_enum/magic_enum.hpp>

#include "core/util/logger.h"

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
#include <chrono>
#endif

namespace gk {
namespace {

using model::Node;
using model::NodeKind;

[[noreturn]] void ThrowInvalid(std::string const& message) {
    LOG_ERROR("Invalid graph descriptor: {}", message);
    throw std::invalid_argument("Invalid graph descriptor: " + message);
}

void CheckNode(Node const& node, std::string const& location) {
    if (node.id_or_val.empty()) {
        ThrowInvalid(location + " has an empty identifier or value");
    }

    if (!magic_enum::enum_contains(node.kind)) {
        ThrowInvalid(location + " has an unknown node kind");
    }
    if (node.kind == NodeKind::kEntity && (node.type.empty() || node.type == "-")) {
        ThrowInvalid(location + " entity type must be nonempty and different from '-'");
    }
    if (node.kind == NodeKind::kValue && !node.type.empty()) {
        ThrowInvalid(location + " value must not have an entity type");
    }
}

void RegisterEntityType(std::unordered_map<std::string, std::string>& entity_types,
                        Node const& entity, std::string const& location) {
    auto [it, inserted] = entity_types.emplace(entity.id_or_val, entity.type);
    if (!inserted && it->second != entity.type) {
        ThrowInvalid("entity " + entity.id_or_val + " has inconsistent types at " + location);
    }
}

}  // namespace

void CheckTriple(model::Triple const& triple, std::size_t index) {
    std::string const prefix = "graph triple " + std::to_string(index);
    CheckNode(triple.subject, prefix + " subject");
    if (triple.subject.kind != NodeKind::kEntity) {
        ThrowInvalid(prefix + " subject must be an entity");
    }
    if (triple.predicate.empty()) ThrowInvalid(prefix + " has an empty predicate");
    CheckNode(triple.object, prefix + " object");
}

void CheckGraph(model::Graph const& graph) {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    bool const graph_check_trace = util::logging::Initialize()->should_log(spdlog::level::trace);
    auto const graph_check_started = graph_check_trace ? std::chrono::steady_clock::now()
                                                       : std::chrono::steady_clock::time_point{};
#endif

    LOG_DEBUG("Validating GK graph descriptor: {} triples", graph.size());

    std::unordered_map<std::string, std::string> entity_types;
    std::size_t value_nodes = 0;
    for (std::size_t index = 0; index < graph.size(); ++index) {
        gk::model::Triple const& triple = graph[index];
        std::string const prefix = "graph triple " + std::to_string(index);

        CheckTriple(triple, index);
        RegisterEntityType(entity_types, triple.subject, prefix + " subject");
        if (triple.object.kind == NodeKind::kEntity) {
            RegisterEntityType(entity_types, triple.object, prefix + " object");
        } else {
            ++value_nodes;
        }
    }

    LOG_DEBUG("GK graph descriptor is valid: {} distinct entities, {} value nodes",
              entity_types.size(), value_nodes);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    if (graph_check_trace)
        LOG_TRACE("GK phase 'graph_validation': {} ns",
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - graph_check_started)
                          .count());
#endif
}

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
