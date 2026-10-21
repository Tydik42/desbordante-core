#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/algorithms/gk/chase/chase_engine.h"
#include "core/algorithms/gk/gk_deduplicator/gk_deduplicator.h"
#include "core/algorithms/gk/graph_descriptor.h"
#include "core/config/exceptions.h"
#include "core/config/names.h"

namespace {

using namespace gk::model;
using Clusters = gk::EquivalenceRelation::Classes;

Node Entity(std::string id, std::string type = "X") {
    return {NodeKind::kEntity, std::move(id), std::move(type)};
}

Node Value(std::string value) {
    return {NodeKind::kValue, std::move(value), {}};
}

GraphKey EntityKey(std::string root_type = "X", std::string neighbor_type = "X",
                   std::string predicate = "p") {
    Variable const x{VarKind::kEntity, "x"};
    Variable const y{VarKind::kEntity, "y"};
    return {{{x, std::move(predicate), y}},
            {{x, std::move(root_type)}, {y, std::move(neighbor_type)}},
            x};
}

GraphKey ValueKey(std::string root_type = "X", std::string predicate = "code") {
    Variable const x{VarKind::kEntity, "x"};
    Variable const value{VarKind::kValue, "value"};
    return {{{x, std::move(predicate), value}}, {{x, std::move(root_type)}}, x};
}

Graph DependentGraph() {
    return {{Entity("a", "A"), "p", Entity("c1", "Z")},
            {Entity("b", "A"), "p", Entity("c2", "Z")},
            {Entity("c1", "Z"), "code", Value("shared")},
            {Entity("c2", "Z"), "code", Value("shared")}};
}

std::vector<GraphKey> DependentKeys() {
    return {EntityKey("A", "Z"), ValueKey("Z")};
}

// Independent small-graph oracle: enumerate individual injective embeddings, compare their
// images, and close an explicit Boolean relation. It uses neither EvalGK nor the production DSU.
struct Embeddings {
    std::vector<Variable> variables;
    std::vector<std::vector<Node>> images;
    std::size_t root;
};

Embeddings EnumerateEmbeddings(Graph const& graph, GraphKey const& key) {
    Embeddings result;
    std::vector<Node> domain;
    for (auto const& triple : graph) {
        for (auto const& node : {triple.subject, triple.object}) {
            if (std::find(domain.begin(), domain.end(), node) == domain.end())
                domain.push_back(node);
        }
    }
    for (auto const& triple : key.Q) {
        for (auto const& variable : {triple.subject, triple.object}) {
            if (std::find(result.variables.begin(), result.variables.end(), variable) ==
                result.variables.end())
                result.variables.push_back(variable);
        }
    }
    auto index_of = [&result](Variable const& variable) {
        return static_cast<std::size_t>(
                std::find(result.variables.begin(), result.variables.end(), variable) -
                result.variables.begin());
    };
    result.root = index_of(key.root_x);
    std::vector<Node> images;
    std::function<void(std::size_t)> enumerate = [&](std::size_t index) {
        if (index == result.variables.size()) {
            bool const preserves_triples =
                    std::all_of(key.Q.begin(), key.Q.end(), [&](auto const& t) {
                        return std::any_of(graph.begin(), graph.end(), [&](auto const& edge) {
                            return edge.subject == images[index_of(t.subject)] &&
                                   edge.predicate == t.predicate &&
                                   edge.object == images[index_of(t.object)];
                        });
                    });
            if (preserves_triples) result.images.push_back(images);
            return;
        }
        auto const& variable = result.variables[index];
        for (auto const& node : domain) {
            bool const entity_variable =
                    variable.kind == VarKind::kEntity || variable.kind == VarKind::kWildcard;
            if (entity_variable) {
                if (node.kind != NodeKind::kEntity || node.type != key.T_Q.at(variable)) continue;
            } else {
                if (node.kind != NodeKind::kValue) continue;
                if (variable.kind == VarKind::kConstant && node.id_or_val != variable.name)
                    continue;
            }
            if (std::find(images.begin(), images.end(), node) != images.end()) continue;
            images.push_back(node);
            enumerate(index + 1);
            images.pop_back();
        }
    };
    enumerate(0);
    return result;
}

Clusters ReferenceChase(Graph const& graph, std::vector<GraphKey> const& keys) {
    std::vector<std::string> entities;
    for (auto const& triple : graph) {
        for (auto const& node : {triple.subject, triple.object}) {
            if (node.kind == NodeKind::kEntity) entities.push_back(node.id_or_val);
        }
    }
    std::sort(entities.begin(), entities.end());
    entities.erase(std::unique(entities.begin(), entities.end()), entities.end());
    auto index_of = [&entities](std::string const& id) {
        return static_cast<std::size_t>(std::lower_bound(entities.begin(), entities.end(), id) -
                                        entities.begin());
    };
    std::size_t const count = entities.size();
    std::vector<std::vector<std::uint8_t>> eq(count, std::vector<std::uint8_t>(count, 0));
    for (std::size_t i = 0; i < count; ++i) eq[i][i] = 1;
    std::vector<Embeddings> embeddings;
    for (auto const& key : keys) embeddings.push_back(EnumerateEmbeddings(graph, key));
    bool changed;
    do {
        changed = false;
        for (auto const& matches : embeddings) {
            for (auto const& lhs : matches.images) {
                for (auto const& rhs : matches.images) {
                    auto const first = index_of(lhs[matches.root].id_or_val);
                    auto const second = index_of(rhs[matches.root].id_or_val);
                    if (eq[first][second]) continue;
                    bool matching = true;
                    for (std::size_t u = 0; u < matches.variables.size(); ++u) {
                        auto const kind = matches.variables[u].kind;
                        if (kind == VarKind::kValue && lhs[u] != rhs[u]) matching = false;
                        if (kind == VarKind::kEntity && u != matches.root &&
                            !eq[index_of(lhs[u].id_or_val)][index_of(rhs[u].id_or_val)]) {
                            matching = false;
                        }
                    }
                    if (matching) {
                        eq[first][second] = eq[second][first] = 1;
                        changed = true;
                    }
                }
            }
        }
        for (std::size_t k = 0; k < count; ++k) {
            for (std::size_t i = 0; i < count; ++i) {
                for (std::size_t j = 0; j < count; ++j) {
                    if (eq[i][k] && eq[k][j]) eq[i][j] = 1;
                }
            }
        }
    } while (changed);
    Clusters clusters;
    std::vector<std::uint8_t> used(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        if (used[i]) continue;
        auto& group = clusters.emplace_back();
        for (std::size_t j = i; j < count; ++j) {
            if (eq[i][j]) {
                group.push_back(entities[j]);
                used[j] = 1;
            }
        }
    }
    return clusters;
}

}  // namespace

