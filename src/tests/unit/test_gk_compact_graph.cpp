#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/algorithms/gk/parser/graph_parser.h"
#include "core/algorithms/gk/model/validation.h"
#include "core/algorithms/gk_compact/graph/graph_builder.h"
#include "core/algorithms/gk_compact/parser/graph_parser.h"

namespace {

using namespace gk::model;
using CompactGraph = gk_compact::Graph;
using CompactGraphBuilder = gk_compact::GraphBuilder;
using Id = gk_compact::VertexId;
using Edge = gk_compact::Edge;
using Fact = std::tuple<std::string, std::string, std::string, NodeKind, std::string, std::string>;

Node Entity(std::string id, std::string type = "Person") {
    return {NodeKind::kEntity, std::move(id), std::move(type)};
}

Node Value(std::string value) {
    return {NodeKind::kValue, std::move(value), {}};
}

Graph ExampleGraph() {
    return {{Entity("a"), "worksAt", Entity("c", "Company")},
            {Entity("b"), "worksAt", Entity("c", "Company")},
            {Entity("a"), "name", Value("Alice")}};
}

std::set<Fact> Facts(Graph const& graph) {
    std::set<Fact> result;
    for (auto const& triple : graph) {
        result.emplace(triple.subject.id_or_val, triple.subject.type, triple.predicate,
                       triple.object.kind, triple.object.id_or_val, triple.object.type);
    }
    return result;
}

Graph Expand(CompactGraph const& graph) {
    Graph result;
    for (std::size_t index = 0; index < graph.GetVertexCount(); ++index) {
        auto const vertex = static_cast<Id>(index);
        for (auto const& edge : gk_compact::Adjacent(graph.Data().outgoing, vertex)) {
            result.push_back({gk_compact::GetNode(graph.Data(), vertex),
                              graph.Data().predicates.at(edge.predicate),
                              gk_compact::GetNode(graph.Data(), edge.neighbor)});
        }
    }
    return result;
}

std::vector<Edge> Copy(std::span<Edge const> edges) {
    return {edges.begin(), edges.end()};
}

void CheckRepresentation(CompactGraph const& graph, Graph const& source) {
    ASSERT_EQ(Facts(Expand(graph)), Facts(source));
    EXPECT_EQ(graph.GetInputTripleCount(), source.size());
    EXPECT_EQ(graph.GetEdgeCount(), Facts(source).size());
    for (auto offsets : {std::span<gk_compact::Offset const>(graph.Data().outgoing.offsets),
                         std::span<gk_compact::Offset const>(graph.Data().incoming.offsets)}) {
        ASSERT_EQ(offsets.size(), graph.GetVertexCount() + 1);
        EXPECT_EQ(offsets.front(), 0U);
        EXPECT_EQ(offsets.back(), graph.GetEdgeCount());
        EXPECT_TRUE(std::is_sorted(offsets.begin(), offsets.end()));
    }
    std::size_t incoming_count = 0;
    std::size_t entity_count = 0;
    for (std::size_t index = 0; index < graph.GetVertexCount(); ++index) {
        auto const vertex = static_cast<Id>(index);
        auto const outgoing = gk_compact::Adjacent(graph.Data().outgoing, vertex);
        auto const incoming = gk_compact::Adjacent(graph.Data().incoming, vertex);
        incoming_count += incoming.size();
        auto less = [](Edge const& lhs, Edge const& rhs) {
            return std::tie(lhs.predicate, lhs.neighbor) < std::tie(rhs.predicate, rhs.neighbor);
        };
        EXPECT_TRUE(std::is_sorted(outgoing.begin(), outgoing.end(), less));
        EXPECT_TRUE(std::is_sorted(incoming.begin(), incoming.end(), less));
        EXPECT_EQ(std::adjacent_find(outgoing.begin(), outgoing.end()), outgoing.end());
        EXPECT_EQ(std::adjacent_find(incoming.begin(), incoming.end()), incoming.end());
        for (auto const& edge : incoming) {
            ASSERT_LT(edge.neighbor, graph.GetVertexCount());
            ASSERT_LT(edge.predicate, graph.Data().predicates.size());
            EXPECT_TRUE(gk_compact::Contains(graph.Data().outgoing, edge.neighbor, edge.predicate,
                                             vertex));
        }
        for (std::size_t p = 0; p < graph.Data().predicates.size(); ++p) {
            auto const predicate = static_cast<gk_compact::PredicateId>(p);
            for (bool const reverse : {false, true}) {
                auto const all = reverse ? incoming : outgoing;
                auto const filtered =
                        reverse ? gk_compact::Adjacent(graph.Data().incoming, vertex, predicate)
                                : gk_compact::Adjacent(graph.Data().outgoing, vertex, predicate);
                std::vector<Edge> expected;
                for (auto edge : all) {
                    if (edge.predicate == predicate) expected.push_back(edge);
                }
                EXPECT_EQ(Copy(filtered), expected);
            }
        }
        EXPECT_TRUE(
                gk_compact::Adjacent(graph.Data().outgoing, vertex, gk_compact::kNoType).empty());
        EXPECT_TRUE(
                gk_compact::Adjacent(graph.Data().incoming, vertex, gk_compact::kNoType).empty());
    }
    for (auto entity : gk_compact::EntityIds(graph.Data().vertices)) {
        EXPECT_EQ(gk_compact::GetNode(graph.Data(), entity).kind, NodeKind::kEntity);
        EXPECT_LT(graph.Data().vertices.types.at(entity), graph.Data().types.size());
        ++entity_count;
    }
    EXPECT_EQ(entity_count, graph.GetEntityCount());
    EXPECT_EQ(incoming_count, graph.GetEdgeCount());
}

}  // namespace

