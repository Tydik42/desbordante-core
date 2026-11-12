import argparse
import logging
import pickle
from pathlib import Path

import desbordante


gk = desbordante.gk_verification
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

    a, b = entity("a", "Person"), entity("b", "Person")
    company = entity("company", "Company")
    triples = []
    for person, city in ((a, "city1"), (b, "city2")):
        triples.extend([
            gk.Triple(person, "worksAt", company),
            gk.Triple(person, "name", value("Alex")),
            gk.Triple(person, "livesIn", entity(city, "City")),
            gk.Triple(person, "status", value("active")),
            gk.Triple(person, "note", value(person.id_or_val)),
        ])
    triples.append(triples[0])

    x = gk.Variable(gk.VarKind.ENTITY, "x")
    y = gk.Variable(gk.VarKind.ENTITY, "y")
    z = gk.Variable(gk.VarKind.VALUE, "z")
    w = gk.Variable(gk.VarKind.WILDCARD, "w")
    active = gk.Variable(gk.VarKind.CONSTANT, "active")
    keys = {
        "All variable kinds": gk.GraphKey(
            [gk.PatternTriple(x, "worksAt", y),
             gk.PatternTriple(x, "name", z),
             gk.PatternTriple(x, "livesIn", w),
             gk.PatternTriple(x, "status", active)],
            {x: "Person", y: "Company", w: "City"}, x),
        "Distinct cities": gk.GraphKey(
            [gk.PatternTriple(x, "livesIn", y)], {x: "Person", y: "City"}, x),
        "Missing predicate": gk.GraphKey(
            [gk.PatternTriple(x, "absent", z)], {x: "Person"}, x),
    }
    return triples, keys


def show_key(key):
    print(key.to_string())
    print("Root:", key.root.name, key.root.kind)
    print("Types:", sorted((v.name, t) for v, t in key.types.items()))
    for triple in key.pattern:
        print("Pattern:", triple.subject.name, triple.predicate, triple.object.name)
    restored = pickle.loads(pickle.dumps(key))
    reordered = gk.GraphKey(list(reversed(key.pattern)), key.types, key.root)
    assert key == restored == reordered
    assert len({key, restored, reordered}) == 1


def validate(constructor, source, key, expected):
    algorithm = constructor()
    algorithm.load_data(**source, gk_key=key)
    print("Algorithm:", constructor.__name__)
    print("Options:", sorted(algorithm.get_possible_options()))
    print("Localization:", algorithm.get_description("use_localization"))
    for name, options in STAGES:
        algorithm.execute(**options)
        violations = algorithm.get_violations()
        print(name, "holds:", algorithm.holds, "violations:", violations)
        assert violations == expected
        assert algorithm.holds == (not expected)
        assert algorithm.get_opts()["gk_key"] == key
    algorithm.load_data()
    algorithm.execute()
    assert algorithm.get_violations() == expected


def show_optimizations(graph, key):
    ops = gk.optimizations
    first = ops.localize(graph, "a", 1)
    second = ops.localize(graph, "b", 1)
    print("Neighborhoods:", first, second)
    pairing = ops.compute_pairing(key, first, second)
    print("Pairable:", pairing.can_pair("a", "b"), "pairs:", pairing.pair_count)
    assert pairing.can_pair("a", "b")
    for variable in pairing.variables:
        print("Candidates:", variable, pairing.candidate_count(variable),
              pairing.get_candidates(variable))
    reduced = ops.reduce_subgraphs(pairing)
    for index, subgraph in enumerate(reduced, 1):
        print("Reduced graph", index, subgraph)
        print("Vertices:", subgraph.get_vertices())
    assert all(subgraph.edge_count < first.edge_count for subgraph in reduced)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--trace-log", type=Path)
    args = parser.parse_args()
    if args.trace_log:
        logging.basicConfig(filename=args.trace_log, level=logging.TRACE)
        logging.getLogger("desbordante").setLevel(logging.TRACE)

    print("Default algorithm:", gk.algorithms.Default.__name__)
    print("Compact validator available:", gk.compact_validator_available)
    triples, keys = make_graph_and_keys()
    print("In-memory triples:")
    for triple in triples:
        print(triple)
    graph = gk.CompactGraph.from_triples(iter(triples))
    print("Compact graph:", graph)
    print("Counts:", graph.vertex_count, graph.entity_count,
          graph.edge_count, graph.input_triple_count)
    for node in graph.get_vertices():
        print("Vertex:", node.kind, node.id_or_val, repr(node.type))

    for name, key in keys.items():
        print("Key:", name)
        show_key(key)
        expected = [("a", "b")] if name == "All variable kinds" else []
        validate(gk.algorithms.GkValidator, {"gk_graph": triples}, key, expected)
        validate(gk.algorithms.CompactGkValidator, {"gk_graph": graph}, key, expected)
    show_optimizations(graph, keys["All variable kinds"])

    x = gk.Variable(gk.VarKind.ENTITY, "x")
    y = gk.Variable(gk.VarKind.ENTITY, "y")
    key = gk.GraphKey([gk.PatternTriple(x, "p", y)], {x: "X", y: "Y"}, x)
    print("CSV graph:", GRAPH_FILE.name)
    print(GRAPH_FILE.read_text().rstrip())
    for constructor in (gk.algorithms.GkValidator, gk.algorithms.CompactGkValidator):
        validate(constructor, {"gk_graph_path": GRAPH_FILE}, key, [("a", "b")])
    validate(gk.algorithms.CompactGkValidator,
             {"gk_graph": gk.CompactGraph.from_csv(GRAPH_FILE)}, key, [("a", "b")])
    print("GK validation examples passed")


if __name__ == "__main__":
    main()
