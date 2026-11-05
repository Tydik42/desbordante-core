#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/algorithms/gk_compact/gk_validator/gk_validator.h"
#include "core/algorithms/gk_compact/joint_mapping.h"
#include "core/algorithms/gk_compact/model/graph_builder.h"
#include "core/algorithms/gk_compact/optimizations/evaluation.h"
#include "core/algorithms/gk_compact/optimizations/neighborhood.h"
#include "core/algorithms/gk_compact/optimizations/pairing_filter.h"
#include "core/algorithms/gk_compact/optimizations/subgraph_reduction.h"
#include "core/config/gk/option.h"
#include "core/config/names.h"

namespace {

namespace compact = gk_compact;
using namespace gk::model;

Variable const x{VarKind::kEntity, "x"};
Variable const y{VarKind::kEntity, "y"};
Variable const w{VarKind::kWildcard, "w"};
Variable const z{VarKind::kValue, "z"};
Variable const other_value{VarKind::kValue, "other"};
Variable const constant{VarKind::kConstant, "v"};

Node Entity(std::string id, std::string type = "X") {
    return {NodeKind::kEntity, std::move(id), std::move(type)};
}

Node Value(std::string value = "v") {
    return {NodeKind::kValue, std::move(value), {}};
}

GraphKey EntityKey(Variable target = y) {
    return {{{x, "p", target}}, {{x, "X"}, {target, "Y"}}, x};
}

bool Evaluate(GraphKey const& key, Graph const& source) {
    auto const graph = compact::BuildGraph(source);
    auto numeric = compact::CompileKey(key, graph);
    if (!numeric) return false;
    compact::EvalGK evaluator(std::move(*numeric));
    compact::EquivalenceRelation const eq(graph.Metadata().GetEntityCount());
    return evaluator.Evaluate({graph, graph, *graph.FindEntity("a"), *graph.FindEntity("b")}, eq);
}

void Execute(algos::CompactGkValidator& validator, bool localization = false, bool pairing = false,
             bool reduction = false) {
    validator.SetOption(config::names::kGkUseLocalization, localization);
    if (localization) validator.SetOption(config::names::kGkUsePairingFilter, pairing);
    if (pairing) validator.SetOption(config::names::kGkUseSubgraphReduction, reduction);
    validator.Execute();
}

compact::Graph Select(compact::Graph const& source, std::vector<compact::VertexId> vertices) {
    compact::GraphData data;
    data.metadata = source.Data().metadata;
    data.row_to_vertex = std::move(vertices);
    for (auto vertex : data.row_to_vertex) {
        for (auto edge : source.Outgoing(vertex))
            if (std::ranges::binary_search(data.row_to_vertex, edge.neighbor))
                data.outgoing.edges.push_back(edge);
        for (auto edge : source.Incoming(vertex))
            if (std::ranges::binary_search(data.row_to_vertex, edge.neighbor))
                data.incoming.edges.push_back(edge);
        data.outgoing.offsets.push_back(data.outgoing.edges.size());
        data.incoming.offsets.push_back(data.incoming.edges.size());
    }
    data.input_triple_count = data.outgoing.edges.size();
    return compact::Graph(std::move(data));
}

// Independent small-graph oracle: sets, direct checks of edges, asynchronous
// deletions and an exhaustive scan of potential support pairs.
std::vector<std::set<compact::VertexPair>> ReferencePairing(compact::CompiledKey const& key,
                                                            compact::GraphView first,
                                                            compact::GraphView second) {
    std::vector<std::set<compact::VertexPair>> result(key.nodes.size());
    for (std::size_t u = 0; u < key.nodes.size(); ++u) {
        auto const& node = key.nodes[u];
        for (auto left : first.VertexIds()) {
            for (auto right : second.VertexIds()) {
                bool compatible = false;
                if (node.kind == VarKind::kEntity || node.kind == VarKind::kWildcard)
                    compatible = first.IsEntity(left) && second.IsEntity(right) &&
                                 first.GetType(left) == node.type &&
                                 second.GetType(right) == node.type;
                else if (node.kind == VarKind::kValue)
                    compatible = !first.IsEntity(left) && !second.IsEntity(right) && left == right;
                else
                    compatible = left == *node.constant && right == *node.constant;
                if (compatible) result[u].emplace(left, right);
            }
        }
    }
    bool changed;
    do {
        changed = false;
        for (std::size_t u = 0; u < key.nodes.size(); ++u) {
            for (auto it = result[u].begin(); it != result[u].end();) {
                auto const [left, right] = *it;
                bool supported = true;
                for (auto const& triple : key.triples) {
                    if (triple.subject == u) {
                        bool found = false;
                        for (auto const& [next_left, next_right] : result[triple.object])
                            found |= first.Base().HasEdge(left, triple.predicate, next_left) &&
                                     second.Base().HasEdge(right, triple.predicate, next_right);
                        supported &= found;
                    }
                    if (triple.object == u) {
                        bool found = false;
                        for (auto const& [next_left, next_right] : result[triple.subject])
                            found |= first.Base().HasEdge(next_left, triple.predicate, left) &&
                                     second.Base().HasEdge(next_right, triple.predicate, right);
                        supported &= found;
                    }
                }
                if (supported)
                    ++it;
                else {
                    it = result[u].erase(it);
                    changed = true;
                }
            }
        }
    } while (changed);
    return result;
}

void ExpectReferencePairing(compact::CompiledKey const& key, compact::GraphView first,
                            compact::GraphView second) {
    auto const actual = compact::ComputePairing(key, first, second);
    auto const expected = ReferencePairing(key, first, second);
    ASSERT_EQ(actual.offsets.size(), key.nodes.size() + 1);
    EXPECT_EQ(actual.offsets.front(), 0U);
    EXPECT_EQ(actual.offsets.back(), actual.pairs.size());
    EXPECT_TRUE(std::ranges::is_sorted(actual.offsets));
    for (compact::PatternNodeId u = 0; u < key.nodes.size(); ++u) {
        auto const candidates = actual.Candidates(u);
        EXPECT_TRUE(std::ranges::equal(candidates, expected[u]));
        EXPECT_EQ(std::adjacent_find(candidates.begin(), candidates.end()), candidates.end());
    }
}

// Exhaustive assignments in node order, independent of the traversal and CSR
// candidate generation. Check triples only after both full embeddings exist.
bool ReferenceEvaluate(compact::CompiledKey const& key, compact::EvalGKInput const& input,
                       compact::EquivalenceRelation const& eq) {
    std::vector<std::optional<compact::VertexPair>> assigned(key.nodes.size());
    assigned[key.root] = compact::VertexPair{input.first_root, input.second_root};
    auto visit = [&](auto&& self, std::size_t index) -> bool {
        if (index == key.nodes.size()) {
            for (auto const& triple : key.triples) {
                auto const [s1, s2] = *assigned[triple.subject];
                auto const [o1, o2] = *assigned[triple.object];
                if (!input.first_graph.HasEdge(s1, triple.predicate, o1) ||
                    !input.second_graph.HasEdge(s2, triple.predicate, o2))
                    return false;
            }
            return true;
        }
        if (index == key.root) return self(self, index + 1);
        auto const& node = key.nodes[index];
        for (auto first : input.first_graph.VertexIds()) {
            for (auto second : input.second_graph.VertexIds()) {
                bool compatible;
                if (node.kind == VarKind::kEntity || node.kind == VarKind::kWildcard) {
                    compatible = input.first_graph.GetType(first) == node.type &&
                                 input.second_graph.GetType(second) == node.type;
                    if (compatible && node.kind == VarKind::kEntity)
                        compatible = eq.AreEquivalent(first, second);
                } else if (node.kind == VarKind::kValue) {
                    compatible = !input.first_graph.IsEntity(first) && first == second;
                } else {
                    compatible = first == *node.constant && second == *node.constant;
                }
                if (!compatible) continue;
                bool injective = true;
                for (auto const& pair : assigned)
                    if (pair && (pair->first == first || pair->second == second)) injective = false;
                if (!injective) continue;
                assigned[index] = compact::VertexPair{first, second};
                if (self(self, index + 1)) return true;
                assigned[index].reset();
            }
        }
        return false;
    };
    return visit(visit, 0);
}

static_assert(std::is_nothrow_move_constructible_v<compact::Graph>);
static_assert(std::is_nothrow_move_assignable_v<compact::Graph>);
static_assert(!std::is_constructible_v<compact::GraphView, compact::Graph&&>);
static_assert(!std::is_constructible_v<compact::GraphView, compact::Graph const&&>);
static_assert(!std::is_constructible_v<compact::GraphView, compact::Graph&&,
                                       std::span<compact::VertexId const>>);
static_assert(!std::is_constructible_v<compact::Neighborhoods, compact::Graph&&,
                                       std::span<compact::VertexId const>, std::size_t>);

}  // namespace

