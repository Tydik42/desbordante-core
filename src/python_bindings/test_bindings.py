import pathlib
import pickle
import tempfile
import unittest
from collections import namedtuple
from itertools import chain

import desbordante as desb

OptionContainer = namedtuple("OptionContainer", ['path', 'load_options', 'execute_options'])
FailureCaseContainer = namedtuple("FailureCaseContainer", ['path', 'options'])

TABLE_ONLY_CONTAINER = OptionContainer(
    "WDC_satellites.csv", {}, {}
)


def get_common_option_container(execute_options):
    return OptionContainer(
        "WDC_satellites.csv", {}, execute_options
    )


def get_apriori_load_container(load_options):
    return OptionContainer("TestWide.csv", load_options, {})


def check_metric_verifier_failure(dataset, options) -> bool:
    alg = desb.mfd_verification.algorithms.MetricVerifier()
    alg.load_data(table=(dataset, ",", True))
    for opt_name in options:
        alg._set_option(opt_name, options[opt_name])


ALGO_CORRECT_OPTIONS_INFO = [
    (desb.fd.algorithms.Depminer, [TABLE_ONLY_CONTAINER]),
    (desb.fd.algorithms.FUN, [TABLE_ONLY_CONTAINER]),
    (desb.fd.algorithms.FdMine, [TABLE_ONLY_CONTAINER]),
    (desb.fd.algorithms.HyFD, [TABLE_ONLY_CONTAINER]),
    (desb.fd.algorithms.EulerFD, [TABLE_ONLY_CONTAINER]),
    (desb.afd.algorithms.Pyro, [
        get_common_option_container(
            {"seed": 1, "max_lhs": 12, "threads": 5, "error": 0.015}
        ),
    ]),
    (desb.fd.algorithms.DFD, [
        get_common_option_container(
            {
                "threads": 15,
            }
        ),
    ]),
    (desb.fd.algorithms.FastFDs, [
        get_common_option_container({"max_lhs": 12, "threads": 15}),
    ]),
    (desb.afd.algorithms.Tane, [
        get_common_option_container({"max_lhs": 12, "error": 0.015}),
    ]),
    (desb.statistics.algorithms.DataStats, [
        get_common_option_container({"threads": 15}),
    ]),
    (desb.ucc.algorithms.HyUCC, [
        get_common_option_container({"threads": 15}),
    ]),
    (desb.fd.algorithms.EulerFD, [
        get_common_option_container({"custom_random_seed": 102}),
    ]),
    (desb.fd_verification.algorithms.FDVerifier, [
        get_common_option_container(
            {"lhs_indices": [1, 2, 3], "rhs_indices": [1, 2, 3]}
        ),
    ]),
    (desb.ar.algorithms.Apriori, [
        get_apriori_load_container({"input_format": "tabular", "has_tid": True}),
        get_apriori_load_container({"input_format": "tabular", "has_tid": False}),
        get_apriori_load_container(
            {"input_format": "singular", "tid_column_index": 0, "item_column_index": 2}
        ),
        OptionContainer(
            "rules-kaggle-rows.csv",
            {
                "input_format": "singular",
                "tid_column_index": 0,
                "item_column_index": 1,
            },
            {"minconf": 0.00312, "minsup": 0.2321},
        ),
    ]),
    (desb.mfd_verification.algorithms.MetricVerifier, [
        OptionContainer(
            "TestLong.csv",
            {},
            {
                "metric": "euclidean",
                "rhs_indices": [1, 2],
                "parameter": 213.213111,
                "dist_from_null_is_infinity": False,
                "metric_algorithm": "approx",
                "lhs_indices": [0, 1, 2],
            },
        ),
        OptionContainer(
            "WDC_satellites.csv",
            {},
            {
                "metric": "levenshtein",
                "rhs_indices": [1],
                "parameter": 213.213111,
                "dist_from_null_is_infinity": False,
                "metric_algorithm": "approx",
                "lhs_indices": [0, 1, 2],
            },
        ),
        OptionContainer(
            "WDC_satellites.csv",
            {},
            {
                "metric": "cosine",
                "rhs_indices": [1],
                "parameter": 213.213111,
                "dist_from_null_is_infinity": False,
                "q": 123,
                "metric_algorithm": "approx",
                "lhs_indices": [0, 1, 2],
            },
        ),
    ]),
    (desb.pac_verification.algorithms.DomainPACVerifier, [
        OptionContainer(
            "TestLong.csv",
            {
                "column_indices": [0, 1],
                "domain": desb.pac.domains.Parallelepiped(["0", "0"], ["5", "5"], [1, 1.2]),
            },
            {
                "min_epsilon": 0,
                "max_epsilon": 7,
                "min_delta": 0.8,
                "diagonal_threshold": 1e-10,
            },
        ),
        OptionContainer(
            "TestLong.csv",
            {
                "column_indices": [1],
                "domain": desb.pac.domains.CustomDomain(
                    lambda val: int(val[0]) < 7, "[-\\infty, 7]"),
            },
            {
                "max_epsilon": 7,
                "min_delta": 0.8,
            },
        )
    ]),
]