TEST(GkDeduplicatorTest, EmptyInputsAndMissingRootTypesProduceIdentity) {
    EXPECT_TRUE(gk::ChaseEngine({}).Run({}).clusters.empty());
    Graph const graph = {{Entity("a"), "p", Entity("b")}};
    for (auto const& keys : {std::vector<GraphKey>{}, std::vector<GraphKey>{ValueKey("absent")}}) {
        auto const result = gk::ChaseEngine(keys).Run(graph);
        EXPECT_EQ(result.clusters, (Clusters{{"a"}, {"b"}}));
        EXPECT_EQ(result.statistics.merge_count, 0U);
        EXPECT_EQ(result.statistics.pass_count, 0U);
    }
    EXPECT_TRUE(gk::ChaseEngine({ValueKey()}).Run({}).clusters.empty());
}

TEST(GkDeduplicatorTest, MergesTransitiveMatchesAndKeepsSingletons) {
    Graph const graph = {{Entity("a"), "code", Value("first")},
                         {Entity("b"), "code", Value("first")},
                         {Entity("b"), "code", Value("second")},
                         {Entity("c"), "code", Value("second")},
                         {Entity("foreign", "Y"), "code", Value("first")}};
    auto const result = gk::ChaseEngine({ValueKey()}).Run(graph);
    EXPECT_EQ(result.clusters, (Clusters{{"a", "b", "c"}, {"foreign"}}));
    EXPECT_EQ(result.statistics.merge_count, 2U);
    EXPECT_EQ(result.statistics.entity_count, 4U);
    EXPECT_EQ(result.statistics.merge_count,
              result.statistics.entity_count - result.clusters.size());
}

