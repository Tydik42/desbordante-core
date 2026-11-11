#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/algorithms/gk/gk_deduplicator/chase_engine.h"
#include "core/algorithms/gk_compact/gk_deduplicator/gk_deduplicator.h"
#include "core/algorithms/gk_compact/model/graph_builder.h"
#include "core/config/exceptions.h"
#include "core/config/names.h"

namespace {

namespace compact = gk_compact;
using namespace gk::model;
using Clusters = std::vector<std::vector<std::string>>;
constexpr std::array kModes{std::array{false, false, false}, std::array{true, false, false},
                            std::array{true, true, false}, std::array{true, true, true},
                            std::array{false, false, false}};

Node Entity(std::string id, std::string type = "X") {
    return {NodeKind::kEntity, std::move(id), std::move(type)};
}

Node Value(std::string value) {
    return {NodeKind::kValue, std::move(value), {}};
}

GraphKeyDescriptor EntityKey(std::string type = "A", std::string neighbor_type = "Z") {
    Variable const x{VarKind::kEntity, "x"}, y{VarKind::kEntity, "y"};
    return {{{x, "p", y}}, {{x, std::move(type)}, {y, std::move(neighbor_type)}}, x};
}

GraphKeyDescriptor ValueKey(std::string type = "Z") {
    Variable const x{VarKind::kEntity, "x"}, value{VarKind::kValue, "value"};
    return {{{x, "code", value}}, {{x, std::move(type)}}, x};
}

Graph DependentGraph() {
    return {{Entity("a", "A"), "p", Entity("c1", "Z")},
            {Entity("b", "A"), "p", Entity("c2", "Z")},
            {Entity("c1", "Z"), "code", Value("shared")},
            {Entity("c2", "Z"), "code", Value("shared")}};
}

std::vector<compact::GraphKey> BuildKeys(std::vector<GraphKeyDescriptor> const& keys,
                                         compact::Graph const& graph) {
    std::vector<compact::GraphKey> result;
    for (auto const& key : keys)
        if (auto numeric = compact::BuildGraphKey(key, graph))
            result.push_back(std::move(*numeric));
    return result;
}

Clusters Decode(compact::Graph const& graph, compact::ChaseRunResult const& run) {
    Clusters result;
    for (auto const& cluster : run.clusters) {
        auto& labels = result.emplace_back();
        for (auto vertex : cluster) labels.push_back(graph.GetLabel(vertex));
    }
    return result;
}

void ExpectMatchesString(Graph const& graph, std::vector<GraphKeyDescriptor> const& keys) {
    auto const numeric_graph = compact::BuildGraph(graph);
    compact::ChaseEngine const numeric(BuildKeys(keys, numeric_graph));
    gk::ChaseEngine const strings(keys);
    for (auto const [localization, pairing, reduction] : kModes) {
        auto const expected = strings.Run(graph, localization, pairing, reduction);
        auto const actual = numeric.Run(numeric_graph, localization, pairing, reduction);
        EXPECT_EQ(Decode(numeric_graph, actual), expected.clusters);
        EXPECT_EQ(actual.merge_count, expected.merge_count);
        EXPECT_EQ(actual.clusters.size() + actual.merge_count, numeric_graph.GetEntityCount());
    }
}

compact::Graph Select(compact::Graph const& graph, std::vector<compact::VertexId> vertices) {
    compact::GraphData data;
    data.metadata = graph.Data().metadata;
    data.row_to_vertex = std::move(vertices);
    for (auto vertex : data.row_to_vertex) {
        for (auto edge : graph.Outgoing(vertex))
            if (std::ranges::binary_search(data.row_to_vertex, edge.neighbor))
                data.outgoing.edges.push_back(edge);
        data.outgoing.offsets.push_back(data.outgoing.edges.size());
        for (auto edge : graph.Incoming(vertex))
            if (std::ranges::binary_search(data.row_to_vertex, edge.neighbor))
                data.incoming.edges.push_back(edge);
        data.incoming.offsets.push_back(data.incoming.edges.size());
    }
    data.input_triple_count = data.outgoing.edges.size();
    return compact::Graph(std::move(data));
}

void Configure(algos::Algorithm& algorithm, bool localization = false, bool pairing = false,
               bool reduction = false) {
    using namespace config::names;
    algorithm.SetOption(kGkUseLocalization, localization);
    if (localization) algorithm.SetOption(kGkUsePairingFilter, pairing);
    if (pairing) algorithm.SetOption(kGkUseSubgraphReduction, reduction);
}

}  // namespace