METRIC_VERIFIER_FAILURE_CASES = [
    FailureCaseContainer(
        "WDC_satellites.csv",
        {
            "metric": "euclidean",
            "rhs_indices": [1, 2],
        }
    ),
    
    FailureCaseContainer(
        "TestLong.csv",
        {
            "metric": "euclidean",
            "q": 123,
        }
    ),
    FailureCaseContainer(
        "TestLong.csv",
        {
            "metric": "euclidean",
            "metric_algorithm": "brute",
        }
    ),
    FailureCaseContainer(
        "TestLong.csv",
        {"metric": "levenshtein", "rhs_indices": [1, 2]}
    ),
    FailureCaseContainer("WDC_satellites.csv", {"metric": "levenshtein", "q": 123}),
    FailureCaseContainer(
        "WDC_satellites.csv",
        {
            "metric": "levenshtein",
            "metric_algorithm": "brute",
        }
    ),
    FailureCaseContainer("TestLong.csv", {"metric": "levenshtein", "rhs_indices": [1]}),
    FailureCaseContainer("TestLong.csv", {"rhs_indices": [1]}),
    FailureCaseContainer("WDC_satellites.csv", {"rhs_indices": [1, 2]}),
    FailureCaseContainer("WDC_satellites.csv", {"q": 123}),
    FailureCaseContainer("WDC_satellites.csv", {"metric_algorithm": "approx"}),
]


class TestPythonBindings(unittest.TestCase):
    def _configure_and_verify_no_extras(self, testing_algo, params):
        while option_names := testing_algo._get_needed_options():
            for option_name in option_names:
                testing_algo._set_option(option_name, params.pop(option_name, None))

        self.assertEqual(params, {})

    def _test_correct_option_setting(
        self, algo, path, separator, has_header, options: dict
    ):
        testing_algo = algo()
        self._configure_and_verify_no_extras(
                testing_algo,  {'table': (path, separator, has_header)} | options.load_options)
        testing_algo.load_data()

        self._configure_and_verify_no_extras(testing_algo, options.execute_options)

        algo_option_values = testing_algo.get_opts()
        for name, value in chain(options.execute_options.items(),
                                 options.load_options.items()):
            self.assertEqual(value, algo_option_values[name])

    def test_correct_load(self):
        for algo, option_containers in ALGO_CORRECT_OPTIONS_INFO:
            for option in option_containers:
                with self.subTest(msg=f"testing setting correct options for {algo.__name__}"):
                    self._test_correct_option_setting(
                        algo, option.path, ",", True, option
                    )

    def test_metric_verifier_failure_cases(self):
        for load in METRIC_VERIFIER_FAILURE_CASES:
            with self.subTest(msg=f"metric_verifier_load: {load}"):
                with self.assertRaises(desb.ConfigurationError):
                    check_metric_verifier_failure(load.path, load.options)
                