TEST(GkCompactGraphTest, EmptyGraphHasValidOffsetsAndNoDictionaryEntries) {
    for (auto const& graph : {CompactGraph{}, gk_compact::BuildGraph({})}) {
        CheckRepresentation(graph, {});
        EXPECT_EQ(graph.GetVertexCount(), 0U);
        EXPECT_EQ(graph.GetEntityCount(), 0U);
        EXPECT_EQ(graph.Data().types.size(), 0U);
        EXPECT_EQ(graph.Data().predicates.size(), 0U);
        EXPECT_FALSE(gk_compact::FindEntity(graph.Data().vertices, "missing"));
        EXPECT_FALSE(gk_compact::FindValue(graph.Data().vertices, "missing"));
        EXPECT_FALSE(gk_compact::FindLabel(graph.Data().types, "missing"));
        EXPECT_FALSE(gk_compact::FindLabel(graph.Data().predicates, "missing"));
        EXPECT_THROW(gk_compact::GetNode(graph.Data(), 0), std::out_of_range);
        EXPECT_THROW(static_cast<void>(graph.Data().types.at(0)), std::out_of_range);
        EXPECT_THROW(gk_compact::Adjacent(graph.Data().outgoing, 0), std::out_of_range);
        EXPECT_THROW(gk_compact::Adjacent(graph.Data().incoming, 0), std::out_of_range);
    }
}

TEST(GkCompactGraphTest, KeepsEntityValueAndLabelNamespacesSeparate) {
    auto source = ExampleGraph();
    source.push_back({Entity("Alice"), "name", Value("Alice")});
    source.push_back({Entity("Alice"), "Person", Value("Company")});
    auto const graph = gk_compact::BuildGraph(source);
    CheckRepresentation(graph, source);
    EXPECT_EQ(graph.GetVertexCount(), 6U);
    EXPECT_EQ(graph.GetEntityCount(), 4U);
    auto const entity = gk_compact::FindEntity(graph.Data().vertices, "Alice");
    auto const value = gk_compact::FindValue(graph.Data().vertices, "Alice");
    ASSERT_TRUE(entity);
    ASSERT_TRUE(value);
    EXPECT_NE(*entity, *value);
    EXPECT_EQ(gk_compact::GetNode(graph.Data(), *entity), Entity("Alice"));
    EXPECT_EQ(gk_compact::GetNode(graph.Data(), *value), Value("Alice"));
    EXPECT_EQ(graph.Data().vertices.types.at(*value), gk_compact::kNoType);
    EXPECT_EQ(std::ranges::count(gk_compact::EntityIds(graph.Data().vertices),
                                 *gk_compact::FindLabel(graph.Data().types, "Person"),
                                 [&](Id id) { return graph.Data().vertices.types.at(id); }),
              3);
    EXPECT_EQ(std::ranges::count(gk_compact::EntityIds(graph.Data().vertices),
                                 *gk_compact::FindLabel(graph.Data().types, "Company"),
                                 [&](Id id) { return graph.Data().vertices.types.at(id); }),
              1);
    EXPECT_TRUE(gk_compact::FindLabel(graph.Data().predicates, "Person"));
    EXPECT_FALSE(gk_compact::FindLabel(graph.Data().types, "Alice"));
    EXPECT_FALSE(gk_compact::FindLabel(graph.Data().predicates, "missing"));
}

