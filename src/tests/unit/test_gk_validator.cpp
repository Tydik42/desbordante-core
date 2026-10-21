#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/any.hpp>
#include <gtest/gtest.h>

#include "core/algorithms/algo_factory.h"
#include "core/algorithms/gk/equivalence_relation.h"
#include "core/algorithms/gk/gk_validator/eval_gk.h"
#include "core/algorithms/gk/gk_validator/gk_validator.h"
#include "core/algorithms/gk/graph_descriptor.h"
#include "core/algorithms/gk/joint_mapping.h"
#include "core/algorithms/gk/parser/gk_graph_parser.h"
#include "core/config/exceptions.h"
#include "core/config/names.h"

namespace {

using gk::model::Graph;
using gk::model::GraphKey;
using gk::model::Node;
using gk::model::NodeKind;
using gk::model::Pattern;
using gk::model::PatternTriple;
using gk::model::Triple;
using gk::model::TypeId;
using gk::model::Variable;
using gk::model::VarKind;

Variable EntityVariable(std::string name) {
    return {VarKind::kEntity, std::move(name)};
}

Variable ValueVariable(std::string name) {
    return {VarKind::kValue, std::move(name)};
}

Variable Constant(std::string value) {
    return {VarKind::kConstant, std::move(value)};
}

Variable Wildcard(std::string name) {
    return {VarKind::kWildcard, std::move(name)};
}

Node Entity(std::string id, std::string type) {
    return {NodeKind::kEntity, std::move(id), std::move(type)};
}

Node Value(std::string value) {
    return {NodeKind::kValue, std::move(value), {}};
}

GraphKey MakeKey(Pattern pattern, std::initializer_list<std::pair<Variable, TypeId>> type_entries,
                 Variable root) {
    std::unordered_map<Variable, TypeId> types;
    for (auto const& [variable, type] : type_entries) types.emplace(variable, type);
    return {std::move(pattern), std::move(types), std::move(root)};
}

GraphKey OneEdgeEntityKey() {
    Variable const x = EntityVariable("x");
    Variable const y = EntityVariable("y");
    return MakeKey({{{x, "p", y}}}, {{x, "X"}, {y, "Y"}}, x);
}

GraphKey OneEdgeWildcardKey() {
    Variable const x = EntityVariable("x");
    Variable const y = Wildcard("y");
    return MakeKey({{{x, "p", y}}}, {{x, "X"}, {y, "Y"}}, x);
}

gk::EquivalenceRelation Identity(std::initializer_list<std::string> entities) {
    return gk::EquivalenceRelation{std::vector<std::string>(entities)};
}

bool Evaluate(GraphKey const& key, gk::EquivalenceRelation const& eq, Graph const& first_graph,
              Graph const& second_graph, Node const& first_root, Node const& second_root) {
    gk::EvalGK eval(key);
    return eval.Evaluate({first_graph, second_graph, first_root, second_root}, eq);
}

std::unique_ptr<algos::GkValidator> CreateValidator(Graph graph, GraphKey key) {
    auto validator = std::make_unique<algos::GkValidator>();
    validator->SetOption(config::names::kGkGraphData, boost::any{std::move(graph)});
    validator->SetOption(config::names::kGkKeyData, boost::any{std::move(key)});
    validator->LoadData();
    return validator;
}

void SetBaselineExecutionOptions(algos::GkValidator& validator) {
    validator.SetOption(config::names::kGkUsePairingFilter, false);
    validator.SetOption(config::names::kGkUseSubgraphReduction, false);
}

void ExpectInvalidDescriptor(Graph graph, GraphKey key, std::string_view label) {
    SCOPED_TRACE(std::string(label));
    EXPECT_THROW(CreateValidator(std::move(graph), std::move(key)), std::invalid_argument);
}

Graph SharedTargetGraph() {
    Node const target = Entity("shared", "Y");
    return {{Entity("a", "X"), "p", target}, {Entity("b", "X"), "p", target}};
}

Graph ThreeRootViolationsGraph() {
    Node const target = Entity("shared", "Y");
    return {{Entity("a", "X"), "p", target},
            {Entity("b", "X"), "p", target},
            {Entity("c", "X"), "p", target},
            {Entity("foreign", "Z"), "p", target}};
}

std::filesystem::path WriteTempGraph(std::string_view csv, std::string_view file_name) {
    std::filesystem::path const path = std::filesystem::temp_directory_path() / file_name;
    std::ofstream file(path);
    if (!file) throw std::runtime_error("Could not create temporary GK graph file");
    file << csv;
    return path;
}

}  // namespace