TEST(GkDeduplicatorTest, RevisitsPairsAfterAnotherKeyChangesEquivalence) {
    auto const result = gk::ChaseEngine(DependentKeys()).Run(DependentGraph());
    EXPECT_EQ(result.clusters, (Clusters{{"a", "b"}, {"c1", "c2"}}));
    EXPECT_EQ(result.statistics.merge_count, 2U);
    EXPECT_GE(result.statistics.pass_count, 3U);
    EXPECT_EQ(result.statistics.root_pair_count, 2U);
}

TEST(GkDeduplicatorTest, RecursiveKeyNeedsASeedAndRepeatedPasses) {
    Graph const seeded = {{Entity("a"), "p", Entity("c")},
                          {Entity("b"), "p", Entity("d")},
                          {Entity("c"), "p", Entity("z")},
                          {Entity("d"), "p", Entity("z")},
                          {Entity("z"), "p", Entity("z")}};
    gk::ChaseEngine const engine({EntityKey()});
    auto const result = engine.Run(seeded);
    EXPECT_EQ(result.clusters, (Clusters{{"a", "b"}, {"c", "d"}, {"z"}}));
    EXPECT_GE(result.statistics.pass_count, 3U);
    Graph const unseeded = {{Entity("a"), "p", Entity("b")}, {Entity("b"), "p", Entity("a")}};
    EXPECT_EQ(engine.Run(unseeded).clusters, (Clusters{{"a"}, {"b"}}));
}

TEST(GkDeduplicatorTest, InjectivityUsesOriginalVerticesAfterMerges) {
    auto key = EntityKey("A", "Z");
    Variable const z{VarKind::kEntity, "z"};
    key.Q.push_back({key.root_x, "q", z});
    key.T_Q.emplace(z, "Z");
    gk::ChaseEngine const engine({key, ValueKey("Z")});
    for (bool const injective : {true, false}) {
        Graph graph = DependentGraph();
        graph.push_back({Entity("a", "A"), "q", Entity(injective ? "c2" : "c1", "Z")});
        graph.push_back({Entity("b", "A"), "q", Entity("c1", "Z")});
        auto const expected = injective ? Clusters{{"a", "b"}, {"c1", "c2"}}
                                        : Clusters{{"a"}, {"b"}, {"c1", "c2"}};
        EXPECT_EQ(engine.Run(graph).clusters, expected);
    }
}

TEST(GkDeduplicatorTest, HandlesWildcardsValuesConstantsIncomingEdgesAndCycles) {
    Variable const x{VarKind::kEntity, "x"};
    Variable const w{VarKind::kWildcard, "w"};
    Variable const value{VarKind::kValue, "value"};
    Variable const constant{VarKind::kConstant, "fixed"};
    GraphKey const key{{{w, "p", x}, {x, "back", w}, {x, "code", value}, {x, "status", constant}},
                       {{x, "X"}, {w, "Y"}},
                       x};
    for (bool const matching : {true, false}) {
        Graph const graph = {{Entity("y1", "Y"), "p", Entity("a")},
                             {Entity("y2", "Y"), "p", Entity("b")},
                             {Entity("a"), "back", Entity("y1", "Y")},
                             {Entity("b"), "back", Entity("y2", "Y")},
                             {Entity("a"), "code", Value("shared")},
                             {Entity("b"), "code", Value("shared")},
                             {Entity("a"), "status", Value("fixed")},
                             {Entity("b"), "status", Value(matching ? "fixed" : "other")}};
        auto const expected = matching ? Clusters{{"a", "b"}, {"y1"}, {"y2"}}
                                       : Clusters{{"a"}, {"b"}, {"y1"}, {"y2"}};
        EXPECT_EQ(gk::ChaseEngine({key}).Run(graph).clusters, expected);
        EXPECT_EQ(ReferenceChase(graph, {key}), expected);
    }
}

TEST(GkDeduplicatorTest, OwnsKeysAndResetsEquivalenceBetweenExecutions) {
    gk::ChaseEngine const engine(DependentKeys());
    Graph graph = DependentGraph();
    auto const first = engine.Run(graph);
    graph.back().object = Value("different");
    EXPECT_EQ(engine.Run(graph).clusters, (Clusters{{"a"}, {"b"}, {"c1"}, {"c2"}}));
    auto const repeated = engine.Run(DependentGraph());
    EXPECT_EQ(repeated.clusters, first.clusters);
    EXPECT_EQ(repeated.statistics.merge_count, first.statistics.merge_count);
    EXPECT_EQ(repeated.statistics.eval_call_count, first.statistics.eval_call_count);
}

