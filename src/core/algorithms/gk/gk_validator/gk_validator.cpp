#include "core/algorithms/gk/gk_validator/gk_validator.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <magic_enum/magic_enum.hpp>

#include "core/algorithms/gk/equivalence_relation.h"
#include "core/algorithms/gk/gk_validator/eval_gk.h"
#include "core/algorithms/gk/parser/gk_graph_parser.h"
#include "core/config/names_and_descriptions.h"
#include "core/util/logger.h"

namespace {

using gk::model::Graph;
using gk::model::Node;
using gk::model::NodeKind;
using gk::model::Pattern;
using gk::model::PatternTriple;
using gk::model::Variable;
using gk::model::VarKind;

std::uint64_t ElapsedNanoseconds(std::chrono::steady_clock::time_point started_at) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                              std::chrono::steady_clock::now() - started_at)
                                              .count());
}

[[noreturn]] void ThrowInvalid(std::string const& message) {
    LOG_ERROR("Invalid graph-key descriptor: {}", message);
    throw std::invalid_argument("Invalid graph-key descriptor: " + message);
}

void CheckNode(Node const& node, std::string const& location) {
    if (node.id_or_val.empty()) {
        ThrowInvalid(location + " has an empty identifier or value");
    }

    if (!magic_enum::enum_contains(node.kind)) {
        ThrowInvalid(location + " has an unknown node kind");
    }
    if (node.kind == NodeKind::kEntity && node.type.empty()) {
        ThrowInvalid(location + " entity has an empty type");
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

void CheckVariable(Variable const& variable, std::string const& location) {
    if (variable.name.empty()) ThrowInvalid(location + " has an empty name");

    if (!magic_enum::enum_contains(variable.kind)) {
        ThrowInvalid(location + " has an unknown variable kind");
    }
}

void AddPatternVariable(std::vector<Variable>& variables, Variable const& variable,
                        std::string const& location) {
    CheckVariable(variable, location);

    if (std::find(variables.begin(), variables.end(), variable) == variables.end()) {
        variables.push_back(variable);
    }
}

std::vector<Variable> ValidatePattern(Pattern const& pattern) {
    if (pattern.empty()) ThrowInvalid("pattern must contain at least one triple");

    std::vector<Variable> variables;
    for (std::size_t index = 0; index < pattern.size(); ++index) {
        PatternTriple const& triple = pattern[index];
        bool const is_duplicate =
                std::any_of(pattern.begin(), pattern.begin() + static_cast<std::ptrdiff_t>(index),
                            [&triple](PatternTriple const& previous) {
                                return previous.subject == triple.subject &&
                                       previous.predicate == triple.predicate &&
                                       previous.object == triple.object;
                            });
        if (is_duplicate) {
            ThrowInvalid("pattern contains a duplicate triple at index " + std::to_string(index));
        }
        if (triple.predicate.empty()) {
            ThrowInvalid("pattern triple " + std::to_string(index) + " has an empty predicate");
        }

        if (triple.subject.kind != VarKind::kEntity && triple.subject.kind != VarKind::kWildcard) {
            ThrowInvalid("pattern triple " + std::to_string(index) +
                         " subject must be an entity variable or wildcard");
        }

        AddPatternVariable(variables, triple.subject,
                           "pattern triple " + std::to_string(index) + " subject");
        AddPatternVariable(variables, triple.object,
                           "pattern triple " + std::to_string(index) + " object");
    }
    return variables;
}

std::size_t FindVariable(std::vector<Variable> const& variables, Variable const& needle) {
    auto it = std::find(variables.begin(), variables.end(), needle);
    if (it == variables.end()) ThrowInvalid("internal pattern variable lookup failed");
    return static_cast<std::size_t>(std::distance(variables.begin(), it));
}

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

}  // namespace

namespace algos {

void GkValidator::ResetState() {
    LOG_TRACE("Resetting GK validation result");
    result_.holds = true;
    result_.violations.clear();

    timing_stats_.execute_prepare_ns = 0;
    timing_stats_.execute_eval_ns = 0;
    timing_stats_.execute_finalize_ns = 0;
    timing_stats_.execute_total_ns = 0;
    timing_stats_.graph_entity_count = 0;
    timing_stats_.root_candidate_count = 0;
    timing_stats_.root_pair_count = 0;
    timing_stats_.checked_pair_count = 0;
    timing_stats_.violation_count = 0;
}

void GkValidator::RegisterOptions() {
    using namespace config::descriptions;
    using namespace config::names;

    RegisterOption(config::Option{&graph_, kGkGraphData, kDGkGraphData}.SetIsRequiredFunc(
            [this] { return !OptionIsSet(kGkGraphPath); }));
    RegisterOption(config::Option{&graph_path_, kGkGraphPath, kDGkGraphPath}.SetIsRequiredFunc(
            [this] { return !OptionIsSet(kGkGraphData); }));
    RegisterOption(config::Option{&key_, kGkKeyData, kDGkKeyData});
    RegisterOption(
            config::Option{&use_pairing_filter_, kGkUsePairingFilter, kDGkUsePairingFilter, false});
    RegisterOption(config::Option{&use_subgraph_reduction_, kGkUseSubgraphReduction,
                                  kDGkUseSubgraphReduction, false});
}

GkValidator::GkValidator() : Algorithm() {
    RegisterOptions();
    MakeOptionsAvailable(
            {config::names::kGkGraphData, config::names::kGkGraphPath, config::names::kGkKeyData});
}

void GkValidator::MakeExecuteOptsAvailable() {
    MakeOptionsAvailable(
            {config::names::kGkUsePairingFilter, config::names::kGkUseSubgraphReduction});
}

void GkValidator::CheckGraph(gk::model::Graph const& graph) {
    LOG_DEBUG("Validating GK graph descriptor: {} triples", graph.size());

    std::unordered_map<std::string, std::string> entity_types;
    std::size_t value_nodes = 0;
    for (std::size_t index = 0; index < graph.size(); ++index) {
        gk::model::Triple const& triple = graph[index];
        std::string const prefix = "graph triple " + std::to_string(index);

        CheckNode(triple.subject, prefix + " subject");
        if (triple.subject.kind != NodeKind::kEntity) {
            ThrowInvalid(prefix + " subject must be an entity");
        }
        RegisterEntityType(entity_types, triple.subject, prefix + " subject");

        if (triple.predicate.empty()) ThrowInvalid(prefix + " has an empty predicate");

        CheckNode(triple.object, prefix + " object");
        if (triple.object.kind == NodeKind::kEntity) {
            RegisterEntityType(entity_types, triple.object, prefix + " object");
        } else {
            ++value_nodes;
        }
    }

    LOG_DEBUG("GK graph descriptor is valid: {} distinct entities, {} value nodes",
              entity_types.size(), value_nodes);
}

void GkValidator::CheckKey(gk::model::GraphKey const& key) {
    LOG_DEBUG("Validating GK key descriptor: {} pattern triples, {} type entries, root='{}'",
              key.Q.size(), key.T_Q.size(), key.root_x.name);

    std::vector<Variable> const variables = ValidatePattern(key.Q);
    if (key.root_x.name.empty()) ThrowInvalid("key root must not be empty");
    if (key.root_x.kind != VarKind::kEntity) {
        ThrowInvalid("key root must be an entity variable");
    }

    Variable const* root = nullptr;
    for (Variable const& variable : variables) {
        if (variable == key.root_x) {
            root = &variable;
            break;
        }
    }
    if (root == nullptr) ThrowInvalid("key root must be an entity variable in the pattern");

    std::unordered_set<Variable> typed_variables;
    for (Variable const& variable : variables) {
        bool const requires_type =
                variable.kind == VarKind::kEntity || variable.kind == VarKind::kWildcard;
        auto it = key.T_Q.find(variable);
        if (requires_type) {
            if (it == key.T_Q.end() || it->second.empty()) {
                ThrowInvalid("missing type for pattern variable " + variable.name);
            }
            typed_variables.insert(variable);
        }
    }

    for (auto const& [variable, type] : key.T_Q) {
        CheckVariable(variable, "key type map variable");
        if (type.empty()) {
            ThrowInvalid("empty type for pattern variable " + variable.name);
        }
        if (variable.kind != VarKind::kEntity && variable.kind != VarKind::kWildcard) {
            ThrowInvalid("type is provided for a non-entity pattern variable " + variable.name);
        }
        if (!typed_variables.contains(variable)) {
            ThrowInvalid("type is provided for a variable outside the pattern " + variable.name);
        }
    }

    std::vector<std::vector<std::size_t>> adjacency(variables.size());
    for (PatternTriple const& triple : key.Q) {
        std::size_t const subject = FindVariable(variables, triple.subject);
        std::size_t const object = FindVariable(variables, triple.object);
        adjacency[subject].push_back(object);
        adjacency[object].push_back(subject);
    }

    std::size_t const root_index = FindVariable(variables, *root);
    std::unordered_set<std::size_t> reached;
    reached.reserve(variables.size());
    std::deque<std::size_t> pending;
    reached.insert(root_index);
    pending.push_back(root_index);
    while (!pending.empty()) {
        std::size_t const current = pending.front();
        pending.pop_front();
        for (std::size_t next : adjacency[current]) {
            if (reached.contains(next)) continue;
            reached.insert(next);
            pending.push_back(next);
        }
    }

    if (reached.size() != variables.size()) {
        ThrowInvalid("pattern must be connected with respect to the root");
    }

    LOG_DEBUG("GK key descriptor is valid: {} pattern variables, root='{}'", variables.size(),
              key.root_x.name);
}

void GkValidator::LoadDataInternal() {
    auto const load_started_at = std::chrono::steady_clock::now();
    timing_stats_ = {};
    bool const has_graph = OptionIsSet(config::names::kGkGraphData);
    bool const has_graph_path = OptionIsSet(config::names::kGkGraphPath);

    LOG_DEBUG("Loading GK data: in-memory graph option set={}, graph path option set={}", has_graph,
              has_graph_path);
    if (has_graph == has_graph_path) {
        LOG_ERROR("GK data loading requires exactly one graph source");
        throw config::ConfigurationError(
                "Exactly one of gk_graph and gk_graph_path must be provided");
    }

    if (has_graph_path) {
        auto const source_started_at = std::chrono::steady_clock::now();
        LOG_DEBUG("Reading GK graph from '{}'", graph_path_.string());
        graph_ = gk::parser::ReadGraph(graph_path_);
        LOG_DEBUG("Read GK graph from file: {} triples", graph_.size());
        timing_stats_.load_source_ns = ElapsedNanoseconds(source_started_at);
    }

    auto const graph_check_started_at = std::chrono::steady_clock::now();
    CheckGraph(graph_);
    timing_stats_.load_graph_check_ns = ElapsedNanoseconds(graph_check_started_at);

    auto const key_check_started_at = std::chrono::steady_clock::now();
    CheckKey(key_);
    timing_stats_.load_key_check_ns = ElapsedNanoseconds(key_check_started_at);
    timing_stats_.graph_triple_count = graph_.size();
    timing_stats_.load_total_ns = ElapsedNanoseconds(load_started_at);

    LOG_DEBUG(
            "Loaded and validated GK data: {} graph triples, {} key triples, source={} ns, "
            "graph_check={} ns, key_check={} ns, total={} ns",
            graph_.size(), key_.Q.size(), timing_stats_.load_source_ns,
            timing_stats_.load_graph_check_ns, timing_stats_.load_key_check_ns,
            timing_stats_.load_total_ns);
}

void GkValidator::ExecuteInternal() {
    auto const execute_started_at = std::chrono::steady_clock::now();
    LOG_INFO(
            "Starting GK validation: graph triples={}, key triples={}, pairing_filter={}, "
            "subgraph_reduction={}",
            graph_.size(), key_.Q.size(), use_pairing_filter_, use_subgraph_reduction_);

    if (use_pairing_filter_ || use_subgraph_reduction_) {
        LOG_ERROR(
                "GK execution requested with unsupported options: pairing_filter={}, "
                "subgraph_reduction={}",
                use_pairing_filter_, use_subgraph_reduction_);
        throw std::logic_error(
                "GK pairing filtering and subgraph reduction are not implemented in the "
                "baseline validator");
    }

    auto const prepare_started_at = std::chrono::steady_clock::now();
    std::vector<Node> const entities = CollectEntities(graph_);
    timing_stats_.graph_entity_count = entities.size();
    std::string const& root_type = key_.T_Q.at(key_.root_x);
    std::vector<Node const*> root_candidates;
    root_candidates.reserve(entities.size());
    for (Node const& entity : entities) {
        if (entity.type == root_type) root_candidates.push_back(&entity);
    }
    timing_stats_.root_candidate_count = root_candidates.size();

    std::vector<gk::model::EntityId> entity_ids;
    entity_ids.reserve(entities.size());
    for (Node const& entity : entities) entity_ids.push_back(entity.id_or_val);

    gk::EquivalenceRelation const eq0(entity_ids);
    gk::EvalGK eval(key_);
    std::size_t const root_pair_count =
            root_candidates.size() < 2 ? 0
                                       : root_candidates.size() * (root_candidates.size() - 1) / 2;
    timing_stats_.root_pair_count = root_pair_count;
    timing_stats_.execute_prepare_ns = ElapsedNanoseconds(prepare_started_at);
    LOG_DEBUG(
            "Prepared GK baseline: {} graph entities, {} root candidates, {} root pairs, "
            "{} Eq0 pairs",
            entities.size(), root_candidates.size(), root_pair_count, eq0.GetRelation().size());

    auto const eval_started_at = std::chrono::steady_clock::now();
    std::size_t checked_pairs = 0;
    for (std::size_t first_index = 0; first_index < root_candidates.size(); ++first_index) {
        Node const& first = *root_candidates[first_index];
        for (std::size_t second_index = first_index + 1; second_index < root_candidates.size();
             ++second_index) {
            Node const& second = *root_candidates[second_index];
            ++checked_pairs;
            LOG_TRACE("Evaluating GK root pair ({}, {})", first.id_or_val, second.id_or_val);
            gk::EvalGKInput const input{graph_, graph_, first, second};
            bool const is_violation = eval.Evaluate(input, eq0);
            LOG_TRACE("GK root pair ({}, {}) result: {}", first.id_or_val, second.id_or_val,
                      is_violation ? "violation" : "no violation");
            if (is_violation) {
                result_.violations.emplace_back(first.id_or_val, second.id_or_val);
            }
        }
    }
    timing_stats_.checked_pair_count = checked_pairs;
    timing_stats_.execute_eval_ns = ElapsedNanoseconds(eval_started_at);

    auto const finalize_started_at = std::chrono::steady_clock::now();
    result_.holds = result_.violations.empty();
    timing_stats_.violation_count = result_.violations.size();
    timing_stats_.execute_finalize_ns = ElapsedNanoseconds(finalize_started_at);
    timing_stats_.execute_total_ns = ElapsedNanoseconds(execute_started_at);
    LOG_INFO(
            "GK validation complete: checked {} root pairs, found {} violations, holds={}, "
            "prepare={} ns, eval={} ns, finalize={} ns, total={} ns",
            checked_pairs, result_.violations.size(), result_.holds,
            timing_stats_.execute_prepare_ns, timing_stats_.execute_eval_ns,
            timing_stats_.execute_finalize_ns, timing_stats_.execute_total_ns);
}

}  // namespace algos