TEST(GkCompactGraphTest, BuildsBothDirectionsAndPredicateRanges) {
    auto const graph = gk_compact::BuildGraph(ExampleGraph());
    CheckRepresentation(graph, ExampleGraph());
    auto const a = *gk_compact::FindEntity(graph.Data().vertices, "a");
    auto const b = *gk_compact::FindEntity(graph.Data().vertices, "b");
    auto const company = *gk_compact::FindEntity(graph.Data().vertices, "c");
    auto const value = *gk_compact::FindValue(graph.Data().vertices, "Alice");
    auto const works = *gk_compact::FindLabel(graph.Data().predicates, "worksAt");
    auto const name = *gk_compact::FindLabel(graph.Data().predicates, "name");
    std::vector<Edge> expected{{works, company}, {name, value}};
    std::ranges::sort(expected);
    EXPECT_EQ(Copy(gk_compact::Adjacent(graph.Data().outgoing, a)), expected);
    EXPECT_EQ(Copy(gk_compact::Adjacent(graph.Data().incoming, company)),
              (std::vector<Edge>{{works, a}, {works, b}}));
    EXPECT_EQ(Copy(gk_compact::Adjacent(graph.Data().outgoing, a, name)),
              (std::vector<Edge>{{name, value}}));
    EXPECT_EQ(Copy(gk_compact::Adjacent(graph.Data().incoming, value, name)),
              (std::vector<Edge>{{name, a}}));
    EXPECT_TRUE(gk_compact::Adjacent(graph.Data().outgoing, company).empty());
    EXPECT_TRUE(gk_compact::Adjacent(graph.Data().outgoing, value).empty());
    EXPECT_TRUE(gk_compact::Adjacent(graph.Data().incoming, a).empty());
    EXPECT_TRUE(gk_compact::Contains(graph.Data().outgoing, a, works, company));
    EXPECT_FALSE(gk_compact::Contains(graph.Data().outgoing, company, works, a));
    EXPECT_FALSE(gk_compact::Contains(graph.Data().outgoing, a, name, company));
}

TEST(GkCompactGraphTest, DeduplicatesFactsButKeepsLoopsAndDifferentPredicates) {
    Graph const source = {{Entity("a"), "p", Entity("b")},
                          {Entity("a"), "q", Entity("b")},
                          {Entity("a"), "p", Entity("a")},
                          {Entity("a"), "p", Entity("b")},
                          {Entity("b"), "q", Entity("a")}};
    auto const graph = gk_compact::BuildGraph(source);
    CheckRepresentation(graph, source);
    EXPECT_EQ(graph.GetInputTripleCount(), 5U);
    EXPECT_EQ(graph.GetEdgeCount(), 4U);
    auto const a = *gk_compact::FindEntity(graph.Data().vertices, "a");
    EXPECT_TRUE(gk_compact::Contains(graph.Data().outgoing, a,
                                     *gk_compact::FindLabel(graph.Data().predicates, "p"), a));
}

TEST(GkCompactGraphTest, ReadsTheSameCsvIncludingQuotesUnicodeAndMultilineValues) {
    for (std::string const ending : {"\n", "\r\n"}) {
        std::string const csv =
                "subject_id,subject_type,predicate,object_kind,object_id_or_value,object_type" +
                ending + "\"a,one\",Person,worksAt,entity,c,Company" + ending +
                "\"a,one\",Person,name,value,\"Алиса, \"\"A\"\"" + ending + "second line\",-" +
                ending + "c,Company,name,value,\" a\t\\n \",-" + ending +
                "\"a,one\",Person,worksAt,entity,c,Company";
        std::istringstream string_stream(csv);
        std::istringstream compact_stream(csv);
        auto const source = gk::parser::ReadGraph(string_stream);
        auto const graph = gk_compact::parser::ReadGraph(compact_stream);
        CheckRepresentation(graph, source);
        auto const converted = gk_compact::BuildGraph(source);
        EXPECT_EQ(Facts(Expand(converted)), Facts(Expand(graph)));
        EXPECT_TRUE(gk_compact::FindValue(graph.Data().vertices,
                                          "Алиса, \"A\"" + ending + "second line"));
        EXPECT_TRUE(gk_compact::FindValue(graph.Data().vertices, " a\t\\n "));
    }
    std::istringstream header(
            "subject_id,subject_type,predicate,object_kind,object_id_or_value,"
            "object_type\n");
    CheckRepresentation(gk_compact::parser::ReadGraph(header), {});
}