TEST(GkCompactDeduplicatorTest, ReachesFixedPointAndResetsEquivalenceOnEveryRun) {
    auto graph = DependentGraph();
    graph.push_back({Entity("lonely", "A"), "note", Value("only")});
    auto const numeric_graph = compact::BuildGraph(graph);
    compact::ChaseEngine const engine(BuildKeys({EntityKey(), ValueKey()}, numeric_graph));
    for (auto const [localization, pairing, reduction] : kModes) {
        auto const result = engine.Run(numeric_graph, localization, pairing, reduction);
        EXPECT_EQ(Decode(numeric_graph, result), (Clusters{{"a", "b"}, {"c1", "c2"}, {"lonely"}}));
        EXPECT_EQ(result.merge_count, 2U);
        EXPECT_EQ(numeric_graph.GetEntityCount(), 5U);
        EXPECT_EQ(numeric_graph.GetEdgeCount(), graph.size());
    }
    // Duplicate facts, keys and input order do not change the fixed point.
    graph.push_back(graph.front());
    std::reverse(graph.begin(), graph.end());
    ExpectMatchesString(graph, {ValueKey(), EntityKey(), ValueKey()});
}

TEST(GkCompactDeduplicatorTest, PreservesOriginalVertexInjectivityAfterMerges) {
    auto key = EntityKey();
    Variable const z{VarKind::kEntity, "z"};
    key.Q.push_back({key.root_x, "q", z});
    key.T_Q.emplace(z, "Z");
    for (bool injective : {false, true}) {
        auto graph = DependentGraph();
        graph.push_back({Entity("a", "A"), "q", Entity(injective ? "c2" : "c1", "Z")});
        graph.push_back({Entity("b", "A"), "q", Entity("c1", "Z")});
        auto const numeric_graph = compact::BuildGraph(graph);
        compact::ChaseEngine const engine(BuildKeys({key, ValueKey()}, numeric_graph));
        auto const expected = injective ? Clusters{{"a", "b"}, {"c1", "c2"}}
                                        : Clusters{{"a"}, {"b"}, {"c1", "c2"}};
        for (auto const [localization, pairing, reduction] : kModes)
            EXPECT_EQ(Decode(numeric_graph,
                             engine.Run(numeric_graph, localization, pairing, reduction)),
                      expected);
    }
}

TEST(GkCompactDeduplicatorTest, HandlesIncomingEdgesCyclesWildcardsValuesAndConstants) {
    Variable const x{VarKind::kEntity, "x"}, w{VarKind::kWildcard, "w"};
    Variable const value{VarKind::kValue, "value"}, constant{VarKind::kConstant, "fixed"};
    GraphKeyDescriptor const key{
            {{w, "p", x}, {x, "back", w}, {x, "code", value}, {x, "status", constant}},
            {{x, "X"}, {w, "Y"}},
            x};
    for (bool matching : {false, true}) {
        Graph const graph{{Entity("y1", "Y"), "p", Entity("a")},
                          {Entity("y2", "Y"), "p", Entity("b")},
                          {Entity("a"), "back", Entity("y1", "Y")},
                          {Entity("b"), "back", Entity("y2", "Y")},
                          {Entity("a"), "code", Value("shared")},
                          {Entity("b"), "code", Value("shared")},
                          {Entity("a"), "status", Value("fixed")},
                          {Entity("b"), "status", Value(matching ? "fixed" : "other")}};
        ExpectMatchesString(graph, {key});
    }
}

TEST(GkCompactDeduplicatorTest, MatchesStringChaseOnSmallRecursiveGraphs) {
    std::array<Triple, 6> const possible{{{Entity("a"), "p", Entity("b")},
                                          {Entity("b"), "p", Entity("c")},
                                          {Entity("c"), "p", Entity("a")},
                                          {Entity("a"), "code", Value("shared")},
                                          {Entity("b"), "code", Value("shared")},
                                          {Entity("c"), "code", Value("other")}}};
    for (unsigned mask = 0; mask < 64; ++mask) {
        SCOPED_TRACE(mask);
        Graph graph;
        for (std::size_t index = 0; index < possible.size(); ++index)
            if (mask & (1U << index)) graph.push_back(possible[index]);
        ExpectMatchesString(graph, {EntityKey("X", "X")});
        ExpectMatchesString(graph, {ValueKey("X")});
        ExpectMatchesString(graph, {EntityKey("X", "X"), ValueKey("X")});
    }
}

TEST(GkCompactDeduplicatorTest, ReturnsOnlyActiveEntitiesAndKeepsIsolatedSingletons) {
    auto graph = DependentGraph();
    graph.push_back({Entity("inactive", "A"), "p", Entity("unused", "Z")});
    auto const full = compact::BuildGraph(graph);
    std::vector<compact::VertexId> vertices;
    for (auto vertex : full.VertexIds())
        if (full.GetLabel(vertex) != "inactive" && full.GetLabel(vertex) != "unused")
            vertices.push_back(vertex);
    auto const active = Select(full, vertices);
    compact::ChaseEngine const engine(BuildKeys({EntityKey(), ValueKey()}, active));
    for (auto const [localization, pairing, reduction] : kModes) {
        auto const result = engine.Run(active, localization, pairing, reduction);
        EXPECT_EQ(Decode(active, result), (Clusters{{"a", "b"}, {"c1", "c2"}}));
        EXPECT_EQ(result.merge_count, 2U);
    }
    auto const isolated = Select(full, {*full.FindEntity("a"), *full.FindEntity("b")});
    for (auto const [localization, pairing, reduction] : kModes) {
        auto const result = engine.Run(isolated, localization, pairing, reduction);
        EXPECT_EQ(Decode(isolated, result), (Clusters{{"a"}, {"b"}}));
        EXPECT_EQ(result.merge_count, 0U);
    }
    compact::ChaseEngine const no_keys(std::vector<compact::GraphKey>{});
    EXPECT_EQ(Decode(isolated, no_keys.Run(isolated)), (Clusters{{"a"}, {"b"}}));
    EXPECT_TRUE(engine.Run(Select(full, {})).clusters.empty());
    EXPECT_TRUE(no_keys.Run(compact::Graph{}).clusters.empty());
}

