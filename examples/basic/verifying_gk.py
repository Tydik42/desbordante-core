"""Minimal GK validation example.

The first graph is read from a GK TSV file.  The second graph is assembled in
memory.  GraphKey is constructed in Python in both cases because the current
GK PoC does not expose a GraphKey file parser yet.
"""

from pathlib import Path

import desbordante


GRAPH_FILE = Path(__file__).resolve().parents[1] / "datasets" / "gk" / "simple_graph.tsv"


def make_key():
    gk = desbordante.gk_verification
    x = gk.Variable(gk.VarKind.ENTITY, "x")
    y = gk.Variable(gk.VarKind.ENTITY, "y")

    return gk.GraphKey(
        [gk.PatternTriple(x, "p", y)],
        {x: "X", y: "Y"},
        x,
    )


def print_key(key) -> None:
    print(f"GraphKey: {key}")


def print_graph(graph) -> None:
    for triple in graph:
        print(f"  {triple}")


def check_graph_from_file() -> None:
    gk = desbordante.gk_verification
    key = make_key()

    print(f"Graph loaded from file: {GRAPH_FILE}")
    print(GRAPH_FILE.read_text().rstrip())
    print_key(key)

    validator = gk.algorithms.GkValidator()
    validator.load_data(gk_graph_path=GRAPH_FILE, gk_key=key)
    validator.execute()

    print(f"File graph: holds={validator.holds}, violations={validator.get_violations()}")
    assert not validator.holds
    assert validator.get_violations() == [("a", "b")]


def check_graph_created_in_memory() -> None:
    gk = desbordante.gk_verification
    entity = gk.NodeKind.ENTITY
    graph = [
        gk.Triple(
            gk.Node(entity, "a", "X"),
            "p",
            gk.Node(entity, "target_a", "Y"),
        ),
        gk.Triple(
            gk.Node(entity, "b", "X"),
            "p",
            gk.Node(entity, "target_b", "Y"),
        ),
    ]
    key = make_key()

    print("Graph created in memory:")
    print_graph(graph)
    print_key(key)

    validator = gk.algorithms.GkValidator()
    validator.load_data(gk_graph=graph, gk_key=key)
    validator.execute()

    print(f"In-memory graph: holds={validator.holds}, violations={validator.get_violations()}")
    assert validator.holds
    assert validator.get_violations() == []


if __name__ == "__main__":
    check_graph_from_file()
    check_graph_created_in_memory()
    print("GK smoke check passed")