TEST(GkValidatorTest, GkValidatorEquivalenceRelationKeepsClosure) {
    gk::EquivalenceRelation eq = Identity({"a", "b", "c"});

    EXPECT_TRUE(eq.AreEquivalent("a", "a"));
    EXPECT_TRUE(eq.AreEquivalent("b", "b"));
    EXPECT_FALSE(eq.AreEquivalent("a", "b"));

    EXPECT_TRUE(eq.Merge("a", "b"));
    EXPECT_TRUE(eq.Merge("b", "c"));

    for (std::string const& lhs : {"a", "b", "c"}) {
        for (std::string const& rhs : {"a", "b", "c"}) {
            EXPECT_TRUE(eq.AreEquivalent(lhs, rhs));
        }
    }

    std::size_t const relation_size = eq.GetRelation().size();
    EXPECT_FALSE(eq.Merge("a", "c"));
    eq.AddEntity("d");
    EXPECT_EQ(eq.GetRelation().size(), relation_size + 1);
    EXPECT_TRUE(eq.AreEquivalent("d", "d"));
    EXPECT_FALSE(eq.AreEquivalent("a", "d"));
    eq.AddEntity("d");
    EXPECT_EQ(eq.GetEntityCount(), 4U);
    EXPECT_EQ(eq.GetClassCount(), 2U);
    EXPECT_EQ(eq.GetClasses(), (gk::EquivalenceRelation::Classes{{"a", "b", "c"}, {"d"}}));
    EXPECT_FALSE(eq.AreEquivalent("unknown", "unknown"));
}

TEST(GkValidatorTest, GkValidatorJointMappingSupportsLifecycleAndInjectivity) {
    gk::JointMapping mapping;
    Variable const x = EntityVariable("x");
    Variable const y = EntityVariable("y");

    EXPECT_TRUE(mapping.All().empty());
    Node const first_x = Entity("first-x", "X");
    Node const second_x = Entity("second-x", "X");
    mapping.Bind(first_x, second_x, x);
    EXPECT_TRUE(mapping.IsBound(x));
    EXPECT_EQ(mapping.Get(x), (gk::JointMapping::Mapping{first_x, second_x}));

    EXPECT_FALSE(mapping.IsInjective(y, first_x, Entity("another", "X")));
    EXPECT_FALSE(mapping.IsInjective(y, Entity("another", "X"), second_x));
    EXPECT_TRUE(mapping.IsInjective(y, Entity("another", "X"), Entity("another", "X")));
    EXPECT_TRUE(mapping.IsInjective(x, first_x, second_x));

    Node const first_y = Entity("first-y", "Y");
    Node const second_y = Entity("second-y", "Y");
    mapping.Bind(first_y, second_y, y);
    EXPECT_EQ(mapping.All().size(), 2U);
    mapping.Unbind(y);
    EXPECT_FALSE(mapping.IsBound(y));

    Node const rebound_first = Entity("rebound-first", "X");
    Node const rebound_second = Entity("rebound-second", "X");
    mapping.Bind(rebound_first, rebound_second, x);
    EXPECT_EQ(mapping.Get(x), (gk::JointMapping::Mapping{rebound_first, rebound_second}));
    EXPECT_EQ(mapping.All().size(), 1U);
}

TEST(GkValidatorTest, GkValidatorTraversalOrderRejectsEmptyOrDisconnectedPattern) {
    Variable const x = EntityVariable("x");
    Variable const y = EntityVariable("y");
    Variable const z = EntityVariable("z");
    Variable const w = EntityVariable("w");
    EXPECT_THROW(gk::EvalGK(MakeKey({}, {{x, "X"}}, x)), std::invalid_argument);
    EXPECT_THROW(gk::EvalGK(MakeKey({{x, "p", y}, {z, "q", w}},
                                    {{x, "X"}, {y, "Y"}, {z, "Z"}, {w, "W"}}, x)),
                 std::invalid_argument);
}

TEST(GkValidatorTest, GkValidatorTraversalOrderIsDeterministicAndConnected) {
    Variable const x = EntityVariable("x");
    Variable const y = EntityVariable("y");
    Variable const z = EntityVariable("z");
    Variable const w = EntityVariable("w");
    GraphKey const key = MakeKey({{z, "q", w}, {x, "p", y}, {y, "r", z}},
                                 {{x, "X"}, {y, "Y"}, {z, "Z"}, {w, "W"}}, x);
    gk::EvalGK const eval(key);
    gk::TraversalOrder const& order = eval.GetTraversalOrder();

    ASSERT_EQ(order, (gk::TraversalOrder{1, 2, 0}));
    std::unordered_set<std::size_t> used;
    std::vector<Variable> reached{x};
    for (std::size_t const index : order) {
        ASSERT_LT(index, key.Q.size());
        EXPECT_TRUE(used.insert(index).second);
        PatternTriple const& triple = key.Q[index];
        bool const touches_reached =
                std::find(reached.begin(), reached.end(), triple.subject) != reached.end() ||
                std::find(reached.begin(), reached.end(), triple.object) != reached.end();
        EXPECT_TRUE(touches_reached);
        if (std::find(reached.begin(), reached.end(), triple.subject) == reached.end()) {
            reached.push_back(triple.subject);
        }
        if (std::find(reached.begin(), reached.end(), triple.object) == reached.end()) {
            reached.push_back(triple.object);
        }
    }
    EXPECT_EQ(used.size(), key.Q.size());
    EXPECT_EQ(reached.size(), 4U);
}