TEST(GkCompactGraphTest, LoadsCsvFromPathAndReportsMissingFiles) {
    auto const directory =
            std::filesystem::path(__FILE__).parent_path() / "../../../examples/datasets/gk";
    auto const graph = gk_compact::parser::ReadGraph(directory / "simple_graph.csv");
    CheckRepresentation(graph, gk::parser::ReadGraph(directory / "simple_graph.csv"));
    EXPECT_THROW(gk_compact::parser::ReadGraph(directory / "does-not-exist.csv"),
                 std::runtime_error);
}

TEST(GkCompactGraphTest, CsvRejectsMalformedAndInconsistentDataLikeTheStringLoader) {
    std::string const header =
            "subject_id,subject_type,predicate,object_kind,object_id_or_value,object_type\n";
    for (std::string const& body :
         {"a,X,p,entity,b\n", "a,X,p,value,value,Y\n", "a,X,p,entity,b,-\n", "a,X,p,other,b,Y\n",
          "a,X,p,value,\"unfinished\n", "a,X,p,value,,\n", "a,X,p,entity,b,Y\nb,Z,q,value,v,-\n",
          "a,X,p,entity,a,Y\n"}) {
        SCOPED_TRACE(body);
        std::istringstream string_stream(header + body);
        std::istringstream compact_stream(header + body);
        EXPECT_THROW(gk::parser::ReadGraph(string_stream), std::runtime_error);
        EXPECT_THROW(gk_compact::parser::ReadGraph(compact_stream), std::runtime_error);
    }
    std::istringstream empty;
    EXPECT_THROW(gk_compact::parser::ReadGraph(empty), std::runtime_error);
}

TEST(GkCompactGraphTest, BuilderRejectsInvalidDescriptorsWithoutAddingRejectedFacts) {
    Triple const valid{Entity("a"), "p", Entity("b")};
    std::vector<Triple> const invalid = {{Value("a"), "p", Entity("b")},
                                         {Entity(""), "p", Entity("b")},
                                         {Entity("a", ""), "p", Entity("b")},
                                         {Entity("a"), "", Entity("b")},
                                         {Entity("a"), "p", Value("")},
                                         {Entity("a"), "p", {NodeKind::kValue, "v", "wrong"}},
                                         {Entity("a"), "p", {static_cast<NodeKind>(-1), "v", {}}},
                                         {Entity("a", "Other"), "p", Entity("b")},
                                         {Entity("new"), "p", Entity("a", "Other")},
                                         {Entity("new", "X"), "p", Entity("new", "Y")}};
    CompactGraphBuilder builder;
    builder.AddTriple(valid);
    for (auto const& triple : invalid) {
        EXPECT_THROW(builder.AddTriple(triple), std::invalid_argument);
        EXPECT_THROW(gk::CheckGraph(Graph{valid, triple}), std::invalid_argument);
    }
    auto const graph = std::move(builder).Build();
    CheckRepresentation(graph, {valid});
    EXPECT_EQ(graph.GetVertexCount(), 2U);
    EXPECT_FALSE(gk_compact::FindEntity(graph.Data().vertices, "new"));
}

TEST(GkCompactGraphTest, RoundTripsAllThreeEntityDirectedGraphs) {
    for (unsigned int mask = 0; mask < 512; ++mask) {
        SCOPED_TRACE(mask);
        Graph source;
        for (unsigned int i = 0; i < 3; ++i) {
            auto const from = Entity(std::string(1, static_cast<char>('a' + i)));
            source.push_back({from, "code", Value(i == 0 ? "shared" : "other")});
            for (unsigned int j = 0; j < 3; ++j) {
                if (mask & (1U << (3 * i + j))) {
                    source.push_back(
                            {from, "p", Entity(std::string(1, static_cast<char>('a' + j)))});
                }
            }
        }
        if (mask % 2 == 0) source.push_back(source.front());
        auto const graph = gk_compact::BuildGraph(source);
        CheckRepresentation(graph, source);
        std::reverse(source.begin(), source.end());
        auto const reversed = gk_compact::BuildGraph(source);
        CheckRepresentation(reversed, source);
        EXPECT_EQ(Facts(Expand(graph)), Facts(Expand(reversed)));
    }
}

