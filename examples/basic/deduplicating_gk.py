"""Deduplicate entities using the same graph and key types as GkValidator.

The first key matches A entities whose p-neighbors are equivalent.
The second key makes Z entities equivalent when they share a code.
Thus merging c1/c2 enables merging a/b on a later Chase pass.
No vertices or triples in the input graph are rewritten.
"""

import desbordante


def main():
    gk = desbordante.gk_deduplication
    x = gk.Variable(gk.VarKind.ENTITY, "x")
    y = gk.Variable(gk.VarKind.ENTITY, "y")
    value = gk.Variable(gk.VarKind.VALUE, "value")
    keys = [
        gk.GraphKey([gk.PatternTriple(x, "p", y)], {x: "A", y: "Z"}, x),
        gk.GraphKey([gk.PatternTriple(x, "code", value)], {x: "Z"}, x),
    ]

    def entity(identifier, entity_type):
        return gk.Node(gk.NodeKind.ENTITY, identifier, entity_type)

    graph = [
        gk.Triple(entity("a", "A"), "p", entity("c1", "Z")),
        gk.Triple(entity("b", "A"), "p", entity("c2", "Z")),
        gk.Triple(entity("c1", "Z"), "code", gk.Node(gk.NodeKind.VALUE, "shared")),
        gk.Triple(entity("c2", "Z"), "code", gk.Node(gk.NodeKind.VALUE, "shared")),
    ]
    print("Graph:")
    for triple in graph:
        print(" ", triple)
    print("Keys:")
    for key in keys:
        print(" ", key)

    algorithm = gk.algorithms.GkDeduplicator()
    algorithm.load_data(gk_graph=graph, gk_keys=keys)
    # A file can be loaded instead with gk_graph_path="graph.csv", gk_keys=keys.
    algorithm.execute()
    print("Entity classes:", algorithm.get_clusters())
    print("Successful merges:", algorithm.merge_count)
    stats = algorithm.timing_stats
    print("Chase passes:", stats.execution.pass_count)
    print("Load time (ms):", stats.load_total_ns / 1_000_000)
    print("Execute time (ms):", stats.execute_total_ns / 1_000_000)
    assert algorithm.get_clusters() == [["a", "b"], ["c1", "c2"]]


if __name__ == "__main__":
    main()