TEST(GkValidatorTest, GkValidatorEvalGkAcceptsOutgoingAndIncomingEdges) {
    Variable const x = EntityVariable("x");
    Variable const y = EntityVariable("y");
    Graph const outgoing_first = {{Entity("a", "X"), "p", Entity("target", "Y")}};
    Graph const outgoing_second = {{Entity("b", "X"), "p", Entity("target", "Y")}};
    GraphKey const outgoing_key = MakeKey({{{x, "p", y}}}, {{x, "X"}, {y, "Y"}}, x);
    gk::EquivalenceRelation const outgoing_eq = Identity({"a", "b", "target"});
    EXPECT_TRUE(Evaluate(outgoing_key, outgoing_eq, outgoing_first, outgoing_second,
                         Entity("a", "X"), Entity("b", "X")));

    Graph const incoming_first = {{Entity("target", "Y"), "p", Entity("a", "X")}};
    Graph const incoming_second = {{Entity("target", "Y"), "p", Entity("b", "X")}};
    GraphKey const incoming_key = MakeKey({{{y, "p", x}}}, {{x, "X"}, {y, "Y"}}, x);
    gk::EquivalenceRelation const incoming_eq = Identity({"a", "b", "target"});
    EXPECT_TRUE(Evaluate(incoming_key, incoming_eq, incoming_first, incoming_second,
                         Entity("a", "X"), Entity("b", "X")));
}

TEST(GkValidatorTest, GkValidatorEvalGkRejectsMissingAndMismatchedEdges) {
    GraphKey const key = OneEdgeEntityKey();
    gk::EquivalenceRelation const eq = Identity({"a", "b", "target"});
    Graph const first = {{Entity("a", "X"), "p", Entity("target", "Y")}};
    Graph const second = {{Entity("b", "X"), "p", Entity("target", "Y")}};

    EXPECT_FALSE(Evaluate(key, eq, first, Graph{}, Entity("a", "X"), Entity("b", "X")));
    EXPECT_FALSE(Evaluate(key, eq, Graph{}, second, Entity("a", "X"), Entity("b", "X")));
    EXPECT_FALSE(Evaluate(key, eq, first, {{Entity("b", "X"), "q", Entity("target", "Y")}},
                          Entity("a", "X"), Entity("b", "X")));
}

TEST(GkValidatorTest, GkValidatorEvalGkValidatesRootsAndEntityTypes) {
    GraphKey const key = OneEdgeEntityKey();
    Graph const graph = {{Entity("a", "X"), "p", Entity("target", "Y")}};
    gk::EquivalenceRelation const eq = Identity({"a", "b", "target"});

    EXPECT_FALSE(Evaluate(key, eq, graph, graph, Entity("a", "Z"), Entity("b", "X")));
    EXPECT_FALSE(Evaluate(key, eq, graph, graph, Value("a"), Entity("b", "X")));
    EXPECT_FALSE(Evaluate(key, eq, {{Entity("a", "X"), "p", Entity("target", "Z")}},
                          {{Entity("b", "X"), "p", Entity("target", "Z")}}, Entity("a", "X"),
                          Entity("b", "X")));

    GraphKey missing_root_type = key;
    missing_root_type.T_Q.erase(missing_root_type.root_x);
    EXPECT_FALSE(Evaluate(missing_root_type, eq, graph, graph, Entity("a", "X"), Entity("b", "X")));
}

TEST(GkValidatorTest, GkValidatorEvalGkAppliesEquivalenceToEntityVariables) {
    Variable const x = EntityVariable("x");
    Variable const y = EntityVariable("y");
    GraphKey const key = MakeKey({{{x, "p", y}}}, {{x, "X"}, {y, "Y"}}, x);
    Graph const first = {{Entity("a", "X"), "p", Entity("left", "Y")}};
    Graph const second = {{Entity("b", "X"), "p", Entity("right", "Y")}};
    gk::EquivalenceRelation eq = Identity({"a", "b", "left", "right"});
    gk::EvalGK eval(key);

    EXPECT_FALSE(eval.Evaluate({first, second, Entity("a", "X"), Entity("b", "X")}, eq));
    eq.Merge("left", "right");
    EXPECT_TRUE(eval.Evaluate({first, second, Entity("a", "X"), Entity("b", "X")}, eq));
}