TEST(GkCompactValidatorTest, EquivalenceMatchesReferencePartitionsAndRejectsInvalidIds) {
    std::mt19937 random(20261029);
    for (std::size_t size = 0; size < 17; ++size) {
        compact::EquivalenceRelation eq(size);
        EXPECT_TRUE(eq.IsIdentity());
        std::vector<unsigned> labels(size);
        std::iota(labels.begin(), labels.end(), 0U);
        for (std::size_t step = 0; step < 40; ++step) {
            if (size != 0) {
                auto const first = random() % size, second = random() % size;
                bool const changed = labels[first] != labels[second];
                EXPECT_EQ(eq.Merge(first, second), changed);
                auto const removed = labels[second], retained = labels[first];
                std::replace(labels.begin(), labels.end(), removed, retained);
            }
            std::set<unsigned> const distinct(labels.begin(), labels.end());
            EXPECT_EQ(eq.GetClassCount(), distinct.size());
            EXPECT_EQ(eq.IsIdentity(), distinct.size() == size);
            auto const classes = eq.GetClasses();
            EXPECT_EQ(classes.size(), distinct.size());
            std::set<compact::VertexId> seen;
            for (auto const& group : classes) {
                ASSERT_FALSE(group.empty());
                for (auto vertex : group) {
                    ASSERT_LT(vertex, size);
                    EXPECT_TRUE(seen.insert(vertex).second);
                    EXPECT_EQ(labels[vertex], labels[group.front()]);
                }
            }
            EXPECT_EQ(seen.size(), size);
            for (compact::VertexId first = 0; first < size; ++first)
                for (compact::VertexId second = 0; second < size; ++second)
                    EXPECT_EQ(eq.AreEquivalent(first, second), labels[first] == labels[second]);
        }
        auto const before = eq.GetClasses();
        auto const invalid = static_cast<compact::VertexId>(size);
        EXPECT_THROW(eq.AreEquivalent(invalid, 0), std::out_of_range);
        EXPECT_THROW(eq.Merge(0, invalid), std::out_of_range);
        EXPECT_EQ(eq.GetClasses(), before);
    }
}

TEST(GkCompactValidatorTest, IdentityEquivalenceKeepsTheFullIdDomainWithoutDsuArrays) {
    // Eager arrays would require about 32 GiB here. Identity queries, no-op
    // merges and copies must not materialize that storage.
    auto const count = static_cast<std::size_t>(std::numeric_limits<compact::VertexId>::max());
    auto const last = static_cast<compact::VertexId>(count - 1);
    auto const invalid = std::numeric_limits<compact::VertexId>::max();
    compact::EquivalenceRelation eq(count);
    EXPECT_EQ(eq.GetEntityCount(), count);
    EXPECT_EQ(eq.GetClassCount(), count);
    EXPECT_TRUE(eq.IsIdentity());
    EXPECT_TRUE(eq.AreEquivalent(last, last));
    EXPECT_FALSE(eq.AreEquivalent(0, last));
    EXPECT_FALSE(eq.Merge(last, last));
    EXPECT_THROW(eq.Merge(0, invalid), std::out_of_range);
    EXPECT_THROW(eq.Merge(invalid, 0), std::out_of_range);
    EXPECT_THROW(eq.AreEquivalent(invalid, invalid), std::out_of_range);
    EXPECT_THROW(eq.AreEquivalent(0, invalid), std::out_of_range);
    auto const copy = eq;
    EXPECT_TRUE(copy.IsIdentity());
    EXPECT_EQ(copy.GetEntityCount(), count);
    EXPECT_TRUE(copy.AreEquivalent(last, last));
    if constexpr (std::numeric_limits<std::size_t>::max() >
                  std::numeric_limits<compact::VertexId>::max()) {
        EXPECT_THROW(compact::EquivalenceRelation(count + 1), std::length_error);
    }
}

TEST(GkCompactValidatorTest, FirstMergeMaterializesDsuAndPreservesTheEntityDomain) {
    compact::EquivalenceRelation eq(4);
    std::vector<std::vector<compact::VertexId>> const identity{{0}, {1}, {2}, {3}};
    EXPECT_EQ(eq.GetClasses(), identity);
    EXPECT_FALSE(eq.Merge(2, 2));
    EXPECT_THROW(eq.Merge(4, 0), std::out_of_range);
    EXPECT_TRUE(eq.IsIdentity());
    EXPECT_EQ(eq.GetClasses(), identity);
    EXPECT_TRUE(eq.Merge(3, 1));
    EXPECT_FALSE(eq.IsIdentity());
    EXPECT_EQ(eq.GetEntityCount(), 4U);
    EXPECT_EQ(eq.GetClassCount(), 3U);
    EXPECT_EQ(eq.GetClasses(), (std::vector<std::vector<compact::VertexId>>{{0}, {1, 3}, {2}}));
    EXPECT_TRUE(eq.AreEquivalent(1, 3));
    EXPECT_FALSE(eq.AreEquivalent(1, 2));
    EXPECT_FALSE(eq.Merge(1, 3));
    EXPECT_TRUE(eq.Merge(2, 3));
    EXPECT_TRUE(eq.Merge(0, 1));
    EXPECT_EQ(eq.GetClasses(), (std::vector<std::vector<compact::VertexId>>{{0, 1, 2, 3}}));
    EXPECT_EQ(eq.GetEntityCount(), 4U);
    EXPECT_EQ(eq.GetClassCount(), 1U);
    EXPECT_THROW(eq.AreEquivalent(4, 4), std::out_of_range);
}

TEST(GkCompactValidatorTest, EquivalenceCopiesAndMovesPreserveBothStorageModes) {
    static_assert(std::is_nothrow_move_constructible_v<compact::EquivalenceRelation>);
    static_assert(std::is_nothrow_move_assignable_v<compact::EquivalenceRelation>);
    for (bool materialized : {false, true}) {
        compact::EquivalenceRelation original(4);
        if (materialized) original.Merge(0, 1);
        auto const expected = original.GetClasses();
        auto copy = original;
        EXPECT_TRUE(copy.Merge(2, 3));
        EXPECT_EQ(original.GetClasses(), expected);
        copy = original;
        EXPECT_EQ(copy.GetClasses(), expected);
        EXPECT_EQ(copy.IsIdentity(), !materialized);
        compact::EquivalenceRelation moved(std::move(copy));
        EXPECT_EQ(moved.GetClasses(), expected);
        EXPECT_EQ(copy.GetEntityCount(), 0U);
        EXPECT_EQ(copy.GetClassCount(), 0U);
        EXPECT_TRUE(copy.IsIdentity());
        EXPECT_TRUE(copy.GetClasses().empty());
        EXPECT_THROW(copy.Merge(0, 0), std::out_of_range);
        compact::EquivalenceRelation assigned(5);
        assigned.Merge(0, 4);
        assigned = std::move(moved);
        EXPECT_EQ(assigned.GetClasses(), expected);
        EXPECT_EQ(assigned.GetEntityCount(), 4U);
        EXPECT_TRUE(moved.GetClasses().empty());
        EXPECT_TRUE(moved.IsIdentity());
        EXPECT_EQ(moved.GetEntityCount(), 0U);
        // The empty moved-from value can be reused in either mode.
        moved = original;
        EXPECT_EQ(moved.GetClasses(), expected);
    }
}

