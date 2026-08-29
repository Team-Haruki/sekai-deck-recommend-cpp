import argparse
import io
import os
import sys
import tempfile
import types
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[1]
BENCH_ROOT = REPO_ROOT / "tools" / "bench"
sys.path.insert(0, str(BENCH_ROOT))

import regress  # noqa: E402
import rl_quality  # noqa: E402


class RegressPathTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory(dir=REPO_ROOT)
        self.root = Path(self.temp_dir.name)

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_accepts_workspace_input_and_output(self):
        input_path = self.root / "input.json"
        input_path.write_text("{}", encoding="utf-8")
        self.assertEqual(regress.safe_cli_file(input_path, must_exist=True), input_path)
        self.assertEqual(
            regress.safe_cli_file(self.root / "output.json", must_exist=False),
            self.root / "output.json",
        )

    def test_rejects_empty_and_non_file_inputs(self):
        with self.assertRaisesRegex(ValueError, "must not be empty"):
            regress.safe_cli_file("", must_exist=True)
        with self.assertRaisesRegex(ValueError, "not a file"):
            regress.safe_cli_file(self.root, must_exist=True)

    def test_rejects_paths_outside_repository(self):
        outside = Path(tempfile.gettempdir()) / "sonar-public-output.json"
        with self.assertRaisesRegex(ValueError, "inside the repository"):
            regress.safe_cli_file(outside, must_exist=False)

    def test_rejects_symlink_escape(self):
        outside = Path(tempfile.gettempdir()) / "sonar-public-input.json"
        outside.write_text("{}", encoding="utf-8")
        link = self.root / "input-link.json"
        link.symlink_to(outside)
        try:
            with self.assertRaisesRegex(ValueError, "inside the repository"):
                regress.safe_cli_file(link, must_exist=True)
        finally:
            outside.unlink(missing_ok=True)

    def test_rejects_missing_parent_and_directory_output(self):
        with self.assertRaises(FileNotFoundError):
            regress.safe_cli_file(self.root / "missing" / "out.json", must_exist=False)
        with self.assertRaisesRegex(ValueError, "output path is not a file"):
            regress.safe_cli_file(self.root, must_exist=False)

    def test_resolves_workspace_output_symlink(self):
        target = self.root / "target.json"
        target.write_text("{}", encoding="utf-8")
        link = self.root / "output-link.json"
        link.symlink_to(target)
        self.assertEqual(regress.safe_cli_file(link, must_exist=False), target)


class RegressHelpersTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory(dir=REPO_ROOT)
        self.root = Path(self.temp_dir.name)

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_scenarios_include_optional_events(self):
        with (
            mock.patch.object(regress.common, "detect_events", return_value=(1, 2, 3)),
            mock.patch.object(regress.common, "detect_finale_event", return_value=4),
        ):
            cases = regress.scenarios()
        self.assertIn("ga-wl-multi-score", cases)
        self.assertIn("ga-finale-support", cases)

    def test_deck_repr_converts_nested_details(self):
        card = types.SimpleNamespace(
            card_id=1,
            skill_score_up=2,
            total_power=3,
            bonus=4,
            skill_level=5,
            master_rank=0,
            level=60,
            after_training=True,
            default_image="original",
        )
        deck = types.SimpleNamespace(
            score=10,
            live_score=11,
            total_power=12,
            event_bonus_rate=1.23456789,
            support_deck_bonus_rate=2.3456789,
            multi_live_score_up=3.456789,
            cards=[card],
            support_deck_cards=[card],
        )
        result = regress.deck_repr(deck)
        self.assertEqual(result["cards"], [1])
        self.assertEqual(result["event_bonus_rate"], 1.234568)

    def test_run_records_results_and_errors(self):
        deck = types.SimpleNamespace(
            score=10,
            live_score=11,
            total_power=12,
            event_bonus_rate=1.0,
            support_deck_bonus_rate=2.0,
            multi_live_score_up=3.0,
            cards=[],
            support_deck_cards=[],
        )
        engine = mock.Mock()
        engine.recommend.side_effect = [types.SimpleNamespace(decks=[deck]), RuntimeError("bad")]
        output = self.root / "results.json"
        with (
            mock.patch.object(regress.common, "make_engine", return_value=(object(), engine, {})),
            mock.patch.object(regress.common, "fixed_seeds", side_effect=lambda _m, options: options),
            mock.patch.object(regress.common, "base_options", return_value=types.SimpleNamespace()),
            mock.patch.object(
                regress,
                "scenarios",
                return_value={"success": {"target": "score"}, "failure": {"target": "score"}},
            ),
            redirect_stdout(io.StringIO()),
        ):
            regress.run(output)
        payload = __import__("json").loads(output.read_text(encoding="utf-8"))
        self.assertEqual(payload["results"]["success"][0]["score"], 10)
        self.assertEqual(payload["results"]["failure"], {"error": "bad"})

    def test_run_replaces_racing_symlink_without_overwriting_target(self):
        target = self.root / "target.json"
        target.write_text("do not overwrite", encoding="utf-8")
        output = regress.safe_cli_file(self.root / "results.json", must_exist=False)
        output.symlink_to(target)
        engine = mock.Mock()
        engine.recommend.return_value = types.SimpleNamespace(decks=[])
        with (
            mock.patch.object(regress.common, "make_engine", return_value=(object(), engine, {})),
            mock.patch.object(regress.common, "fixed_seeds", side_effect=lambda _m, options: options),
            mock.patch.object(regress.common, "base_options", return_value=types.SimpleNamespace()),
            mock.patch.object(regress, "scenarios", return_value={"case": {"target": "score"}}),
            redirect_stdout(io.StringIO()),
        ):
            regress.run(output)
        self.assertEqual(target.read_text(encoding="utf-8"), "do not overwrite")
        self.assertFalse(output.is_symlink())
        self.assertEqual(__import__("json").loads(output.read_text(encoding="utf-8"))["results"], {"case": []})

    def test_atomic_writer_cleans_up_when_publish_fails(self):
        output = self.root / "results.json"
        output.write_text("original", encoding="utf-8")
        with (
            mock.patch.object(regress.os, "replace", side_effect=OSError("publish failed")),
            self.assertRaisesRegex(OSError, "publish failed"),
        ):
            regress.write_json_atomically(output, {"result": "new"})
        self.assertEqual(output.read_text(encoding="utf-8"), "original")
        self.assertEqual(list(self.root.glob(".results.json.*.tmp")), [])

    def test_compare_covers_exact_sanity_and_mismatch(self):
        baseline = self.root / "baseline.json"
        after = self.root / "after.json"
        baseline.write_text(
            '{"results":{"same":[{"score":10}],"ga-challenge":[{"score":100}],"changed":[{"score":1,"cards":[1]}]}}',
            encoding="utf-8",
        )
        after.write_text(
            '{"results":{"same":[{"score":10}],"ga-challenge":[{"score":99}],"changed":[{"score":2,"cards":[2]}]}}',
            encoding="utf-8",
        )
        with redirect_stdout(io.StringIO()) as stdout:
            result = regress.compare(baseline, after)
        self.assertEqual(result, 1)
        self.assertIn("EXACT-MATCH", stdout.getvalue())
        self.assertIn("SANITY-OK", stdout.getvalue())
        self.assertIn("MISMATCH", stdout.getvalue())

    def test_main_dispatches_commands_and_reports_bad_paths(self):
        output = self.root / "output.json"
        baseline = self.root / "baseline.json"
        after = self.root / "after.json"
        baseline.write_text('{"results":{}}', encoding="utf-8")
        after.write_text('{"results":{}}', encoding="utf-8")
        with mock.patch.object(regress, "run") as run:
            self.assertEqual(regress.main(["run", str(output), "ga-"]), 0)
        run.assert_called_once_with(output, "ga-")

        with mock.patch.object(regress, "compare", return_value=0) as compare:
            self.assertEqual(regress.main(["compare", str(baseline), str(after)]), 0)
        compare.assert_called_once_with(baseline, after)

        with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            regress.main(["run", str(Path(tempfile.gettempdir()) / "outside.json")])


