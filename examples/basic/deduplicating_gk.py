import argparse
import logging
import pickle
from pathlib import Path

import desbordante


gk = desbordante.gk_deduplication
GRAPH_FILE = Path(__file__).resolve().parents[1] / "datasets" / "gk" / "simple_graph.csv"
STAGES = (
    ("Exact", {}),
    ("L", {"use_localization": True}),
    ("L+P", {"use_localization": True, "use_pairing_filter": True}),
    ("L+P+R", {"use_localization": True, "use_pairing_filter": True,
               "use_subgraph_reduction": True}),
)


def make_graph_and_keys():
    def entity(identifier, entity_type):
        return gk.Node(gk.NodeKind.ENTITY, identifier, entity_type)

    def value(text):
        return gk.Node(gk.NodeKind.VALUE, text)

    x = gk.Variable(gk.VarKind.ENTITY, "x")
    y = gk.Variable(gk.VarKind.ENTITY, "y")
    z = gk.Variable(gk.VarKind.VALUE, "z")
    w = gk.Variable(gk.VarKind.WILDCARD, "w")
    active = gk.Variable(gk.VarKind.CONSTANT, "active")
    keys = [
        gk.GraphKey([gk.PatternTriple(x, "worksAt", y)],
                    {x: "Person", y: "Company"}, x),
        gk.GraphKey([gk.PatternTriple(x, "code", z),
                     gk.PatternTriple(x, "locatedIn", w),
                     gk.PatternTriple(x, "status", active)],
                    {x: "Company", w: "City"}, x),
    ]
    triples = [
        gk.Triple(entity("a", "Person"), "worksAt", entity("c1", "Company")),
        gk.Triple(entity("b", "Person"), "worksAt", entity("c2", "Company")),
    ]
    for company, code, city in (("c1", "shared", "city1"),
                                ("c2", "shared", "city2"),
                                ("c3", "unique", "city3")):
        node = entity(company, "Company")
        triples.extend([
            gk.Triple(node, "code", value(code)),
            gk.Triple(node, "locatedIn", entity(city, "City")),
            gk.Triple(node, "status", value("active")),
        ])
    return triples, keys


def deduplicate(constructor, source, keys, expected):
    algorithm = constructor()
    algorithm.load_data(**source, gk_keys=keys)
    print("Algorithm:", constructor.__name__)
    print("Options:", sorted(algorithm.get_possible_options()))
    print("Localization:", algorithm.get_description("use_localization"))
    for name, options in STAGES:
        algorithm.execute(**options)
        clusters = algorithm.get_clusters()
        print(name, "classes:", clusters, "class count:", algorithm.cluster_count,
              "merges:", algorithm.merge_count)
        assert clusters == expected
        assert algorithm.cluster_count == len(expected)
        assert algorithm.merge_count == sum(len(cluster) - 1 for cluster in expected)
        assert algorithm.get_opts()["gk_keys"] == keys
        clusters.clear()
        assert algorithm.get_clusters() == expected
    algorithm.load_data()
    algorithm.execute()
    assert algorithm.get_clusters() == expected


def show_optimizations(graph, key):
    ops = gk.optimizations
    first = ops.localize(graph, "c1", 1)
    second = ops.localize(graph, "c2", 1)
    print("Company neighborhoods:", first, second)
    pairing = ops.compute_pairing(key, first, second)
    print("Pairable:", pairing.can_pair("c1", "c2"), "pairs:", pairing.pair_count)
    assert pairing.can_pair("c1", "c2")
    for variable in pairing.variables:
        print("Candidates:", variable, pairing.candidate_count(variable),
              pairing.get_candidates(variable))
    for index, reduced in enumerate(ops.reduce_subgraphs(pairing), 1):
        print("Reduced graph", index, reduced, reduced.get_vertices())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--trace-log", type=Path)
    args = parser.parse_args()
    if args.trace_log:
        logging.basicConfig(filename=args.trace_log, level=logging.TRACE)
        logging.getLogger("desbordante").setLevel(logging.TRACE)

    print("Default algorithm:", gk.algorithms.Default.__name__)
    print("Compact deduplicator available:", gk.compact_deduplicator_available)
    assert gk.GraphKey is desbordante.gk_verification.GraphKey
    triples, keys = make_graph_and_keys()
    print("In-memory triples:")
    for triple in triples:
        print(triple)
    for key in keys:
        print("Key:", key.to_string())
        print("Root:", key.root, "pattern:", key.pattern, "types:",
              sorted((variable.name, type_id) for variable, type_id in key.types.items()))
        assert key == pickle.loads(pickle.dumps(key))
    assert len(set(keys)) == 2

    graph = gk.CompactGraph.from_triples(iter(triples))
    print("Compact graph:", graph)
    print("Counts:", graph.vertex_count, graph.entity_count,
          graph.edge_count, graph.input_triple_count)
    for node in graph.get_vertices():
        print("Vertex:", node.kind, node.id_or_val, repr(node.type))
    show_optimizations(graph, keys[1])

    expected = [["a", "b"], ["c1", "c2"], ["c3"], ["city1"], ["city2"], ["city3"]]
    for constructor, source in (
        (gk.algorithms.GkDeduplicator, {"gk_graph": triples}),
        (gk.algorithms.CompactGkDeduplicator, {"gk_graph": graph}),
    ):
        deduplicate(constructor, source, keys, expected)
        singletons = [[node.id_or_val] for node in graph.get_vertices()
                      if node.kind == gk.NodeKind.ENTITY]
        deduplicate(constructor, source, [], sorted(singletons))
    assert graph.edge_count == len(triples)
    assert graph.entity_count == sum(map(len, expected))

    x = gk.Variable(gk.VarKind.ENTITY, "x")
    y = gk.Variable(gk.VarKind.ENTITY, "y")
    key = gk.GraphKey([gk.PatternTriple(x, "p", y)], {x: "X", y: "Y"}, x)
    print("CSV graph:", GRAPH_FILE.name)
    print(GRAPH_FILE.read_text().rstrip())
    for constructor in (gk.algorithms.GkDeduplicator, gk.algorithms.CompactGkDeduplicator):
        deduplicate(constructor, {"gk_graph_path": GRAPH_FILE}, [key],
                    [["a", "b"], ["shared"]])
    deduplicate(gk.algorithms.CompactGkDeduplicator,
                {"gk_graph": gk.CompactGraph.from_csv(GRAPH_FILE)}, [key],
                [["a", "b"], ["shared"]])
    print("GK deduplication examples passed")


if __name__ == "__main__":
    main()