TEST(GkDeduplicatorTest, OrderAndDuplicateFactsOrKeysDoNotChangePartition) {
    auto graph = DependentGraph();
    auto keys = DependentKeys();
    auto const expected = gk::ChaseEngine(keys).Run(graph).clusters;
    graph.push_back(graph.front());
    keys.push_back(keys.front());
    std::reverse(graph.begin(), graph.end());
    std::reverse(keys.begin(), keys.end());
    EXPECT_EQ(gk::ChaseEngine(keys).Run(graph).clusters, expected);
}

TEST(GkDeduplicatorTest, MatchesIndependentEmbeddingEnumerationOnSmallGraphs) {
    auto mixed = EntityKey();
    Variable const value{VarKind::kValue, "v"};
    mixed.Q.push_back({mixed.Q.front().object, "code", value});
    std::vector<std::vector<GraphKey>> const key_sets = {
            {EntityKey()}, {ValueKey()}, {EntityKey(), ValueKey()}, {mixed}};
    for (unsigned int mask = 0; mask < 64; ++mask) {
        SCOPED_TRACE(mask);
        Graph graph;
        for (unsigned int i = 0; i < 3; ++i) {
            Node const source = Entity(std::string(1, static_cast<char>('a' + i)));
            graph.push_back({source, "code", Value((mask & (1U << i)) ? "v1" : "v0")});
            for (unsigned int j = 0; j < 2; ++j) {
                if (mask & (1U << (2 * i + j))) {
                    graph.push_back(
                            {source, "p", Entity(std::string(1, static_cast<char>('a' + j)))});
                }
            }
        }
        for (auto const& keys : key_sets) {
            auto const result = gk::ChaseEngine(keys).Run(graph);
            EXPECT_EQ(result.clusters, ReferenceChase(graph, keys));
            EXPECT_EQ(result.statistics.merge_count, 3U - result.clusters.size());
        }
    }
}

TEST(GkDeduplicatorTest, AdapterRegistersOptionsAndResetsExecutionStatistics) {
    using namespace config::names;
    algos::GkDeduplicator algorithm;
    EXPECT_THROW(algorithm.Execute(), std::logic_error);
    EXPECT_EQ(algorithm.GetTypeIndex(kGkGraphData), typeid(Graph));
    EXPECT_EQ(algorithm.GetTypeIndex(kGkGraphPath), typeid(std::filesystem::path));
    EXPECT_EQ(algorithm.GetTypeIndex(kGkKeysData), typeid(std::vector<GraphKey>));
    algorithm.SetOption(kGkGraphData, DependentGraph());
    algorithm.SetOption(kGkKeysData, DependentKeys());
    algorithm.LoadData();
    for (int iteration = 0; iteration < 2; ++iteration) {
        algorithm.SetOption(kGkUsePairingFilter);
        algorithm.SetOption(kGkUseSubgraphReduction);
        algorithm.Execute();
        EXPECT_EQ(algorithm.GetClusters(), (Clusters{{"a", "b"}, {"c1", "c2"}}));
        EXPECT_EQ(algorithm.GetMergeCount(), 2U);
        EXPECT_EQ(algorithm.GetClusterCount(), 2U);
        auto const& stats = algorithm.GetTimingStats();
        EXPECT_EQ(stats.graph_triple_count, 4U);
        EXPECT_EQ(stats.key_count, 2U);
        EXPECT_EQ(stats.execution.entity_count, 4U);
        EXPECT_EQ(stats.execution.merge_count, 2U);
        EXPECT_EQ(stats.execution.root_pair_count, 2U);
        EXPECT_GE(stats.execution.pass_count, 3U);
        EXPECT_EQ(stats.load_source_ns, 0U);
        EXPECT_GE(stats.load_total_ns, stats.load_graph_check_ns + stats.load_keys_check_ns);
        EXPECT_GE(stats.execute_total_ns, stats.execution.total_ns);
        EXPECT_GE(stats.execution.total_ns, stats.execution.prepare_ns + stats.execution.eval_ns +
                                                    stats.execution.finalize_ns);
    }
}