TEST(GkValidatorTest, GkValidatorEvalGkHandlesWildcardsValuesAndConstants) {
    Variable const x = EntityVariable("x");
    Variable const wildcard = Wildcard("wildcard");
    Variable const value = ValueVariable("value");
    Variable const constant = Constant("fixed");
    GraphKey const key = MakeKey({{{x, "p", wildcard}, {x, "q", value}, {x, "r", constant}}},
                                 {{x, "X"}, {wildcard, "Y"}}, x);
    Graph const first = {{Entity("a", "X"), "p", Entity("left-wildcard", "Y")},
                         {Entity("a", "X"), "q", Value("same")},
                         {Entity("a", "X"), "r", Value("fixed")}};
    Graph const second = {{Entity("b", "X"), "p", Entity("right-wildcard", "Y")},
                          {Entity("b", "X"), "q", Value("same")},
                          {Entity("b", "X"), "r", Value("fixed")}};
    gk::EquivalenceRelation const eq = Identity({"a", "b"});

    EXPECT_TRUE(Evaluate(key, eq, first, second, Entity("a", "X"), Entity("b", "X")));
    EXPECT_FALSE(Evaluate(key, eq, first,
                          {{Entity("b", "X"), "p", Entity("right-wildcard", "Y")},
                           {Entity("b", "X"), "q", Value("different")},
                           {Entity("b", "X"), "r", Value("fixed")}},
                          Entity("a", "X"), Entity("b", "X")));
    EXPECT_FALSE(Evaluate(key, eq, first,
                          {{Entity("b", "X"), "p", Entity("right-wildcard", "Y")},
                           {Entity("b", "X"), "q", Value("same")},
                           {Entity("b", "X"), "r", Value("other")}},
                          Entity("a", "X"), Entity("b", "X")));

    EXPECT_FALSE(Evaluate(key, eq,
                          {{Entity("a", "X"), "p", Value("not-an-entity")},
                           {Entity("a", "X"), "q", Value("same")},
                           {Entity("a", "X"), "r", Value("fixed")}},
                          second, Entity("a", "X"), Entity("b", "X")));
    EXPECT_FALSE(Evaluate(key, eq, first,
                          {{Entity("b", "X"), "p", Entity("right-wildcard", "Y")},
                           {Entity("b", "X"), "q", Entity("same", "Y")},
                           {Entity("b", "X"), "r", Value("fixed")}},
                          Entity("a", "X"), Entity("b", "X")));
}

TEST(GkValidatorTest, GkValidatorEvalGkEnforcesInjectivity) {
    Variable const x = EntityVariable("x");
    Variable const y = EntityVariable("y");
    Variable const z = EntityVariable("z");
    GraphKey const entity_key =
            MakeKey({{{x, "p", y}, {x, "q", z}}}, {{x, "X"}, {y, "Y"}, {z, "Y"}}, x);

    Graph const same_target_first = {{Entity("a", "X"), "p", Entity("same", "Y")},
                                     {Entity("a", "X"), "q", Entity("same", "Y")}};
    Graph const same_target_second = {{Entity("b", "X"), "p", Entity("same", "Y")},
                                      {Entity("b", "X"), "q", Entity("same", "Y")}};
    EXPECT_FALSE(Evaluate(entity_key, Identity({"a", "b", "same"}), same_target_first,
                          same_target_second, Entity("a", "X"), Entity("b", "X")));

    Graph const distinct_first = {{Entity("a", "X"), "p", Entity("left", "Y")},
                                  {Entity("a", "X"), "q", Entity("right", "Y")}};
    Graph const same_second = {{Entity("b", "X"), "p", Entity("same", "Y")},
                               {Entity("b", "X"), "q", Entity("same", "Y")}};
    gk::EquivalenceRelation second_projection_eq = Identity({"a", "b", "left", "right", "same"});
    second_projection_eq.Merge("left", "same");
    second_projection_eq.Merge("right", "same");
    EXPECT_FALSE(Evaluate(entity_key, second_projection_eq, distinct_first, same_second,
                          Entity("a", "X"), Entity("b", "X")));

    Variable const value = ValueVariable("value");
    Variable const other_value = ValueVariable("other_value");
    GraphKey const value_key = MakeKey({{{x, "p", value}, {x, "q", other_value}}}, {{x, "X"}}, x);
    Graph const duplicate_values_first = {{Entity("a", "X"), "p", Value("same")},
                                          {Entity("a", "X"), "q", Value("same")}};
    Graph const duplicate_values_second = {{Entity("b", "X"), "p", Value("same")},
                                           {Entity("b", "X"), "q", Value("same")}};
    EXPECT_FALSE(Evaluate(value_key, Identity({"a", "b"}), duplicate_values_first,
                          duplicate_values_second, Entity("a", "X"), Entity("b", "X")));

    Variable const fixed = Constant("same");
    GraphKey const value_constant_key =
            MakeKey({{{x, "p", value}, {x, "q", fixed}}}, {{x, "X"}}, x);
    EXPECT_FALSE(Evaluate(value_constant_key, Identity({"a", "b"}), duplicate_values_first,
                          duplicate_values_second, Entity("a", "X"), Entity("b", "X")));
}

