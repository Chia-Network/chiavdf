"""Exercise benchmark scheduling, reporting, and failure preservation without native builds."""

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "compare_vdf_bench", Path(__file__).resolve().parents[1] / "tools/compare_vdf_bench.py"
)
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)


class CompareBenchmarks(unittest.TestCase):
    def test_interleaving_balances_positions_and_excludes_warmups(self):
        commands = {name: [name] for name in bench.FRONTENDS}
        calls = []

        def execute(command, **kwargs):
            calls.append(command)
            duration = 9000 if len(calls) <= 6 else {"C++": 1000, "Rust": 2000, "Python": 3000}[command[0]]
            return subprocess.CompletedProcess(command, 0, f"Time: {duration} ms; speed: ignored\n", "")

        with tempfile.TemporaryDirectory() as tmp, patch.object(bench.subprocess, "run", side_effect=execute):
            path = Path(tmp)
            with contextlib.redirect_stdout(io.StringIO()):
                bench.compare(commands, {"discr": 500, "prove": 100000}, 6, path, {"CPU": "test"})
            data = json.loads((path / "results.json").read_text())
            report = (path / "summary.md").read_text()
        self.assertTrue(data["complete"])
        self.assertEqual(len(calls), 42)
        self.assertEqual(len(data["samples"]), 36)
        self.assertNotIn(9000, [s["milliseconds"] for s in data["samples"]])
        orders = []
        for round_number in range(1, 7):
            samples = [s for s in data["samples"] if s["round"] == round_number]
            self.assertEqual([s["workload"] for s in samples[:3]], ["discr" if round_number % 2 else "prove"] * 3)
            order = [s["frontend"] for s in samples[:3]]
            self.assertEqual(order, [s["frontend"] for s in samples[3:]])
            orders.append(order)
        for position in range(3):
            for frontend in bench.FRONTENDS:
                self.assertEqual(sum(order[position] == frontend for order in orders), 2)
        self.assertIn("| Rust | GMP (MinGW) | 6 | 4.000 |", report)
        self.assertIn("| Python | MPIR | 6 | 3000.000 |", report)
        self.assertIn("Time / C++", report)

    def test_failed_command_keeps_output_and_partial_results(self):
        calls = []

        def execute(command, **kwargs):
            calls.append(command)
            if len(calls) == 8:
                return subprocess.CompletedProcess(command, 7, "", "benchmark failed\n")
            return subprocess.CompletedProcess(command, 0, "Time: 100 ms; speed: ignored\n", "")

        with tempfile.TemporaryDirectory() as tmp, patch.object(bench.subprocess, "run", side_effect=execute):
            path = Path(tmp)
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaisesRegex(RuntimeError, "Rust discr failed with exit code 7"):
                    bench.compare({name: [name] for name in bench.FRONTENDS},
                                  {"discr": 50, "prove": 100000}, 6, path, {})
            data = json.loads((path / "results.json").read_text())
            self.assertFalse(data["complete"])
            self.assertEqual(len(data["samples"]), 1)
            self.assertEqual(data["runs"][-1]["stderr"], "benchmark failed\n")
            self.assertIn("INCOMPLETE", (path / "summary.md").read_text())

    def test_missing_zero_and_ambiguous_timings_fail(self):
        for output in ("", "Time: 0 ms; speed: 0\n", "Time: 1 ms;\nTime: 2 ms;\n"):
            with self.subTest(output=output), self.assertRaises(RuntimeError):
                bench.elapsed_ms(output)
        self.assertEqual(bench.elapsed_ms("Time: 123 ms; speed: 5.0K ips\n"), 123)


if __name__ == "__main__":
    unittest.main()