TEST(GkCompactGraphTest, NumberingAndCsrDoNotDependOnInputOrder) {
    Graph source = ExampleGraph();
    source.push_back({Entity("z", "AType"), "another", Value("z")});
    source.push_back({Entity("Alice", "ZType"), "name", Value("a")});
    source.push_back({Entity("z", "AType"), "Person", Entity("Alice", "ZType")});
    auto const reference = gk_compact::BuildGraph(source);
    for (std::size_t permutation = 0; permutation < 2 * source.size(); ++permutation) {
        SCOPED_TRACE(permutation);
        std::rotate(source.begin(), source.begin() + 1, source.end());
        if (permutation == source.size()) std::reverse(source.begin(), source.end());
        auto const graph = gk_compact::BuildGraph(source);
        CheckRepresentation(graph, source);
        ASSERT_EQ(graph.GetVertexCount(), reference.GetVertexCount());
        ASSERT_EQ(graph.Data().types.size(), reference.Data().types.size());
        ASSERT_EQ(graph.Data().predicates.size(), reference.Data().predicates.size());
        for (std::size_t i = 0; i < graph.GetVertexCount(); ++i) {
            auto const vertex = static_cast<Id>(i);
            EXPECT_EQ(gk_compact::GetNode(graph.Data(), vertex),
                      gk_compact::GetNode(reference.Data(), vertex));
            EXPECT_EQ(Copy(gk_compact::Adjacent(graph.Data().outgoing, vertex)),
                      Copy(gk_compact::Adjacent(reference.Data().outgoing, vertex)));
            EXPECT_EQ(Copy(gk_compact::Adjacent(graph.Data().incoming, vertex)),
                      Copy(gk_compact::Adjacent(reference.Data().incoming, vertex)));
        }
        for (std::size_t i = 0; i < graph.Data().types.size(); ++i) {
            auto const id = static_cast<gk_compact::TypeId>(i);
            EXPECT_EQ(graph.Data().types.at(id), reference.Data().types.at(id));
        }
        for (std::size_t i = 0; i < graph.Data().predicates.size(); ++i) {
            auto const id = static_cast<gk_compact::PredicateId>(i);
            EXPECT_EQ(graph.Data().predicates.at(id), reference.Data().predicates.at(id));
        }
    }
}

TEST(GkCompactGraphTest, ConsumedBuilderDoesNotKeepStateOrOwnTheFinishedGraph) {
    CompactGraphBuilder builder;
    auto const source = ExampleGraph();
    for (auto const& triple : source) builder.AddTriple(triple);
    auto const first = std::move(builder).Build();
    Graph const other = {{Entity("a", "Other"), "different", Value("new")}};
    builder.AddTriple(other.front());
    auto const second = std::move(builder).Build();
    CheckRepresentation(first, source);
    CheckRepresentation(second, other);
    EXPECT_FALSE(gk_compact::FindLabel(second.Data().predicates, "worksAt"));
    EXPECT_FALSE(gk_compact::FindEntity(second.Data().vertices, "b"));
    EXPECT_EQ(first.GetEntityCount(), 3U);
    EXPECT_EQ(second.GetEntityCount(), 1U);
}

TEST(GkCompactGraphTest, GraphOwnsValidatedDataIndependentlyOfItsSource) {
    auto const source = ExampleGraph();
    auto const built = gk_compact::BuildGraph(source);
    auto data = built.Data();
    CompactGraph const graph(data);
    data.vertices.labels.clear();
    data.outgoing.offsets.clear();
    CheckRepresentation(graph, source);
    CheckRepresentation(CompactGraph(built.Data()), source);
    CheckRepresentation(CompactGraph(gk_compact::GraphData{}), {});
}