TEST(GkValidatorTest, GkValidatorEvalGkBacktracksInBothDirections) {
    Variable const x = EntityVariable("x");
    Variable const y = EntityVariable("y");
    Variable const z = EntityVariable("z");
    gk::EquivalenceRelation const eq = Identity({"a", "b", "bad-y", "good-y", "good-z"});

    GraphKey const forward_key =
            MakeKey({{{x, "p", y}, {y, "q", z}}}, {{x, "X"}, {y, "Y"}, {z, "Z"}}, x);
    Graph const forward_first = {{Entity("a", "X"), "p", Entity("bad-y", "Y")},
                                 {Entity("a", "X"), "p", Entity("good-y", "Y")},
                                 {Entity("good-y", "Y"), "q", Entity("good-z", "Z")}};
    Graph const forward_second = {{Entity("b", "X"), "p", Entity("bad-y", "Y")},
                                  {Entity("b", "X"), "p", Entity("good-y", "Y")},
                                  {Entity("good-y", "Y"), "q", Entity("good-z", "Z")}};
    EXPECT_TRUE(Evaluate(forward_key, eq, forward_first, forward_second, Entity("a", "X"),
                         Entity("b", "X")));

    GraphKey const backward_key =
            MakeKey({{{y, "p", x}, {z, "q", y}}}, {{x, "X"}, {y, "Y"}, {z, "Z"}}, x);
    Graph const backward_first = {{Entity("bad-y", "Y"), "p", Entity("a", "X")},
                                  {Entity("good-y", "Y"), "p", Entity("a", "X")},
                                  {Entity("good-z", "Z"), "q", Entity("good-y", "Y")}};
    Graph const backward_second = {{Entity("bad-y", "Y"), "p", Entity("b", "X")},
                                   {Entity("good-y", "Y"), "p", Entity("b", "X")},
                                   {Entity("good-z", "Z"), "q", Entity("good-y", "Y")}};
    EXPECT_TRUE(Evaluate(backward_key, eq, backward_first, backward_second, Entity("a", "X"),
                         Entity("b", "X")));
}

TEST(GkValidatorTest, GkValidatorEvalGkChecksBoundEdgesAndSelfLoops) {
    Variable const x = EntityVariable("x");
    Variable const y = EntityVariable("y");
    GraphKey const cycle_key = MakeKey({{{x, "p", y}, {y, "q", x}}}, {{x, "X"}, {y, "Y"}}, x);
    Graph const cycle_first = {{Entity("a", "X"), "p", Entity("target", "Y")},
                               {Entity("target", "Y"), "q", Entity("a", "X")}};
    Graph const cycle_second = {{Entity("b", "X"), "p", Entity("target", "Y")},
                                {Entity("target", "Y"), "q", Entity("b", "X")}};
    gk::EquivalenceRelation const eq = Identity({"a", "b", "target"});
    EXPECT_TRUE(
            Evaluate(cycle_key, eq, cycle_first, cycle_second, Entity("a", "X"), Entity("b", "X")));
    EXPECT_FALSE(Evaluate(cycle_key, eq, cycle_first,
                          {{Entity("b", "X"), "p", Entity("target", "Y")}}, Entity("a", "X"),
                          Entity("b", "X")));

    GraphKey const self_loop_key = MakeKey({{{x, "p", x}}}, {{x, "X"}}, x);
    Graph const self_loop_first = {{Entity("a", "X"), "p", Entity("a", "X")}};
    Graph const self_loop_second = {{Entity("b", "X"), "p", Entity("b", "X")}};
    EXPECT_TRUE(Evaluate(self_loop_key, eq, self_loop_first, self_loop_second, Entity("a", "X"),
                         Entity("b", "X")));
}

TEST(GkValidatorTest, GkValidatorEvalGkDoesNotLeakMappingBetweenCalls) {
    GraphKey const key = OneEdgeEntityKey();
    Graph const graph = {{Entity("a", "X"), "p", Entity("first-target", "Y")},
                         {Entity("b", "X"), "p", Entity("second-target", "Y")},
                         {Entity("c", "X"), "p", Entity("first-target", "Y")}};
    gk::EquivalenceRelation const eq = Identity({"a", "b", "c", "first-target", "second-target"});
    gk::EvalGK eval(key);

    EXPECT_FALSE(eval.Evaluate({graph, graph, Entity("a", "X"), Entity("b", "X")}, eq));
    EXPECT_TRUE(eval.Evaluate({graph, graph, Entity("a", "X"), Entity("c", "X")}, eq));
}

TEST(GkValidatorTest, GkValidatorRegistersOptionsAndAppliesDefaults) {
    algos::GkValidator validator;
    auto const possible = validator.GetPossibleOptions();
    EXPECT_TRUE(possible.contains(config::names::kGkGraphData));
    EXPECT_TRUE(possible.contains(config::names::kGkGraphPath));
    EXPECT_TRUE(possible.contains(config::names::kGkKeyData));
    EXPECT_TRUE(possible.contains(config::names::kGkUsePairingFilter));
    EXPECT_TRUE(possible.contains(config::names::kGkUseSubgraphReduction));
    EXPECT_EQ(validator.GetTypeIndex(config::names::kGkGraphData), typeid(Graph));
    EXPECT_EQ(validator.GetTypeIndex(config::names::kGkGraphPath), typeid(std::filesystem::path));
    EXPECT_EQ(validator.GetTypeIndex(config::names::kGkKeyData), typeid(GraphKey));

    validator.SetOption(config::names::kGkGraphData, boost::any{SharedTargetGraph()});
    validator.SetOption(config::names::kGkKeyData, boost::any{OneEdgeEntityKey()});
    validator.LoadData();
    validator.SetOption(config::names::kGkUsePairingFilter);
    validator.SetOption(config::names::kGkUseSubgraphReduction);

    auto const values = validator.GetOptValues();
    EXPECT_FALSE(boost::any_cast<bool>(values.at(config::names::kGkUsePairingFilter).value));
    EXPECT_FALSE(boost::any_cast<bool>(values.at(config::names::kGkUseSubgraphReduction).value));
}