TEST(GkCompactValidatorTest, JointMappingOwnsAssignmentsAndChecksProjectionsIndependently) {
    compact::JointMapping mapping(3);
    EXPECT_FALSE(mapping.IsBound(0));
    mapping.Bind(0, 1, 2);
    auto const saved = mapping.Get(0);
    EXPECT_FALSE(mapping.IsInjective(1, 3));
    EXPECT_FALSE(mapping.IsInjective(3, 2));
    EXPECT_TRUE(mapping.IsInjective(2, 1));
    mapping.Bind(1, 2, 1);
    mapping.Unbind(0);
    EXPECT_EQ(saved, (compact::JointMapping::Mapping{1, 2}));
    EXPECT_FALSE(mapping.IsBound(0));
    EXPECT_FALSE(mapping.IsInjective(2, 3));
    mapping.Reset();
    for (compact::PatternNodeId node = 0; node < 3; ++node) EXPECT_FALSE(mapping.IsBound(node));
    EXPECT_TRUE(mapping.IsInjective(2, 1));
}

TEST(GkCompactValidatorTest, CompilesConnectedTraversalAndRejectsMalformedDescriptors) {
    auto const graph = compact::BuildGraph(
            {{Entity("a"), "p", Entity("c", "Y")}, {Entity("c", "Y"), "q", Value()}});
    GraphKey key{{{y, "q", z}, {x, "p", y}}, {{x, "X"}, {y, "Y"}}, x};
    auto const compiled = compact::CompileKey(key, graph);
    ASSERT_TRUE(compiled);
    EXPECT_EQ(compiled->traversal_order, (compact::TraversalOrder{1, 0}));
    std::vector<std::uint8_t> reached(compiled->nodes.size(), 0);
    reached[compiled->root] = 1;
    std::set<std::size_t> used;
    for (auto index : compiled->traversal_order) {
        ASSERT_LT(index, compiled->triples.size());
        EXPECT_TRUE(used.insert(index).second);
        auto const& triple = compiled->triples[index];
        EXPECT_TRUE(reached[triple.subject] || reached[triple.object]);
        reached[triple.subject] = reached[triple.object] = 1;
    }
    EXPECT_EQ(used.size(), compiled->triples.size());
    auto missing = key;
    missing.Q.front().predicate = "absent";
    EXPECT_FALSE(compact::CompileKey(missing, graph));
    missing = key;
    missing.T_Q[x] = "absent";
    EXPECT_FALSE(compact::CompileKey(missing, graph));
    GraphKey const absent_constant{{{x, "q", {VarKind::kConstant, "absent"}}}, {{x, "X"}}, x};
    EXPECT_FALSE(compact::CompileKey(absent_constant, graph));
    key.T_Q[x] = "-";
    EXPECT_THROW(compact::CompileKey(key, graph), std::invalid_argument);
    key.T_Q[x] = "X";
    key.root_x = {VarKind::kEntity, "absent"};
    EXPECT_THROW(compact::CompileKey(key, graph), std::invalid_argument);
}

TEST(GkCompactValidatorTest, EvalDistinguishesEntitiesWildcardsValuesAndConstants) {
    Graph const different{{Entity("a"), "p", Entity("c", "Y")},
                          {Entity("b"), "p", Entity("d", "Y")}};
    EXPECT_FALSE(Evaluate(EntityKey(), different));
    EXPECT_TRUE(Evaluate(EntityKey(w), different));
    Graph const same{{Entity("a"), "p", Entity("c", "Y")}, {Entity("b"), "p", Entity("c", "Y")}};
    EXPECT_TRUE(Evaluate(EntityKey(), same));
    Graph const values{{Entity("a"), "name", Value()}, {Entity("b"), "name", Value()}};
    GraphKey const value_key{{{x, "name", z}}, {{x, "X"}}, x};
    GraphKey const constant_key{{{x, "name", constant}}, {{x, "X"}}, x};
    EXPECT_TRUE(Evaluate(value_key, values));
    EXPECT_TRUE(Evaluate(constant_key, values));
    auto unequal = values;
    unequal.back().object = Value("other");
    EXPECT_FALSE(Evaluate(value_key, unequal));
    EXPECT_FALSE(Evaluate(constant_key, unequal));
    auto wrong_type = same;
    for (auto& triple : wrong_type) triple.object.type = "Z";
    EXPECT_FALSE(Evaluate(EntityKey(), wrong_type));
}

TEST(GkCompactValidatorTest, TraversalChecksReadyConstraintsBeforeFurtherExpansion) {
    auto const graph = compact::BuildGraph({{Entity("a"), "p", Entity("c", "Y")},
                                            {Entity("c", "Y"), "q", Value()},
                                            {Entity("c", "Y"), "r", Entity("a")},
                                            {Entity("c", "Y"), "s", Entity("c", "Y")},
                                            {Entity("a"), "t", Entity("a")},
                                            {Entity("a"), "u", Entity("c", "Y")}});
    GraphKey const pattern{
            {{x, "p", w}, {w, "q", z}, {w, "r", x}, {w, "s", w}, {x, "t", x}, {x, "u", w}},
            {{x, "X"}, {w, "Y"}},
            x};
    auto const key = *compact::CompileKey(pattern, graph);
    std::vector<std::uint8_t> reached(key.nodes.size(), 0), used(key.triples.size(), 0);
    reached[key.root] = 1;
    for (auto index : key.traversal_order) {
        auto const& next = key.triples[index];
        if (!reached[next.subject] || !reached[next.object]) {
            for (std::size_t other = 0; other < key.triples.size(); ++other) {
                auto const& triple = key.triples[other];
                EXPECT_FALSE(!used[other] && reached[triple.subject] && reached[triple.object]);
            }
        }
        reached[next.subject] = reached[next.object] = 1;
        used[index] = 1;
    }
}