class TestGkBindings(unittest.TestCase):
    @staticmethod
    def make_graph_and_key():
        entity = desb.gk_verification.NodeKind.ENTITY
        wildcard = desb.gk_verification.VarKind.WILDCARD
        entity_var = desb.gk_verification.VarKind.ENTITY

        x = desb.gk_verification.Variable(entity_var, "x")
        y = desb.gk_verification.Variable(wildcard, "y")
        key = desb.gk_verification.GraphKey(
            [desb.gk_verification.PatternTriple(x, "p", y)],
            {x: "X", y: "Y"},
            x,
        )

        graph = [
            desb.gk_verification.Triple(
                desb.gk_verification.Node(entity, "a", "X"),
                "p",
                desb.gk_verification.Node(entity, "a_target", "Y"),
            ),
            desb.gk_verification.Triple(
                desb.gk_verification.Node(entity, "b", "X"),
                "p",
                desb.gk_verification.Node(entity, "b_target", "Y"),
            ),
        ]
        return graph, key

    def test_gk_model_and_key_data_model(self):
        _, key = self.make_graph_and_key()

        self.assertEqual(key, pickle.loads(pickle.dumps(key)))
        self.assertEqual(len({key, pickle.loads(pickle.dumps(key))}), 1)
        self.assertIn("GraphKey", str(key))
        self.assertEqual(key.root.name, "x")
        self.assertEqual(len(key.pattern), 1)
        self.assertEqual(key.types[key.root], "X")

        y = key.pattern[0].object
        z = desb.gk_verification.Variable(desb.gk_verification.VarKind.WILDCARD, "z")
        reordered = desb.gk_verification.GraphKey(
            [
                desb.gk_verification.PatternTriple(y, "q", z),
                desb.gk_verification.PatternTriple(key.root, "p", y),
            ],
            {key.root: "X", y: "Y", z: "Z"},
            key.root,
        )
        same_semantics = desb.gk_verification.GraphKey(
            list(reversed(reordered.pattern)), reordered.types, reordered.root
        )
        self.assertEqual(reordered, same_semantics)

    def test_gk_validator_uses_the_shared_graph_key_type(self):
        graph, key = self.make_graph_and_key()
        validator = desb.gk_verification.algorithms.GkValidator()

        self.assertEqual(validator._get_option_type("gk_graph"),
                         (list, desb.gk_verification.Triple))
        self.assertEqual(validator._get_option_type("gk_key"),
                         (desb.gk_verification.GraphKey,))

        validator.load_data(gk_graph=graph, gk_key=key)
        self.assertEqual(validator.get_opts()["gk_key"], key)
        self.assertEqual(validator.get_opts()["gk_graph"], graph)

        validator.execute()

        self.assertFalse(validator.holds)
        self.assertEqual(validator.get_violations(), [("a", "b")])
        self.assertEqual(len(validator.get_violations()), 1)

        stats = validator.timing_stats
        self.assertIsInstance(stats, desb.gk_verification.ValidationTimingStats)
        self.assertEqual(stats.graph_triple_count, len(graph))
        self.assertEqual(stats.graph_entity_count, 4)
        self.assertEqual(stats.root_candidate_count, 2)
        self.assertEqual(stats.root_pair_count, 1)
        self.assertEqual(stats.checked_pair_count, 1)
        self.assertEqual(stats.violation_count, 1)
        self.assertEqual(stats.load_source_ns, 0)
        self.assertGreater(stats.load_total_ns, 0)
        self.assertGreater(stats.execute_total_ns, 0)

    def test_gk_validator_loads_graph_from_tsv_path(self):
        _, key = self.make_graph_and_key()
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "graph.tsv"
            path.write_text(
                'subject_id\tsubject_type\tpredicate\tobject_kind\tobject_id_or_value\tobject_type\n'
                'a\tX\tp\tentity\ttarget\tY\n'
                'b\tX\tp\tentity\ttarget\tY\n'
            )

            validator = desb.gk_verification.algorithms.GkValidator()
            self.assertEqual(validator._get_option_type("gk_graph_path"), (str,))
            validator.load_data(gk_graph_path=path, gk_key=key)
            self.assertEqual(str(validator.get_opts()["gk_graph_path"]), str(path))
            validator.execute()

            self.assertFalse(validator.holds)
            self.assertEqual(validator.get_violations(), [("a", "b")])

    def test_gk_future_option_is_not_silently_ignored(self):
        graph, key = self.make_graph_and_key()
        validator = desb.gk_verification.algorithms.GkValidator()
        validator.load_data(gk_graph=graph, gk_key=key)

        with self.assertRaises(RuntimeError):
            validator.execute(use_pairing_filter=True)



class TestMaxFEM(unittest.TestCase):
    # Sequence: event 1 three times, event 2 twice (infrequent at minsup=3),
    # window_size=1 prevents composite episodes.
    # Expected: one maximal episode [[1]] with support 3.
    _SEQUENCE = [[1], [2], [1], [2], [1]]
    _MINSUP = 3
    _WINDOW_SIZE = 1
    _EXPECTED = [([[1]], 3)]

    def _run(self, sequence_arg):
        alg = desb.fem.MaxFEM()
        alg.load_data(sequence=sequence_arg)
        alg.execute(minsup=self._MINSUP, window_size=self._WINDOW_SIZE)
        return alg.get_max_frequent_episodes()

    def test_iterable_input(self):
        result = self._run(self._SEQUENCE)
        self.assertEqual(result, self._EXPECTED)

    def test_path_input(self):
        content = "\n".join(" ".join(str(e) for e in events) for events in self._SEQUENCE)
        with tempfile.NamedTemporaryFile(mode="w", suffix=".txt", delete=False) as f:
            f.write(content)
            tmp_path = pathlib.Path(f.name)
        try:
            result = self._run(tmp_path)
            self.assertEqual(result, self._EXPECTED)
        finally:
            tmp_path.unlink()


if __name__ == "__main__":
    unittest.main()