TEST(GkValidatorTest, GkValidatorLoadsStringIdsAndAttributesFromCsvPath) {
    std::filesystem::path const path = WriteTempGraph(
            "subject_id,subject_type,predicate,object_kind,object_id_or_value,object_type\n"
            "a,X,name,value,Alice,-\n"
            "b,X,name,value,Bob,-\n"
            "target,Y,name,value,Shared,-\n"
            "a,X,p,entity,target,Y\n"
            "b,X,p,entity,target,Y\n",
            "desbordante_gk_validator_graph.csv");

    Graph const parsed_graph = gk::parser::ReadGraph(path);
    EXPECT_TRUE(std::any_of(parsed_graph.begin(), parsed_graph.end(), [](Triple const& triple) {
        return triple.subject.id_or_val == "a" && triple.predicate == "name" &&
               triple.object == Value("Alice");
    }));

    algos::GkValidator validator;
    validator.SetOption(config::names::kGkGraphPath, boost::any{path});
    validator.SetOption(config::names::kGkKeyData, boost::any{OneEdgeWildcardKey()});
    ASSERT_NO_THROW(validator.LoadData());

    auto const options = validator.GetOptValues();
    EXPECT_EQ(boost::any_cast<std::filesystem::path>(options.at(config::names::kGkGraphPath).value),
              path);

    SetBaselineExecutionOptions(validator);
    validator.Execute();

    EXPECT_FALSE(validator.Holds());
    EXPECT_EQ(validator.GetViolations(), (std::vector<gk::EntityPair>{{"a", "b"}}));
    std::filesystem::remove(path);
}

TEST(GkValidatorTest, GkValidatorRejectsTwoGraphSources) {
    algos::GkValidator validator;
    validator.SetOption(config::names::kGkGraphData, boost::any{SharedTargetGraph()});
    validator.SetOption(config::names::kGkGraphPath,
                        boost::any{std::filesystem::path{"does-not-matter.csv"}});
    validator.SetOption(config::names::kGkKeyData, boost::any{OneEdgeEntityKey()});

    EXPECT_THROW(validator.LoadData(), config::ConfigurationError);
}

TEST(GkValidatorTest, GkValidatorRejectsMalformedGkCsv) {
    std::vector<std::string> const malformed_inputs = {
            "wrong_header\n",
            "subject_id,subject_type,predicate,object_kind,object_id_or_value,object_type\n"
            "a,X,p,entity,target\n",
            "subject_id,subject_type,predicate,object_kind,object_id_or_value,object_type\n"
            "a,X,p,unknown,target,Y\n",
            "subject_id,subject_type,predicate,object_kind,object_id_or_value,object_type\n"
            "a,X,p,value,Alice,Y\n",
            "subject_id,subject_type,predicate,object_kind,object_id_or_value,object_type\n"
            "a,X,p,entity,target,Y\n"
            "target,Z,q,value,value,-\n",
            "subject_id,subject_type,predicate,object_kind,object_id_or_value,object_type\n"
            "a,X,p,value,extra,-,field\n",
            "subject_id,subject_type,predicate,object_kind,object_id_or_value,object_type\n"
            "a,X,p,value,\"unterminated\n",
            "subject_id,subject_type,predicate,object_kind,object_id_or_value,object_type\n"
            "a,X,p,value,\"closed\"suffix,-\n",
            "subject_id,subject_type,predicate,object_kind,object_id_or_value,object_type\n"
            "a,X,p,value,bare\"quote,-\n",
            "subject_id,subject_type,predicate,object_kind,object_id_or_value,object_type\n"
            "a,X,p,value,,-\n",
            "subject_id\tsubject_type\tpredicate\tobject_kind\tobject_id_or_value\tobject_type\n",
            "",
    };

    for (std::string const& input : malformed_inputs) {
        SCOPED_TRACE(input);
        std::istringstream stream(input);
        EXPECT_THROW(gk::parser::ReadGraph(stream), std::runtime_error);
    }
}