TEST(GkCompactValidatorTest, EvalIntersectionsAndConstantLookupsRespectBacktrackingAndSelections) {
    auto const graph = compact::BuildGraph({{Entity("a"), "p", Entity("c", "Y")},
                                            {Entity("b"), "p", Entity("c", "Y")},
                                            {Entity("a"), "p", Value("early")},
                                            {Entity("b"), "p", Value("early")},
                                            {Entity("a"), "p", Value("v")},
                                            {Entity("b"), "p", Value("v")},
                                            {Entity("a"), "q", Value("v")},
                                            {Entity("b"), "q", Value("v")}});
    auto const a = *graph.FindEntity("a"), b = *graph.FindEntity("b");
    auto const value = *graph.FindValue("v");
    std::vector<compact::VertexId> selected;
    for (auto vertex : graph.VertexIds())
        if (vertex != value) selected.push_back(vertex);
    compact::GraphView const incomplete(graph, selected);
    compact::EquivalenceRelation const eq(graph.GetEntityCount());
    for (auto target : {z, constant}) {
        GraphKey const pattern{{{x, "p", target}, {x, "q", target}}, {{x, "X"}}, x};
        auto const key = *compact::CompileKey(pattern, graph);
        compact::EvalGK evaluator(key);
        EXPECT_TRUE(evaluator.Evaluate({graph, graph, a, b}, eq));
        EXPECT_FALSE(evaluator.Evaluate({graph, incomplete, a, b}, eq));
        EXPECT_FALSE(evaluator.Evaluate({incomplete, graph, a, b}, eq));
        EXPECT_TRUE(evaluator.Evaluate({graph, graph, b, a}, eq));
        EXPECT_TRUE(ReferenceEvaluate(key, {graph, graph, a, b}, eq));
    }
    GraphKey const collision{{{x, "p", z}, {x, "q", constant}, {x, "q", z}}, {{x, "X"}}, x};
    compact::EvalGK evaluator(*compact::CompileKey(collision, graph));
    EXPECT_FALSE(evaluator.Evaluate({graph, graph, a, b}, eq));
}

TEST(GkCompactValidatorTest, EvalBacktracksHandlesIncomingEdgesAndChecksCycles) {
    Graph const source{{Entity("a"), "p", Entity("c", "Y")},
                       {Entity("a"), "p", Entity("d", "Y")},
                       {Entity("b"), "p", Entity("d", "Y")},
                       {Entity("d", "Y"), "q", Value()}};
    GraphKey const key{{{y, "q", z}, {x, "p", y}}, {{x, "X"}, {y, "Y"}}, x};
    EXPECT_TRUE(Evaluate(key, source));
    Graph const incoming{{Entity("c", "Y"), "p", Entity("a")},
                         {Entity("c", "Y"), "p", Entity("b")}};
    GraphKey const reverse{{{y, "p", x}}, {{x, "X"}, {y, "Y"}}, x};
    EXPECT_TRUE(Evaluate(reverse, incoming));
    Graph cycle{{Entity("a"), "p", Entity("c", "Y")},
                {Entity("b"), "p", Entity("d", "Y")},
                {Entity("c", "Y"), "q", Entity("a")},
                {Entity("d", "Y"), "q", Entity("b")}};
    GraphKey const cyclic{{{x, "p", w}, {w, "q", x}}, {{x, "X"}, {w, "Y"}}, x};
    EXPECT_TRUE(Evaluate(cyclic, cycle));
    cycle.pop_back();
    EXPECT_FALSE(Evaluate(cyclic, cycle));
    GraphKey const loop{{{x, "p", x}}, {{x, "X"}}, x};
    EXPECT_TRUE(Evaluate(loop, {{Entity("a"), "p", Entity("a")}, {Entity("b"), "p", Entity("b")}}));
}

TEST(GkCompactValidatorTest, EvalKeepsInjectivityAfterEquivalenceMergesAndResetsMapping) {
    auto const graph = compact::BuildGraph({{Entity("a"), "p", Entity("a")},
                                            {Entity("b"), "p", Entity("b")},
                                            {Entity("a"), "p", Entity("c")},
                                            {Entity("b"), "p", Entity("d")}});
    GraphKey const key{{{x, "p", y}}, {{x, "X"}, {y, "X"}}, x};
    compact::EvalGK evaluator(*compact::CompileKey(key, graph));
    auto const a = *graph.FindEntity("a"), b = *graph.FindEntity("b");
    compact::EquivalenceRelation eq(graph.Metadata().GetEntityCount());
    EXPECT_FALSE(evaluator.Evaluate({graph, graph, a, b}, eq));
    eq.Merge(a, b);
    EXPECT_FALSE(evaluator.Evaluate({graph, graph, a, b}, eq));
    eq.Merge(*graph.FindEntity("c"), *graph.FindEntity("d"));
    EXPECT_TRUE(evaluator.Evaluate({graph, graph, a, b}, eq));
    compact::EquivalenceRelation const identity(graph.Metadata().GetEntityCount());
    EXPECT_FALSE(evaluator.Evaluate({graph, graph, a, b}, identity));
    EXPECT_TRUE(evaluator.Evaluate({graph, graph, b, a}, eq));

    GraphKey const distinct_values{{{x, "name", z}, {x, "other", other_value}}, {{x, "X"}}, x};
    EXPECT_FALSE(Evaluate(distinct_values, {{Entity("a"), "name", Value()},
                                            {Entity("a"), "other", Value()},
                                            {Entity("b"), "name", Value()},
                                            {Entity("b"), "other", Value()}}));
}

TEST(GkCompactValidatorTest, SparseInputsUseOriginalIdsAndTheFullEquivalenceDomain) {
    auto const original = compact::BuildGraph({{Entity("0"), "p", Entity("0")},
                                               {Entity("a"), "p", Entity("c", "Y")},
                                               {Entity("b"), "p", Entity("c", "Y")},
                                               {Entity("c", "Y"), "q", Value()}});
    auto const a = *original.FindEntity("a"), b = *original.FindEntity("b");
    auto const c = *original.FindEntity("c"), v = *original.FindValue("v");
    auto first = Select(original, {a, c, v});
    auto second = Select(original, {b, c, v});
    ASSERT_GE(c, first.GetEntityCount());
    GraphKey const key{{{x, "p", y}, {y, "q", z}}, {{x, "X"}, {y, "Y"}}, x};
    compact::EvalGK evaluator(*compact::CompileKey(key, original));
    compact::EquivalenceRelation const eq(original.Metadata().GetEntityCount());
    EXPECT_TRUE(evaluator.Evaluate({first, second, a, b}, eq));
    EXPECT_TRUE(evaluator.Evaluate({second, first, b, a}, eq));
    auto empty_edges = Select(original, {b});
    EXPECT_FALSE(evaluator.Evaluate({first, empty_edges, a, b}, eq));
    EXPECT_TRUE(evaluator.Evaluate({first, second, a, b}, eq));
    std::shared_ptr<compact::Graph const> graph =
            std::make_shared<compact::Graph const>(Select(original, {a, b, c, v}));
    algos::CompactGkValidator validator;
    validator.SetOption(config::names::kGkGraphData, graph);
    validator.SetOption(config::names::kGkKeyData, key);
    validator.LoadData();
    Execute(validator);
    EXPECT_EQ(validator.GetViolations(), (std::vector<gk::EntityPair>{{"a", "b"}}));
}

TEST(GkCompactValidatorTest, AdapterOwnsSharedGraphReportsAllPairsAndResetsResults) {
    using namespace config::names;
    algos::CompactGkValidator validator;
    EXPECT_THROW(validator.Holds(), std::logic_error);
    std::shared_ptr<compact::Graph const> graph = std::make_shared<compact::Graph const>(
            compact::BuildGraph({{Entity("a"), "p", Entity("t", "Y")},
                                 {Entity("b"), "p", Entity("t", "Y")},
                                 {Entity("c"), "p", Entity("t", "Y")}}));
    std::weak_ptr<compact::Graph const> weak = graph;
    validator.SetOption(kGkGraphData, graph);
    validator.SetOption(kGkKeyData, EntityKey());
    EXPECT_EQ(boost::any_cast<std::shared_ptr<compact::Graph const>>(
                      validator.GetOptValues().at(kGkGraphData).value)
                      .get(),
              graph.get());
    graph.reset();
    EXPECT_FALSE(weak.expired());
    validator.LoadData();
    std::vector<gk::EntityPair> const expected{{"a", "b"}, {"a", "c"}, {"b", "c"}};
    for (int run = 0; run < 2; ++run) {
        Execute(validator);
        EXPECT_FALSE(validator.Holds());
        EXPECT_EQ(validator.GetViolations(), expected);
    }
}