TEST(GkCompactDeduplicatorTest, RejectsInvalidOptionDependenciesBeforeVacuousRun) {
    compact::ChaseEngine const engine(std::vector<compact::GraphKey>{});
    compact::Graph const graph;
    EXPECT_THROW(engine.Run(graph, false, true, false), config::ConfigurationError);
    EXPECT_THROW(engine.Run(graph, false, false, true), config::ConfigurationError);
    EXPECT_THROW(engine.Run(graph, true, false, true), config::ConfigurationError);
}

TEST(GkCompactDeduplicatorTest, AdapterHandlesMissingAndMalformedKeys) {
    auto const graph =
            std::make_shared<compact::Graph const>(compact::BuildGraph(DependentGraph()));
    auto missing = EntityKey();
    missing.Q.front().predicate = "absent";
    algos::CompactGkDeduplicator algorithm;
    EXPECT_THROW(algorithm.GetResult(), std::logic_error);
    algorithm.SetOption(config::names::kGkGraphData, graph);
    algorithm.SetOption(config::names::kGkKeysData, std::vector{missing});
    algorithm.LoadData();
    Configure(algorithm, true, true, true);
    algorithm.Execute();
    EXPECT_EQ(algorithm.GetClusters(), (Clusters{{"a"}, {"b"}, {"c1"}, {"c2"}}));
    EXPECT_EQ(algorithm.GetMergeCount(), 0U);

    auto invalid = EntityKey();
    invalid.root_x.name = "absent";
    algos::CompactGkDeduplicator malformed;
    malformed.SetOption(config::names::kGkGraphData, graph);
    malformed.SetOption(config::names::kGkKeysData, std::vector{ValueKey(), invalid});
    EXPECT_THROW(malformed.LoadData(), std::invalid_argument);
    EXPECT_THROW(malformed.GetResult(), std::logic_error);

    algos::CompactGkDeduplicator null_graph;
    null_graph.SetOption(config::names::kGkGraphData, std::shared_ptr<compact::Graph const>{});
    null_graph.SetOption(config::names::kGkKeysData, std::vector<GraphKeyDescriptor>{});
    EXPECT_THROW(null_graph.LoadData(), config::ConfigurationError);
}

TEST(GkCompactDeduplicatorTest, AdapterReloadsCsvAndRejectsExecutionAfterFailedLoad) {
    auto const stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto const path = std::filesystem::temp_directory_path() /
                      ("gk_compact_chase_" + std::to_string(stamp) + ".csv");

    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } cleanup{path};

    std::string const csv =
            "subject_id,subject_type,predicate,object_kind,object_id_or_value,object_type\n"
            "a,A,p,entity,c1,Z\nb,A,p,entity,c2,Z\n"
            "c1,Z,code,value,shared,-\nc2,Z,code,value,shared,-\n";
    {
        std::ofstream(path) << csv;
    }
    algos::CompactGkDeduplicator algorithm;
    algorithm.SetOption(config::names::kGkGraphPath, path);
    algorithm.SetOption(config::names::kGkKeysData, std::vector{EntityKey(), ValueKey()});
    algorithm.LoadData();
    for (auto const [localization, pairing, reduction] : kModes) {
        Configure(algorithm, localization, pairing, reduction);
        algorithm.Execute();
        EXPECT_EQ(algorithm.GetClusters(), (Clusters{{"a", "b"}, {"c1", "c2"}}));
        EXPECT_EQ(algorithm.GetMergeCount(), 2U);
        EXPECT_EQ(algorithm.GetClusterCount(), 2U);
    }
    {
        std::ofstream(path) << "invalid CSV\n";
    }
    Configure(algorithm);
    EXPECT_THROW(algorithm.LoadData(), std::runtime_error);
    Configure(algorithm);
    EXPECT_THROW(algorithm.Execute(), std::logic_error);
    EXPECT_THROW(algorithm.GetResult(), std::logic_error);
    {
        std::ofstream(path) << csv;
    }
    Configure(algorithm);
    algorithm.LoadData();
    Configure(algorithm, true, true, true);
    algorithm.Execute();
    EXPECT_EQ(algorithm.GetClusters(), (Clusters{{"a", "b"}, {"c1", "c2"}}));
}