TEST(GkValidatorTest, GkValidatorCsvPreservesQuotedFieldsAndLineEndings) {
    std::vector<std::pair<std::string, std::string>> const values = {
            {"\"Alice, \"\"A\"\"\"", "Alice, \"A\""},
            {"\"first\n\nsecond\"", "first\n\nsecond"},
            {"\"first\r\nsecond\"", "first\r\nsecond"},
            {"\" a\t\\n \"", " a\t\\n "},
            {"C:\\tmp", "C:\\tmp"},
            {"Привет", "Привет"},
    };
    for (auto const& [encoded, expected] : values) {
        SCOPED_TRACE(encoded);
        for (std::string const ending : {"\n", "\r\n"}) {
            std::istringstream stream(ending +
                                      "\"subject_id\",subject_type,predicate,object_kind,object_id_"
                                      "or_value,object_type" +
                                      ending + "\"a,one\",X,\"name\"\"quoted\",value," + encoded +
                                      ",-");
            Graph const graph = gk::parser::ReadGraph(stream);
            ASSERT_EQ(graph.size(), 1U);
            EXPECT_EQ(graph[0].subject, Entity("a,one", "X"));
            EXPECT_EQ(graph[0].predicate, "name\"quoted");
            EXPECT_EQ(graph[0].object, Value(expected));
        }
    }
}

TEST(GkValidatorTest, GkValidatorCsvAcceptsHeaderOnlyAndEmptyRecords) {
    std::istringstream stream(
            "\r\nsubject_id,subject_type,predicate,object_kind,object_id_or_value,object_type\r\n"
            "\n\r\n");
    EXPECT_TRUE(gk::parser::ReadGraph(stream).empty());
}

TEST(GkValidatorTest, GkValidatorAcceptsValidDescriptorsAndEmptyGraph) {
    Variable const x = EntityVariable("x");
    Variable const wildcard = Wildcard("wildcard");
    Variable const value = ValueVariable("value");
    Variable const constant = Constant("fixed");
    GraphKey const key = MakeKey({{{x, "p", wildcard}, {x, "q", value}, {x, "r", constant}}},
                                 {{x, "X"}, {wildcard, "Y"}}, x);
    auto validator = CreateValidator({}, key);
    SetBaselineExecutionOptions(*validator);
    EXPECT_NO_THROW(validator->Execute());
    EXPECT_TRUE(validator->Holds());
    EXPECT_TRUE(validator->GetViolations().empty());
}

TEST(GkValidatorTest, GkValidatorReportsEveryUnorderedRootPair) {
    auto validator = CreateValidator(ThreeRootViolationsGraph(), OneEdgeWildcardKey());
    SetBaselineExecutionOptions(*validator);
    validator->Execute();

    EXPECT_FALSE(validator->Holds());
    EXPECT_EQ(validator->GetViolations(),
              (std::vector<gk::EntityPair>{{"a", "b"}, {"a", "c"}, {"b", "c"}}));
}

TEST(GkValidatorTest, GkValidatorClearsResultBeforeRepeatedExecution) {
    auto validator = CreateValidator(SharedTargetGraph(), OneEdgeWildcardKey());
    SetBaselineExecutionOptions(*validator);
    validator->Execute();
    ASSERT_EQ(validator->GetViolations().size(), 1U);

    SetBaselineExecutionOptions(*validator);
    validator->Execute();
    EXPECT_FALSE(validator->Holds());
    EXPECT_EQ(validator->GetViolations(), (std::vector<gk::EntityPair>{{"a", "b"}}));
}

TEST(GkValidatorTest, GkValidatorReportsPhaseTimingAndWorkCounters) {
    auto validator = CreateValidator(ThreeRootViolationsGraph(), OneEdgeWildcardKey());
    SetBaselineExecutionOptions(*validator);
    validator->Execute();

    gk::ValidationTimingStats const& stats = validator->GetTimingStats();
    EXPECT_EQ(stats.graph_triple_count, 4U);
    EXPECT_EQ(stats.graph_entity_count, 5U);
    EXPECT_EQ(stats.root_candidate_count, 3U);
    EXPECT_EQ(stats.root_pair_count, 3U);
    EXPECT_EQ(stats.checked_pair_count, 3U);
    EXPECT_EQ(stats.violation_count, 3U);

    EXPECT_GT(stats.load_total_ns, 0U);
    EXPECT_GT(stats.execute_prepare_ns, 0U);
    EXPECT_GT(stats.execute_eval_ns, 0U);
    EXPECT_GT(stats.execute_total_ns, 0U);
    EXPECT_GE(stats.execute_total_ns, stats.execute_prepare_ns);
    EXPECT_GE(stats.execute_total_ns, stats.execute_eval_ns);
    EXPECT_GE(stats.execute_total_ns, stats.execute_finalize_ns);
}

TEST(GkValidatorTest, GkValidatorRejectsUnimplementedExecutionModes) {
    for (std::string_view enabled :
         {config::names::kGkUsePairingFilter, config::names::kGkUseSubgraphReduction}) {
        SCOPED_TRACE(std::string(enabled));
        auto validator = CreateValidator(SharedTargetGraph(), OneEdgeWildcardKey());
        validator->SetOption(config::names::kGkUsePairingFilter,
                             enabled == config::names::kGkUsePairingFilter);
        validator->SetOption(config::names::kGkUseSubgraphReduction,
                             enabled == config::names::kGkUseSubgraphReduction);
        EXPECT_THROW(validator->Execute(), std::logic_error);
    }
}

