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

    def test_gk_validator_loads_graph_from_csv_path(self):
        _, key = self.make_graph_and_key()
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "graph.csv"
            path.write_text(
                'subject_id,subject_type,predicate,object_kind,object_id_or_value,object_type\n'
                '"a,quoted",X,p,entity,"target ""Y""",Y\n'
                'b,X,p,entity,"target ""Y""",Y\n'
            )

            validator = desb.gk_verification.algorithms.GkValidator()
            self.assertEqual(validator._get_option_type("gk_graph_path"), (str,))
            validator.load_data(gk_graph_path=path, gk_key=key)
            self.assertEqual(str(validator.get_opts()["gk_graph_path"]), str(path))
            validator.execute()

            self.assertFalse(validator.holds)
            self.assertEqual(validator.get_violations(), [("a,quoted", "b")])

    def test_gk_optimizations_preserve_results_and_report_filtering(self):
        graph, key = self.make_graph_and_key()
        gk = desb.gk_verification
        graph.append(gk.Triple(gk.Node(gk.NodeKind.ENTITY, "c", "X"),
                               "other", gk.Node(gk.NodeKind.VALUE, "unrelated")))
        validator = desb.gk_verification.algorithms.GkValidator()
        validator.load_data(gk_graph=graph, gk_key=key)

        for pairing, reduction in [(False, False), (True, False), (False, True),
                                   (True, True), (False, False)]:
            with self.subTest(pairing=pairing, reduction=reduction):
                validator.execute(use_pairing_filter=pairing, use_subgraph_reduction=reduction)
                self.assertEqual(validator.get_violations(), [("a", "b")])
                stats = validator.timing_stats.evaluation
                self.assertIsInstance(stats, gk.EvaluationStatistics)
                self.assertEqual(stats.pairing_check_count, 3 if pairing or reduction else 0)
                self.assertEqual(stats.filtered_pair_count, 2 if pairing or reduction else 0)
                self.assertEqual(stats.search_call_count, 1 if pairing or reduction else 3)
                self.assertEqual(stats.reduced_triple_count, 2 if reduction else 0)



class TestGkDeduplicationBindings(unittest.TestCase):
    @staticmethod
    def make_graph_and_keys():
        gk = desb.gk_verification
        x = gk.Variable(gk.VarKind.ENTITY, "x")
        y = gk.Variable(gk.VarKind.ENTITY, "y")
        value = gk.Variable(gk.VarKind.VALUE, "value")
        entity_key = gk.GraphKey([gk.PatternTriple(x, "p", y)], {x: "A", y: "Z"}, x)
        value_key = gk.GraphKey([gk.PatternTriple(x, "code", value)], {x: "Z"}, x)

        def entity(identifier, entity_type):
            return gk.Node(gk.NodeKind.ENTITY, identifier, entity_type)

        graph = [
            gk.Triple(entity("a", "A"), "p", entity("c1", "Z")),
            gk.Triple(entity("b", "A"), "p", entity("c2", "Z")),
            gk.Triple(entity("c1", "Z"), "code", gk.Node(gk.NodeKind.VALUE, "shared")),
            gk.Triple(entity("c2", "Z"), "code", gk.Node(gk.NodeKind.VALUE, "shared")),
        ]
        return graph, [entity_key, value_key]

    def test_shared_descriptors_clusters_and_repeated_execution(self):
        module = desb.gk_deduplication
        self.assertIs(module.GraphKey, desb.gk_verification.GraphKey)
        graph, keys = self.make_graph_and_keys()
        algorithm = module.algorithms.GkDeduplicator()
        self.assertEqual(algorithm._get_option_type("gk_keys"), (list, module.GraphKey))
        algorithm.load_data(gk_graph=graph, gk_keys=keys)
        self.assertEqual(algorithm.get_opts()["gk_keys"], keys)
        self.assertEqual(algorithm.get_opts()["gk_graph"], graph)
        for _ in range(2):
            algorithm.execute()
            self.assertEqual(algorithm.get_clusters(), [["a", "b"], ["c1", "c2"]])
            self.assertEqual(algorithm.cluster_count, 2)
            self.assertEqual(algorithm.merge_count, 2)
            stats = algorithm.timing_stats
            self.assertIsInstance(stats, module.DeduplicationTimingStats)
            self.assertEqual(stats.key_count, 2)
            self.assertEqual(stats.graph_triple_count, 4)
            self.assertEqual(stats.execution.entity_count, 4)
            self.assertEqual(stats.execution.merge_count, 2)
            self.assertGreaterEqual(stats.execution.pass_count, 3)
            self.assertGreaterEqual(stats.execute_total_ns, stats.execution.total_ns)
            clusters = algorithm.get_clusters()
            clusters[0].clear()
            self.assertEqual(algorithm.get_clusters(), [["a", "b"], ["c1", "c2"]])

    def test_loads_existing_csv_and_preserves_singletons_without_keys(self):
        _, keys = self.make_graph_and_keys()
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "graph.csv"
            path.write_text(
                'subject_id,subject_type,predicate,object_kind,object_id_or_value,object_type\n'
                '"a,quoted",A,p,entity,c1,Z\n'
                'b,A,p,entity,c2,Z\n'
                'c1,Z,code,value,shared,-\n'
                'c2,Z,code,value,shared,-\n'
            )
            for input_keys, expected in [
                (keys, [["a,quoted", "b"], ["c1", "c2"]]),
                ([], [["a,quoted"], ["b"], ["c1"], ["c2"]]),
            ]:
                algorithm = desb.gk_deduplication.algorithms.GkDeduplicator()
                algorithm.load_data(gk_graph_path=path, gk_keys=input_keys)
                algorithm.execute()
                self.assertEqual(algorithm.get_clusters(), expected)
                self.assertGreater(algorithm.timing_stats.load_source_ns, 0)
        algorithm = desb.gk_deduplication.algorithms.GkDeduplicator()
        algorithm.load_data(gk_graph=[], gk_keys=[])
        algorithm.execute()
        self.assertEqual(algorithm.get_clusters(), [])
        self.assertEqual(algorithm.merge_count, 0)

    def test_optimizations_preserve_recursive_chase_and_reset_statistics(self):
        graph, keys = self.make_graph_and_keys()
        algorithm = desb.gk_deduplication.algorithms.GkDeduplicator()
        algorithm.load_data(gk_graph=graph, gk_keys=keys)
        for pairing, reduction in [(False, False), (True, False), (False, True),
                                   (True, True), (False, False)]:
            with self.subTest(pairing=pairing, reduction=reduction):
                algorithm.execute(use_pairing_filter=pairing, use_subgraph_reduction=reduction)
                self.assertEqual(algorithm.get_clusters(), [["a", "b"], ["c1", "c2"]])
                self.assertEqual(algorithm.merge_count, 2)
                execution = algorithm.timing_stats.execution
                self.assertEqual(execution.evaluation.pairing_check_count,
                                 execution.eval_call_count if pairing or reduction else 0)
                self.assertEqual(execution.evaluation.filtered_pair_count, 0)
                self.assertGreater(execution.evaluation.search_call_count, 0)


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