TEST(GkCompactValidatorTest, AdapterRejectsNullAndAmbiguousSourcesAndHandlesMissingEmbeddings) {
    using namespace config::names;
    for (bool null_graph : {true, false}) {
        algos::CompactGkValidator validator;
        std::shared_ptr<compact::Graph const> graph;
        if (!null_graph) graph = std::make_shared<compact::Graph const>();
        validator.SetOption(kGkGraphData, graph);
        validator.SetOption(kGkKeyData, EntityKey());
        if (!null_graph) validator.SetOption(kGkGraphPath, std::filesystem::path("unused.csv"));
        EXPECT_THROW(validator.LoadData(), config::ConfigurationError);
    }
    algos::CompactGkValidator empty;
    empty.SetOption(kGkGraphData, std::make_shared<compact::Graph const>());
    empty.SetOption(kGkKeyData, EntityKey());
    empty.LoadData();
    Execute(empty);
    EXPECT_TRUE(empty.Holds());
    EXPECT_TRUE(empty.GetViolations().empty());
    Execute(empty, true);
    EXPECT_TRUE(empty.Holds());
    EXPECT_TRUE(empty.GetViolations().empty());
}

TEST(GkCompactValidatorTest, AdapterRejectsExecutionAfterFailedReloadAndRecovers) {
    using namespace config::names;
    auto const path = std::filesystem::temp_directory_path() / "gk_compact_failed_reload.csv";
    std::string const header =
            "subject_id,subject_type,predicate,object_kind,object_id_or_value,object_type\n";
    std::string const csv = header + "a,X,p,entity,t,Y\nb,X,p,entity,t,Y\n";
    auto write = [&](std::string const& content) { std::ofstream(path) << content; };
    write(csv);
    algos::CompactGkValidator validator;
    validator.SetOption(kGkGraphPath, path);
    validator.SetOption(kGkKeyData, EntityKey());
    validator.LoadData();
    Execute(validator, true, true, true);
    EXPECT_EQ(validator.GetViolations(), (std::vector<gk::EntityPair>{{"a", "b"}}));

    write("invalid CSV\n");
    validator.SetOption(kGkUseLocalization, false);
    EXPECT_THROW(validator.LoadData(), std::runtime_error);
    EXPECT_THROW(validator.GetResult(), std::logic_error);
    EXPECT_THROW(validator.Execute(), std::logic_error);
    EXPECT_THROW(validator.GetResult(), std::logic_error);

    // A successful load with no embeddings is distinct from a failed load.
    write(header);
    validator.SetOption(kGkUseLocalization, false);
    validator.LoadData();
    Execute(validator);
    EXPECT_TRUE(validator.Holds());
    EXPECT_TRUE(validator.GetViolations().empty());

    write(csv);
    validator.SetOption(kGkUseLocalization, false);
    validator.LoadData();
    Execute(validator, true, true, true);
    EXPECT_EQ(validator.GetViolations(), (std::vector<gk::EntityPair>{{"a", "b"}}));
    std::filesystem::remove(path);
}

TEST(GkCompactValidatorTest, ConfigAcceptsOnlyOptimizationDependencyPrefixes) {
    for (bool localization : {false, true})
        for (bool pairing : {false, true})
            for (bool reduction : {false, true}) {
                if ((pairing && !localization) || (reduction && !pairing)) {
                    EXPECT_THROW(config::CheckGkOptimizationDependencies(localization, pairing,
                                                                         reduction),
                                 config::ConfigurationError);
                } else {
                    EXPECT_NO_THROW(config::CheckGkOptimizationDependencies(localization, pairing,
                                                                            reduction));
                }
            }
}

TEST(GkCompactValidatorTest, GraphViewsFilterBothEndpointsWithoutCopyingCsr) {
    auto const graph = compact::BuildGraph({{Entity("a"), "p", Entity("b")},
                                            {Entity("c"), "p", Entity("a")},
                                            {Entity("b"), "q", Entity("c")},
                                            {Entity("b"), "q", Entity("outside")},
                                            {Entity("outside"), "q", Entity("b")},
                                            {Entity("b"), "q", Entity("b")},
                                            {Entity("b"), "name", Value()}});
    auto const a = *graph.FindEntity("a"), b = *graph.FindEntity("b");
    auto const c = *graph.FindEntity("c"), outside = *graph.FindEntity("outside");
    auto const value = *graph.FindValue("v");
    auto const p = *graph.FindPredicate("p"), q = *graph.FindPredicate("q");
    std::vector<compact::VertexId> const vertices{a, b, c, value};
    compact::GraphView const view(graph, vertices);
    EXPECT_EQ(&view.Base(), &graph);
    EXPECT_EQ(view.GetVertexCount(), 4U);
    EXPECT_EQ(view.GetEntityCount(), 3U);
    EXPECT_TRUE(view.ContainsVertex(value));
    EXPECT_FALSE(view.ContainsVertex(outside));
    EXPECT_TRUE(view.HasEdge(a, p, b));
    EXPECT_TRUE(view.HasEdge(b, q, c));
    EXPECT_TRUE(view.HasEdge(b, q, b));
    EXPECT_FALSE(view.HasEdge(b, q, outside));
    EXPECT_FALSE(view.HasEdge(outside, q, b));
    EXPECT_TRUE(std::ranges::empty(view.Outgoing(outside)));
    EXPECT_TRUE(std::ranges::empty(view.Incoming(outside, q)));
    auto const materialized = Select(graph, vertices);
    for (auto vertex : vertices) {
        EXPECT_TRUE(std::ranges::equal(view.Outgoing(vertex), materialized.Outgoing(vertex)));
        EXPECT_TRUE(std::ranges::equal(view.Incoming(vertex), materialized.Incoming(vertex)));
        for (auto predicate : {p, q}) {
            EXPECT_TRUE(std::ranges::equal(view.Outgoing(vertex, predicate),
                                           materialized.Outgoing(vertex, predicate)));
            EXPECT_TRUE(std::ranges::equal(view.Incoming(vertex, predicate),
                                           materialized.Incoming(vertex, predicate)));
        }
    }
    // A range returned from a temporary view does not refer to that view object.
    auto outgoing = compact::GraphView(graph, vertices).Outgoing(a, p);
    ASSERT_NE(outgoing.begin(), outgoing.end());
    EXPECT_EQ(&*outgoing.begin(), graph.Outgoing(a, p).data());
    compact::GraphView const full(graph);
    EXPECT_EQ(full.GetVertexCount(), graph.GetVertexCount());
    EXPECT_TRUE(full.HasEdge(b, q, outside));
    compact::GraphView const empty(graph, std::span<compact::VertexId const>{});
    EXPECT_EQ(empty.GetVertexCount(), 0U);
    EXPECT_EQ(empty.GetEntityCount(), 0U);
    EXPECT_TRUE(std::ranges::empty(empty.VertexIds()));
    EXPECT_FALSE(empty.ContainsVertex(a));
    EXPECT_FALSE(empty.HasEdge(a, p, b));
    EXPECT_TRUE(std::ranges::empty(empty.Outgoing(a)));
}