TEST(GkDeduplicatorTest, AdapterLoadsTheExistingCsvFormat) {
    using namespace config::names;
    auto const path = std::filesystem::temp_directory_path() / "desbordante_gk_deduplicator.csv";

    struct RemoveFile {
        std::filesystem::path path;

        ~RemoveFile() {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } const cleanup{path};

    {
        std::ofstream stream(path);
        ASSERT_TRUE(stream);
        stream << "subject_id,subject_type,predicate,object_kind,object_id_or_value,object_type\n"
                  "\"a,quoted\",A,p,entity,c1,Z\n"
                  "b,A,p,entity,c2,Z\n"
                  "c1,Z,code,value,\"shared\nvalue\",-\n"
                  "c2,Z,code,value,\"shared\nvalue\",-\n";
        ASSERT_TRUE(stream);
    }
    algos::GkDeduplicator algorithm;
    algorithm.SetOption(kGkGraphPath, path);
    algorithm.SetOption(kGkKeysData, DependentKeys());
    algorithm.LoadData();
    algorithm.SetOption(kGkUsePairingFilter);
    algorithm.SetOption(kGkUseSubgraphReduction);
    algorithm.Execute();
    EXPECT_EQ(algorithm.GetClusters(), (Clusters{{"a,quoted", "b"}, {"c1", "c2"}}));
    EXPECT_GT(algorithm.GetTimingStats().load_source_ns, 0U);
}

TEST(GkDeduplicatorTest, AdapterRejectsMissingAmbiguousAndInvalidInputs) {
    using namespace config::names;
    {
        algos::GkDeduplicator algorithm;
        EXPECT_THROW(algorithm.LoadData(), std::logic_error);
        algorithm.SetOption(kGkGraphData, DependentGraph());
        EXPECT_THROW(algorithm.LoadData(), std::logic_error);
        algorithm.SetOption(kGkKeysData, DependentKeys());
        algorithm.SetOption(kGkGraphPath, std::filesystem::path("unused.csv"));
        EXPECT_THROW(algorithm.LoadData(), config::ConfigurationError);
    }
    {
        algos::GkDeduplicator algorithm;
        auto graph = DependentGraph();
        graph.front().subject.type.clear();
        algorithm.SetOption(kGkGraphData, graph);
        algorithm.SetOption(kGkKeysData, DependentKeys());
        EXPECT_THROW(algorithm.LoadData(), std::invalid_argument);
    }
    {
        algos::GkDeduplicator algorithm;
        auto key = EntityKey("absent");
        key.T_Q.clear();
        algorithm.SetOption(kGkGraphData, Graph{});
        algorithm.SetOption(kGkKeysData, std::vector<GraphKey>{key});
        EXPECT_THROW(algorithm.LoadData(), std::invalid_argument);
    }
}

TEST(GkDeduplicatorTest, AdapterHandlesIdentityAndRejectsUnsupportedOptimizations) {
    using namespace config::names;
    for (Graph const& graph : {Graph{}, DependentGraph()}) {
        algos::GkDeduplicator algorithm;
        algorithm.SetOption(kGkGraphData, graph);
        algorithm.SetOption(kGkKeysData, std::vector<GraphKey>{});
        algorithm.LoadData();
        algorithm.SetOption(kGkUsePairingFilter);
        algorithm.SetOption(kGkUseSubgraphReduction);
        algorithm.Execute();
        EXPECT_EQ(algorithm.GetMergeCount(), 0U);
        EXPECT_EQ(algorithm.GetClusters(), ReferenceChase(graph, {}));
        EXPECT_EQ(algorithm.GetTimingStats().execution.pass_count, 0U);
    }
    algos::GkDeduplicator algorithm;
    algorithm.SetOption(kGkGraphData, DependentGraph());
    algorithm.SetOption(kGkKeysData, DependentKeys());
    algorithm.LoadData();
    for (auto const option : {kGkUsePairingFilter, kGkUseSubgraphReduction}) {
        algorithm.SetOption(kGkUsePairingFilter, false);
        algorithm.SetOption(kGkUseSubgraphReduction, false);
        algorithm.SetOption(option, true);
        EXPECT_THROW(algorithm.Execute(), std::logic_error);
        EXPECT_TRUE(algorithm.GetClusters().empty());
        EXPECT_EQ(algorithm.GetMergeCount(), 0U);
        EXPECT_EQ(algorithm.GetTimingStats().execute_total_ns, 0U);
    }
    algorithm.SetOption(kGkUsePairingFilter, false);
    algorithm.SetOption(kGkUseSubgraphReduction, false);
    algorithm.Execute();
    EXPECT_EQ(algorithm.GetMergeCount(), 2U);
}