TEST(GkValidatorTest, GkValidatorRejectsMalformedGraphDescriptors) {
    GraphKey const key = OneEdgeEntityKey();

    ExpectInvalidDescriptor({{Value("subject"), "p", Entity("object", "Y")}}, key, "value subject");
    ExpectInvalidDescriptor({{Entity("subject", "X"), "", Entity("object", "Y")}}, key,
                            "empty predicate");
    ExpectInvalidDescriptor(
            {{Node{static_cast<NodeKind>(99), "subject", "X"}, "p", Entity("object", "Y")}}, key,
            "unknown node kind");
    ExpectInvalidDescriptor({{Entity("", "X"), "p", Entity("object", "Y")}}, key,
                            "empty entity identifier");
    ExpectInvalidDescriptor({{Entity("subject", ""), "p", Entity("object", "Y")}}, key,
                            "empty entity type");
    ExpectInvalidDescriptor({{Entity("subject", "X"), "p", Value("")}}, key, "empty data value");
    ExpectInvalidDescriptor({{Entity("subject", "X"), "p", Node{NodeKind::kValue, "value", "Y"}}},
                            key, "typed data value");
    ExpectInvalidDescriptor({{Entity("same", "X"), "p", Entity("object", "Y")},
                             {Entity("same", "Z"), "q", Value("value")}},
                            key, "inconsistent entity types");
}

TEST(GkValidatorTest, GkValidatorRejectsMalformedKeyDescriptors) {
    Variable const x = EntityVariable("x");
    Variable const y = EntityVariable("y");
    GraphKey const base = OneEdgeEntityKey();
    Graph const graph = SharedTargetGraph();

    {
        GraphKey key = base;
        key.Q.clear();
        ExpectInvalidDescriptor(graph, std::move(key), "empty pattern");
    }
    {
        GraphKey key = base;
        key.Q[0].predicate.clear();
        ExpectInvalidDescriptor(graph, std::move(key), "empty predicate");
    }
    {
        GraphKey key = base;
        key.Q[0].object.name.clear();
        ExpectInvalidDescriptor(graph, std::move(key), "empty variable name");
    }
    {
        GraphKey key = base;
        key.Q[0].object.kind = static_cast<VarKind>(99);
        ExpectInvalidDescriptor(graph, std::move(key), "unknown variable kind");
    }
    {
        GraphKey key = base;
        key.Q[0].object = ValueVariable("x");
        key.T_Q.erase(y);
        EXPECT_NO_THROW(CreateValidator(graph, std::move(key)));
    }
    {
        GraphKey key = base;
        key.Q[0].subject = ValueVariable("subject");
        ExpectInvalidDescriptor(graph, std::move(key), "value subject");
    }
    {
        GraphKey key = base;
        key.Q[0].subject = Constant("subject");
        ExpectInvalidDescriptor(graph, std::move(key), "constant subject");
    }
    {
        GraphKey key = base;
        key.T_Q.erase(x);
        ExpectInvalidDescriptor(graph, std::move(key), "missing root type");
    }
    {
        GraphKey key = base;
        key.root_x = EntityVariable("");
        ExpectInvalidDescriptor(graph, std::move(key), "empty root name");
    }
    {
        GraphKey key = base;
        key.root_x = EntityVariable("outside");
        ExpectInvalidDescriptor(graph, std::move(key), "root outside pattern");
    }
    {
        GraphKey key = base;
        key.T_Q.erase(y);
        ExpectInvalidDescriptor(graph, std::move(key), "missing non-root type");
    }
    {
        GraphKey key = base;
        key.T_Q[y].clear();
        ExpectInvalidDescriptor(graph, std::move(key), "empty type");
    }
    {
        GraphKey key = base;
        key.root_x = Wildcard("x");
        ExpectInvalidDescriptor(graph, std::move(key), "non-entity root");
    }
    {
        GraphKey key = base;
        Variable const z = EntityVariable("z");
        key.Q.push_back({z, "q", EntityVariable("w")});
        key.T_Q.emplace(z, "Z");
        key.T_Q.emplace(EntityVariable("w"), "W");
        ExpectInvalidDescriptor(graph, std::move(key), "disconnected pattern");
    }
    {
        GraphKey key = base;
        key.T_Q.emplace(EntityVariable("outside"), "Z");
        ExpectInvalidDescriptor(graph, std::move(key), "type outside pattern");
    }
    {
        GraphKey key = base;
        key.T_Q.emplace(ValueVariable("value"), "D");
        ExpectInvalidDescriptor(graph, std::move(key), "type for data variable");
    }
    {
        GraphKey key = base;
        key.Q.push_back(key.Q.front());
        ExpectInvalidDescriptor(graph, std::move(key), "duplicate triple");
    }
}