TEST(GkCompactValidatorTest, PatternRadiusUsesShortestUndirectedDistancesNotTraversalLength) {
    Variable const t{VarKind::kWildcard, "t"};
    auto const graph = compact::BuildGraph({{Entity("a"), "p", Entity("b")}});
    std::vector<std::pair<GraphKey, std::size_t>> const cases{
            {{{{x, "p", x}}, {{x, "X"}}, x}, 0},
            {{{{y, "p", x}}, {{x, "X"}, {y, "X"}}, x}, 1},
            {{{{y, "p", x}, {w, "p", y}, {t, "p", w}}, {{x, "X"}, {y, "X"}, {w, "X"}, {t, "X"}}, x},
             3},
            {{{{x, "p", y}, {y, "p", w}, {w, "p", t}, {x, "p", w}, {t, "p", x}},
              {{x, "X"}, {y, "X"}, {w, "X"}, {t, "X"}},
              x},
             1}};
    for (auto const& [key, expected] : cases) {
        auto const compiled = compact::CompileKey(key, graph);
        ASSERT_TRUE(compiled);
        EXPECT_EQ(compact::GetPatternRadius(*compiled), expected);
    }
}

TEST(GkCompactValidatorTest, NeighborhoodsRetainIsolatedRootsAndSparseOriginalIds) {
    Graph triples;
    for (std::size_t i = 0; i < 90; ++i) {
        auto const node = Entity("e" + std::to_string(i));
        triples.push_back({node, "loop", node});
    }
    auto const original = compact::BuildGraph(triples);
    compact::GraphData data;
    data.metadata = original.Data().metadata;
    data.row_to_vertex = {2, 65, 70, 89};
    data.outgoing.offsets = {0, 1, 2, 2, 2};
    data.incoming.offsets = {0, 0, 1, 2, 2};
    data.outgoing.edges = {{0, 65}, {0, 70}};
    data.incoming.edges = {{0, 2}, {0, 65}};
    data.input_triple_count = 2;
    compact::Graph const sparse(std::move(data));
    std::array<compact::VertexId, 3> const roots{65, 89, 70};
    compact::Neighborhoods const neighborhoods(sparse, roots, 1);
    std::vector<std::vector<compact::VertexId>> const expected{{2, 65, 70}, {89}, {65, 70}};
    ASSERT_EQ(neighborhoods.size(), roots.size());
    for (std::size_t i = 0; i < roots.size(); ++i)
        EXPECT_TRUE(std::ranges::equal(neighborhoods[i].VertexIds(), expected[i]));
    compact::GraphView const full(sparse);
    EXPECT_TRUE(
            std::ranges::equal(full.VertexIds(), std::vector<compact::VertexId>{2, 65, 70, 89}));
    compact::Neighborhoods const singletons(sparse, roots, 0);
    for (std::size_t i = 0; i < roots.size(); ++i) {
        EXPECT_EQ(singletons[i].GetVertexCount(), 1U);
        EXPECT_TRUE(singletons[i].ContainsVertex(roots[i]));
        EXPECT_TRUE(std::ranges::empty(singletons[i].Outgoing(roots[i])));
    }
    compact::Neighborhoods const none(sparse, {}, 3);
    EXPECT_EQ(none.size(), 0U);
    EXPECT_THROW(none[0], std::out_of_range);
    EXPECT_THROW(neighborhoods[roots.size()], std::out_of_range);
    auto moved = compact::Neighborhoods(sparse, roots, 1);
    auto const borrowed = moved[0];
    auto owner = std::move(moved);
    EXPECT_EQ(owner.size(), roots.size());
    EXPECT_TRUE(std::ranges::equal(borrowed.VertexIds(), expected[0]));
    EXPECT_TRUE(std::ranges::equal(owner[0].VertexIds(), expected[0]));
    std::array<compact::VertexId, 1> const inactive{64};
    EXPECT_THROW(compact::Neighborhoods(sparse, inactive, 1), std::invalid_argument);
    std::array<compact::VertexId, 1> const invalid{100};
    EXPECT_THROW(compact::Neighborhoods(sparse, invalid, 1), std::invalid_argument);
    compact::Graph const empty;
    EXPECT_EQ(compact::Neighborhoods(empty, {}, 0).size(), 0U);
}

TEST(GkCompactValidatorTest, NeighborhoodsMatchIndependentDistancesAndInducedEdges) {
    // All directed non-loop edges on three entities, plus shared values and
    // loops. Floyd-Warshall provides an independent oracle for the BFS selections.
    for (unsigned mask = 0; mask < 64; ++mask) {
        SCOPED_TRACE(mask);
        Graph triples;
        unsigned bit = 0;
        for (unsigned i = 0; i < 3; ++i) {
            auto const subject = Entity(std::to_string(i));
            triples.push_back({subject, "name", Value(i == 2 ? "other" : "v")});
            triples.push_back({subject, "loop", subject});
            for (unsigned j = 0; j < 3; ++j) {
                if (i == j) continue;
                if (mask & (1U << bit))
                    triples.push_back({subject, "p", Entity(std::to_string(j))});
                ++bit;
            }
        }
        auto const graph = compact::BuildGraph(triples);
        auto const count = graph.GetVertexCount();
        std::vector<compact::VertexId> roots;
        for (auto vertex : graph.VertexIds()) roots.push_back(vertex);
        std::vector<std::vector<std::size_t>> distance(count,
                                                       std::vector<std::size_t>(count, count));
        for (auto vertex : roots) distance[vertex][vertex] = 0;
        for (auto subject : roots) {
            for (auto const& edge : graph.Outgoing(subject)) {
                auto const object = edge.neighbor;
                if (subject != object) distance[subject][object] = distance[object][subject] = 1;
            }
        }
        for (auto middle : roots)
            for (auto first : roots)
                for (auto second : roots)
                    distance[first][second] =
                            std::min(distance[first][second],
                                     distance[first][middle] + distance[middle][second]);
        for (std::size_t radius = 0; radius < count; ++radius) {
            SCOPED_TRACE(radius);
            compact::Neighborhoods const neighborhoods(graph, roots, radius);
            for (std::size_t i = 0; i < roots.size(); ++i) {
                std::vector<compact::VertexId> selected;
                for (auto vertex : roots) {
                    bool const expected = distance[roots[i]][vertex] <= radius;
                    EXPECT_EQ(neighborhoods[i].ContainsVertex(vertex), expected);
                    if (expected) selected.push_back(vertex);
                }
                EXPECT_TRUE(std::ranges::equal(neighborhoods[i].VertexIds(), selected));
                auto const materialized = Select(graph, selected);
                for (auto vertex : roots) {
                    EXPECT_TRUE(std::ranges::equal(neighborhoods[i].Outgoing(vertex),
                                                   materialized.Outgoing(vertex)));
                    EXPECT_TRUE(std::ranges::equal(neighborhoods[i].Incoming(vertex),
                                                   materialized.Incoming(vertex)));
                }
            }
        }
    }
}

TEST(GkCompactValidatorTest, EvalUsesSelectionsAndMatchesMaterializedSubgraphs) {
    auto const graph = compact::BuildGraph({{Entity("a"), "p", Entity("c", "Y")},
                                            {Entity("b"), "p", Entity("d", "Y")},
                                            {Entity("c", "Y"), "q", Value()},
                                            {Entity("d", "Y"), "q", Value()}});
    auto const a = *graph.FindEntity("a"), b = *graph.FindEntity("b");
    auto const c = *graph.FindEntity("c"), d = *graph.FindEntity("d");
    auto const value = *graph.FindValue("v");
    GraphKey const key{{{x, "p", w}, {w, "q", z}}, {{x, "X"}, {w, "Y"}}, x};
    compact::EvalGK evaluator(*compact::CompileKey(key, graph));
    compact::EquivalenceRelation const eq(graph.Metadata().GetEntityCount());
    std::vector<compact::VertexId> const first{a, c, value}, second{b, d, value};
    std::vector<compact::VertexId> const incomplete{b, d};
    EXPECT_TRUE(evaluator.Evaluate(
            {compact::GraphView(graph, first), compact::GraphView(graph, second), a, b}, eq));
    EXPECT_FALSE(evaluator.Evaluate(
            {compact::GraphView(graph, first), compact::GraphView(graph, incomplete), a, b}, eq));
    auto const materialized_first = Select(graph, first),
               materialized_second = Select(graph, second);
    EXPECT_TRUE(evaluator.Evaluate({materialized_first, materialized_second, a, b}, eq));
    EXPECT_TRUE(evaluator.Evaluate({graph, graph, a, b}, eq));
}