class RlQualityTests(unittest.TestCase):
    def test_bounded_count(self):
        self.assertEqual(rl_quality.bounded_count("1"), 1)
        self.assertEqual(rl_quality.bounded_count("100"), 100)
        for value in ("0", "101"):
            with self.assertRaises(argparse.ArgumentTypeError):
                rl_quality.bounded_count(value)

    def test_top_value(self):
        deck = types.SimpleNamespace(
            event_bonus_rate=1.11111,
            support_deck_bonus_rate=2.22222,
            multi_live_score_up=3.33333,
            score=42,
        )
        self.assertEqual(rl_quality.top_value({"target": "bonus"}, deck), 3.3333)
        self.assertEqual(rl_quality.top_value({"target": "skill"}, deck), 3.3333)
        self.assertEqual(rl_quality.top_value({"target": "score"}, deck), 42)

    def test_scenarios_include_world_bloom_when_available(self):
        with mock.patch.object(rl_quality.common, "detect_events", return_value=(1, 2, 3)):
            cases = rl_quality.scenarios()
        self.assertIn("score-wl", cases)
        self.assertIn("bonus-wl", cases)

    def test_main_dispatches_in_process(self):
        with (
            mock.patch.object(rl_quality, "run_calls") as run_calls,
            mock.patch.dict(os.environ, {"DECK_RL_SEED_CACHE_DISABLE": "1"}),
        ):
            self.assertEqual(rl_quality.main(["--calls", "2"]), 0)
        run_calls.assert_called_once_with(2, False)

    def test_main_dispatches_fresh_processes(self):
        with mock.patch.object(rl_quality, "run_in_fresh_process") as run_worker:
            self.assertEqual(
                rl_quality.main(["--ref", "--calls", "2", "--processes", "3"]),
                0,
            )
        self.assertEqual(run_worker.call_count, 3)
        run_worker.assert_called_with(2, True)

    def test_main_warns_without_cache_disable(self):
        with (
            mock.patch.object(rl_quality, "run_calls"),
            mock.patch.dict(os.environ, {}, clear=True),
            redirect_stderr(io.StringIO()) as stderr,
        ):
            rl_quality.main([])
        self.assertIn("DECK_RL_SEED_CACHE_DISABLE=1 not set", stderr.getvalue())

    def test_fresh_process_success_and_failure(self):
        process = mock.Mock(exitcode=0)
        context = mock.Mock()
        context.Process.return_value = process
        with mock.patch.object(rl_quality.multiprocessing, "get_context", return_value=context):
            rl_quality.run_in_fresh_process(2, True)
        process.start.assert_called_once_with()
        process.join.assert_called_once_with()

        process.exitcode = 9
        with (
            mock.patch.object(rl_quality.multiprocessing, "get_context", return_value=context),
            self.assertRaisesRegex(RuntimeError, "status 9"),
        ):
            rl_quality.run_in_fresh_process(2, False)

    def test_run_calls_reports_each_target(self):
        deck = types.SimpleNamespace(
            event_bonus_rate=1.0,
            support_deck_bonus_rate=2.0,
            multi_live_score_up=3.0,
            score=4,
        )
        engine = mock.Mock()
        engine.recommend.return_value = types.SimpleNamespace(decks=[deck])
        module = types.SimpleNamespace()
        with (
            mock.patch.object(rl_quality.common, "make_engine", return_value=(module, engine, {})),
            mock.patch.object(rl_quality.common, "base_options", return_value=types.SimpleNamespace()),
            mock.patch.object(
                rl_quality,
                "scenarios",
                return_value={"case": {"target": "score"}},
            ),
            redirect_stdout(io.StringIO()) as stdout,
        ):
            rl_quality.run_calls(1, False)
        self.assertIn("case call=0 val=4", stdout.getvalue())


if __name__ == "__main__":
    unittest.main()