TEST(GkCompactGraphTest, ConstructorRejectsInvalidVertexDataAndDictionaries) {
    auto const graph = gk_compact::BuildGraph(ExampleGraph());
    auto reject = [&](auto mutate) {
        auto data = graph.Data();
        mutate(data);
        EXPECT_THROW((CompactGraph(std::move(data))), std::invalid_argument);
    };
    reject([](auto& d) { d.vertices.types.pop_back(); });
    reject([](auto& d) { d.vertices.entity_count = d.vertices.labels.size() + 1; });
    reject([](auto& d) { d.vertices.labels[0].clear(); });
    reject([](auto& d) { d.vertices.labels[1] = d.vertices.labels[0]; });
    reject([](auto& d) { std::swap(d.vertices.labels[0], d.vertices.labels[1]); });
    reject([](auto& d) { d.vertices.types[0] = gk_compact::kNoType; });
    reject([](auto& d) { d.vertices.types[0] = static_cast<Id>(d.types.size()); });
    reject([](auto& d) { d.vertices.types.back() = 0; });
    reject([](auto& d) { d.types[0].clear(); });
    reject([](auto& d) { d.types[1] = d.types[0]; });
    reject([](auto& d) { std::reverse(d.types.begin(), d.types.end()); });
    reject([](auto& d) { d.predicates[0].clear(); });
    reject([](auto& d) { d.predicates[1] = d.predicates[0]; });
    reject([](auto& d) { std::reverse(d.predicates.begin(), d.predicates.end()); });
}

TEST(GkCompactGraphTest, ConstructorRejectsInvalidCsrBeforeUsingOffsetsOrIds) {
    auto const graph = gk_compact::BuildGraph(ExampleGraph());
    for (bool const incoming : {false, true}) {
        SCOPED_TRACE(incoming);
        auto reject = [&](auto mutate) {
            auto data = graph.Data();
            mutate(incoming ? data.incoming : data.outgoing);
            EXPECT_THROW((CompactGraph(std::move(data))), std::invalid_argument);
        };
        reject([](auto& index) { index.offsets.clear(); });
        reject([](auto& index) { index.offsets.pop_back(); });
        reject([](auto& index) { index.offsets.front() = 1; });
        reject([](auto& index) { ++index.offsets.back(); });
        reject([](auto& index) {
            index.offsets[1] = std::numeric_limits<gk_compact::Offset>::max();
        });
        reject([](auto& index) { index.edges.front().neighbor = gk_compact::kNoType; });
        reject([](auto& index) { index.edges.front().predicate = gk_compact::kNoType; });
        reject([&](auto& index) {
            auto const vertex =
                    *gk_compact::FindEntity(graph.Data().vertices, incoming ? "c" : "a");
            std::swap(index.edges[index.offsets[vertex]], index.edges[index.offsets[vertex] + 1]);
        });
        reject([&](auto& index) {
            auto const vertex =
                    *gk_compact::FindEntity(graph.Data().vertices, incoming ? "c" : "a");
            index.edges[index.offsets[vertex] + 1] = index.edges[index.offsets[vertex]];
        });
    }
}

TEST(GkCompactGraphTest, ConstructorRejectsDisagreeingDirectionsAndValueSubjects) {
    auto const graph = gk_compact::BuildGraph(ExampleGraph());
    auto data = graph.Data();
    data.input_triple_count = 0;
    EXPECT_THROW((CompactGraph(data)), std::invalid_argument);
    data = graph.Data();
    data.incoming.edges.pop_back();
    --data.incoming.offsets.back();
    EXPECT_THROW((CompactGraph(data)), std::invalid_argument);
    data = graph.Data();
    auto const value = *gk_compact::FindValue(data.vertices, "Alice");
    data.incoming.edges[data.incoming.offsets[value]].neighbor = 1;
    EXPECT_THROW((CompactGraph(data)), std::invalid_argument);

    // Both indexes agree and IDs are valid, but a value cannot be a subject.
    data = {};
    data.vertices = {{"a", "v"}, {0, gk_compact::kNoType}, 1};
    data.types = {"Person"};
    data.predicates = {"p"};
    data.outgoing = {{0, 0, 1}, {{0, 0}}};
    data.incoming = {{0, 1, 1}, {{0, 1}}};
    data.input_triple_count = 1;
    EXPECT_THROW((CompactGraph(data)), std::invalid_argument);
}