TEST(GkCompactValidatorTest, OptimizationsPreserveViolationsAndMatchReferencePairing) {
    using namespace config::names;
    for (unsigned mask = 0; mask < 64; ++mask) {
        Graph triples;
        unsigned bit = 0;
        for (unsigned i = 0; i < 3; ++i) {
            auto const subject = Entity(std::to_string(i));
            triples.push_back({subject, "name", Value(i == 2 ? "other" : "v")});
            triples.push_back({subject, "loop", subject});
            for (unsigned j = 0; j < 3; ++j) {
                if (i == j) continue;
                if (mask & (1U << bit))
                    triples.push_back({subject, "p", Entity(std::to_string(j))});
                ++bit;
            }
        }
        std::shared_ptr<compact::Graph const> graph =
                std::make_shared<compact::Graph const>(compact::BuildGraph(triples));
        std::vector<GraphKey> const keys{
                {{{x, "name", z}}, {{x, "X"}}, x},
                {{{x, "loop", x}}, {{x, "X"}}, x},
                {{{x, "p", w}}, {{x, "X"}, {w, "X"}}, x},
                {{{y, "p", x}}, {{x, "X"}, {y, "X"}}, x},
                {{{x, "p", y}, {y, "name", z}}, {{x, "X"}, {y, "X"}}, x},
                {{{x, "p", w}, {w, "p", x}}, {{x, "X"}, {w, "X"}}, x},
                {{{x, "name", constant}}, {{x, "X"}}, x},
                {{{x, "p", y}, {x, "p", w}, {y, "p", w}}, {{x, "X"}, {y, "X"}, {w, "X"}}, x}};
        for (auto const& key : keys) {
            SCOPED_TRACE(mask);
            algos::CompactGkValidator validator;
            validator.SetOption(kGkGraphData, graph);
            validator.SetOption(kGkKeyData, key);
            validator.LoadData();
            Execute(validator);
            auto const expected = validator.GetViolations();
            for (auto const [localization, pairing, reduction] :
                 {std::array{true, false, false}, std::array{true, true, false},
                  std::array{true, true, true}, std::array{false, false, false},
                  std::array{true, true, true}}) {
                Execute(validator, localization, pairing, reduction);
                EXPECT_EQ(validator.GetViolations(), expected);
            }
            auto const compiled = compact::CompileKey(key, *graph);
            if (!compiled) continue;
            ExpectReferencePairing(*compiled, *graph, *graph);
            std::vector<compact::VertexId> roots;
            for (auto vertex : graph->EntityIds()) roots.push_back(vertex);
            auto const neighborhoods = compact::PrepareNeighborhoods(*graph, roots, *compiled);
            compact::EquivalenceRelation identity(graph->Metadata().GetEntityCount());
            compact::EquivalenceRelation merged(graph->Metadata().GetEntityCount());
            for (auto root : roots) merged.Merge(roots.front(), root);
            compact::EvalGK evaluator(*compiled);
            for (std::size_t i = 0; i < roots.size(); ++i) {
                for (std::size_t j = i + 1; j < roots.size(); ++j) {
                    ExpectReferencePairing(*compiled, neighborhoods[i], neighborhoods[j]);
                    auto first_vertices = neighborhoods[i].VertexIds();
                    auto second_vertices = neighborhoods[j].VertexIds();
                    auto const materialized_first =
                            Select(*graph, std::vector<compact::VertexId>(first_vertices.begin(),
                                                                          first_vertices.end()));
                    auto const materialized_second =
                            Select(*graph, std::vector<compact::VertexId>(second_vertices.begin(),
                                                                          second_vertices.end()));
                    for (auto const* eq : {&identity, &merged}) {
                        auto const input = compact::EvalGKInput{neighborhoods[i], neighborhoods[j],
                                                                roots[i], roots[j]};
                        bool const exact = ReferenceEvaluate(
                                *compiled, {*graph, *graph, roots[i], roots[j]}, *eq);
                        EXPECT_EQ(evaluator.Evaluate({*graph, *graph, roots[i], roots[j]}, *eq),
                                  exact);
                        EXPECT_EQ(evaluator.Evaluate(input, *eq), exact);
                        EXPECT_EQ(evaluator.Evaluate({materialized_first, materialized_second,
                                                      roots[i], roots[j]},
                                                     *eq),
                                  exact);
                        for (bool reduction : {false, true})
                            EXPECT_EQ(compact::EvaluatePair(evaluator, *compiled, input, *eq, true,
                                                            reduction),
                                      exact);
                    }
                }
            }
        }
    }
}

TEST(GkCompactValidatorTest, PairingDoesNotDependOnEquivalenceOrRequireInjectivity) {
    auto const graph = compact::BuildGraph(
            {{Entity("a"), "p", Entity("c", "Y")}, {Entity("b"), "p", Entity("d", "Y")}});
    auto const a = *graph.FindEntity("a"), b = *graph.FindEntity("b");
    auto const c = *graph.FindEntity("c"), d = *graph.FindEntity("d");
    auto const key = *compact::CompileKey(EntityKey(), graph);
    auto const pairing = compact::ComputePairing(key, graph, graph);
    EXPECT_TRUE(pairing.Contains(key.root, a, b));
    EXPECT_TRUE(pairing.Contains(key.root, a, a));
    ExpectReferencePairing(key, graph, graph);
    compact::EvalGK evaluator(key);
    compact::EquivalenceRelation eq(graph.Metadata().GetEntityCount());
    EXPECT_FALSE(compact::EvaluatePair(evaluator, key, {graph, graph, a, b}, eq, true, true));
    eq.Merge(c, d);
    EXPECT_TRUE(compact::EvaluatePair(evaluator, key, {graph, graph, a, b}, eq, true, true));
    EXPECT_EQ(compact::ComputePairing(key, graph, graph).pairs, pairing.pairs);

    Variable const other_wildcard{VarKind::kWildcard, "other_wildcard"};
    GraphKey const noninjective{{{x, "p", w}, {x, "q", other_wildcard}},
                                {{x, "X"}, {w, "Y"}, {other_wildcard, "Y"}},
                                x};
    auto const repeated = compact::BuildGraph({{Entity("a"), "p", Entity("c", "Y")},
                                               {Entity("a"), "q", Entity("c", "Y")},
                                               {Entity("b"), "p", Entity("d", "Y")},
                                               {Entity("b"), "q", Entity("d", "Y")}});
    auto const compiled = *compact::CompileKey(noninjective, repeated);
    auto const first = *repeated.FindEntity("a"), second = *repeated.FindEntity("b");
    std::array<compact::VertexId, 2> const roots{first, second};
    auto const neighborhoods = compact::PrepareNeighborhoods(repeated, roots, compiled);
    EXPECT_TRUE(compact::ComputePairing(compiled, neighborhoods[0], neighborhoods[1])
                        .Contains(compiled.root, first, second));
    compact::EvalGK exact(compiled);
    compact::EquivalenceRelation const identity(repeated.Metadata().GetEntityCount());
    EXPECT_FALSE(compact::EvaluatePair(exact, compiled,
                                       {neighborhoods[0], neighborhoods[1], first, second},
                                       identity, true, true));
}

TEST(GkCompactValidatorTest, PairingHonorsValueConstantTypeAndViewDomains) {
    auto const graph = compact::BuildGraph({{Entity("a"), "name", Value("a")},
                                            {Entity("b"), "name", Value("a")},
                                            {Entity("a"), "state", Value("fixed")},
                                            {Entity("b"), "state", Value("other")},
                                            {Entity("foreign", "Y"), "state", Value("fixed")}});
    auto const a = *graph.FindEntity("a"), b = *graph.FindEntity("b");
    GraphKey const value_key{{{x, "name", z}}, {{x, "X"}}, x};
    GraphKey const constant_key{{{x, "state", {VarKind::kConstant, "fixed"}}}, {{x, "X"}}, x};
    for (auto const& key : {value_key, constant_key}) {
        auto const compiled = *compact::CompileKey(key, graph);
        ExpectReferencePairing(compiled, graph, graph);
        auto const relation = compact::ComputePairing(compiled, graph, graph);
        EXPECT_EQ(relation.Contains(compiled.root, a, b), key.Q.front().predicate == "name");
        std::vector<compact::VertexId> const roots_only{a, b};
        auto const empty =
                compact::ComputePairing(compiled, compact::GraphView(graph, roots_only), graph);
        EXPECT_TRUE(empty.pairs.empty());
        EXPECT_EQ(empty.offsets, std::vector<compact::Offset>(compiled.nodes.size() + 1, 0));
    }
    auto const compiled = *compact::CompileKey(value_key, graph);
    compact::GraphView const empty_view(graph, std::span<compact::VertexId const>{});
    EXPECT_TRUE(compact::ComputePairing(compiled, empty_view, graph).pairs.empty());
    EXPECT_TRUE(compact::ComputePairing(compiled, graph, empty_view).pairs.empty());
}

TEST(GkCompactValidatorTest, PairingPropagatesSupportLossToAFixedPointAndKeepsCycles) {
    auto const graph = compact::BuildGraph({{Entity("a"), "p", Entity("c", "Y")},
                                            {Entity("c", "Y"), "q", Entity("e", "Z")},
                                            {Entity("e", "Z"), "r", Value()},
                                            {Entity("b"), "p", Entity("d", "Y")},
                                            {Entity("d", "Y"), "q", Entity("f", "Z")},
                                            {Entity("f", "Z"), "other", Value()},
                                            {Entity("b2"), "p", Entity("d2", "Y")},
                                            {Entity("d2", "Y"), "q", Entity("f2", "Z")},
                                            {Entity("f2", "Z"), "r", Value()}});
    GraphKey const pattern{
            {{x, "p", y}, {y, "q", w}, {w, "r", z}}, {{x, "X"}, {y, "Y"}, {w, "Z"}}, x};
    auto const key = *compact::CompileKey(pattern, graph);
    auto const a = *graph.FindEntity("a"), b = *graph.FindEntity("b"), b2 = *graph.FindEntity("b2");
    std::vector<compact::VertexId> first{a, *graph.FindEntity("c"), *graph.FindEntity("e"),
                                         *graph.FindValue("v")};
    std::vector<compact::VertexId> second{b,
                                          b2,
                                          *graph.FindEntity("d"),
                                          *graph.FindEntity("d2"),
                                          *graph.FindEntity("f"),
                                          *graph.FindEntity("f2"),
                                          *graph.FindValue("v")};
    std::ranges::sort(first);
    std::ranges::sort(second);
    compact::GraphView const left(graph, first), right(graph, second);
    ExpectReferencePairing(key, left, right);
    auto const relation = compact::ComputePairing(key, left, right);
    EXPECT_FALSE(relation.Contains(key.root, a, b));
    EXPECT_TRUE(relation.Contains(key.root, a, b2));
    EXPECT_EQ(relation.Candidates(key.root).size(), 1U);

    auto const cycle =
            compact::BuildGraph({{Entity("a"), "p", Entity("b")}, {Entity("b"), "p", Entity("a")}});
    GraphKey const loop_pattern{{{x, "p", x}}, {{x, "X"}}, x};
    auto const loop = *compact::CompileKey(loop_pattern, cycle);
    ExpectReferencePairing(loop, cycle, cycle);
    EXPECT_TRUE(compact::ComputePairing(loop, cycle, cycle)
                        .Contains(loop.root, *cycle.FindEntity("a"), *cycle.FindEntity("b")));
    EXPECT_FALSE(Evaluate(loop_pattern,
                          {{Entity("a"), "p", Entity("b")}, {Entity("b"), "p", Entity("a")}}));
}

TEST(GkCompactValidatorTest, ReductionUsesAllProjectionsAndPreservesInducedEdgesWithoutCopies) {
    auto const graph = compact::BuildGraph({{Entity("a"), "p", Entity("c", "Y")},
                                            {Entity("b"), "p", Entity("d", "Y")},
                                            {Entity("c", "Y"), "name", Value()},
                                            {Entity("d", "Y"), "name", Value()},
                                            {Entity("a"), "extra", Value()},
                                            {Entity("c", "Y"), "extra", Entity("a")},
                                            {Entity("outside"), "p", Entity("c", "Y")}});
    auto const a = *graph.FindEntity("a"), b = *graph.FindEntity("b");
    auto const c = *graph.FindEntity("c"), d = *graph.FindEntity("d"), v = *graph.FindValue("v");
    compact::PairingRelation const relation{{0, 1, 2, 3, 4}, {{a, b}, {c, d}, {v, v}, {c, d}}};
    auto const first = compact::ReduceSubgraph(graph, relation, compact::PairingProjection::kFirst);
    auto const second =
            compact::ReduceSubgraph(graph, relation, compact::PairingProjection::kSecond);
    EXPECT_EQ(first, (std::vector<compact::VertexId>{a, c, v}));
    EXPECT_EQ(second, (std::vector<compact::VertexId>{b, d, v}));
    compact::GraphView const view(graph, first);
    EXPECT_TRUE(view.HasEdge(a, *graph.FindPredicate("extra"), v));
    EXPECT_TRUE(view.HasEdge(c, *graph.FindPredicate("extra"), a));
    EXPECT_FALSE(view.ContainsVertex(*graph.FindEntity("outside")));
    auto edges = view.Outgoing(a, *graph.FindPredicate("p"));
    ASSERT_NE(edges.begin(), edges.end());
    EXPECT_EQ(&*edges.begin(), graph.Outgoing(a, *graph.FindPredicate("p")).data());
    auto const materialized = Select(graph, first);
    for (auto vertex : first) {
        EXPECT_TRUE(std::ranges::equal(view.Outgoing(vertex), materialized.Outgoing(vertex)));
        EXPECT_TRUE(std::ranges::equal(view.Incoming(vertex), materialized.Incoming(vertex)));
    }
    compact::PairingRelation const empty{{0, 0}, {}};
    EXPECT_TRUE(compact::ReduceSubgraph(graph, empty, compact::PairingProjection::kFirst).empty());

    Graph triples;
    for (unsigned i = 0; i < 90; ++i) {
        auto const node = Entity("e" + std::to_string(i));
        triples.push_back({node, "loop", node});
    }
    auto const large = compact::BuildGraph(triples);
    std::vector<compact::VertexId> const sparse_first{65, 70}, sparse_second{2, 89};
    compact::PairingRelation const sparse{{0, 2}, {{65, 89}, {70, 2}}};
    EXPECT_EQ(compact::ReduceSubgraph(compact::GraphView(large, sparse_first), sparse,
                                      compact::PairingProjection::kFirst),
              sparse_first);
    EXPECT_EQ(compact::ReduceSubgraph(compact::GraphView(large, sparse_second), sparse,
                                      compact::PairingProjection::kSecond),
              sparse_second);
}
