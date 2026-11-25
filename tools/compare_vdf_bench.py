#!/usr/bin/env python3
"""Run the three vdf_bench frontends sequentially and write a GitHub summary."""

import argparse
import json
from pathlib import Path
import re
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FRONTENDS = ("C++", "Rust", "Python")
BACKENDS = {"C++": "MPIR", "Rust": "GMP (MinGW)", "Python": "MPIR"}


def elapsed_ms(stdout):
    matches = re.findall(r"^Time: (\d+) ms;", stdout, re.MULTILINE)
    if len(matches) != 1 or int(matches[0]) <= 0:
        raise RuntimeError(f"Expected one positive benchmark timing, got: {stdout!r}")
    return int(matches[0])


def summary(samples, counts, rounds, metadata, complete):
    lines = [
        "## Windows VDF benchmark comparison",
        "",
        f"**{'Complete' if complete else 'INCOMPLETE — see failed step'}**. "
        f"{rounds} measured rounds after one unmeasured warm-up per frontend/workload.",
        "All samples run sequentially on the same runner. Frontend order rotates each round; "
        "workload order alternates. Timings come from the benchmarks, excluding process startup.",
        "",
        "These are the Windows build paths: C++ and Python use pinned MPIR; Rust uses "
        "the patched MinGW GMP archive. Differences include the arithmetic library, not just binding overhead.",
        "",
    ]
    for name, value in metadata.items():
        value = str(value).replace("\n", " ").replace("|", "\\|")
        lines.append(f"- **{name}:** {value}")
    lines.append("")
    for workload, count in counts.items():
        unit = "ms/discriminant" if workload == "discr" else "ms/proof"
        divisor = count if workload == "discr" else 1
        lines += [
            f"### {workload} {count} — 1024-bit discriminant",
            "",
            f"| Frontend | Library | Samples | Mean {unit} ↓ | Std dev | Median | Min–max | Time / C++ |",
            "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
        cpp = [s["milliseconds"] for s in samples if s["frontend"] == "C++" and s["workload"] == workload]
        for frontend in FRONTENDS:
            times = [s["milliseconds"] for s in samples if s["frontend"] == frontend and s["workload"] == workload]
            if not times:
                lines.append(f"| {frontend} | {BACKENDS[frontend]} | 0 | — | — | — | — | — |")
                continue
            mean = statistics.mean(times)
            deviation = statistics.stdev(times) if len(times) > 1 else 0
            ratio = f"{mean / statistics.mean(cpp):.2f}×" if cpp else "—"
            lines.append(
                f"| {frontend} | {BACKENDS[frontend]} | {len(times)} | {mean / divisor:.3f} | "
                f"{deviation / divisor:.3f} | {statistics.median(times) / divisor:.3f} | "
                f"{min(times) / divisor:.3f}–{max(times) / divisor:.3f} | {ratio} |"
            )
        lines.append("")
    lines += [
        "<details><summary>Individual samples in execution order</summary>",
        "",
        "| Round | Workload | Frontend | Elapsed ms |",
        "| ---: | --- | --- | ---: |",
    ]
    for sample in samples:
        lines.append(
            f"| {sample['round']} | {sample['workload']} | {sample['frontend']} | {sample['milliseconds']} |"
        )
    lines += ["", "</details>", "", "Raw timings, commands, and output are available in the `windows-vdf-benchmarks` artifact.", ""]
    return "\n".join(lines)


def compare(commands, counts, rounds, output_dir, metadata):
    output_dir.mkdir(parents=True, exist_ok=True)
    samples = []
    runs = []
    complete = False

    def run(frontend, workload, round_number):
        command = commands[frontend] + [workload, str(counts[workload])]
        label = "warm-up" if round_number == 0 else f"round {round_number}"
        print(f"{label}: {frontend} {workload} {counts[workload]}", flush=True)
        result = subprocess.run(command, capture_output=True, text=True, timeout=300)
        runs.append({"round": round_number, "frontend": frontend, "workload": workload,
                     "command": command, "returncode": result.returncode,
                     "stdout": result.stdout, "stderr": result.stderr})
        print(result.stdout, end="", flush=True)
        if result.stderr:
            print(result.stderr, end="", file=sys.stderr, flush=True)
        if result.returncode:
            raise RuntimeError(f"{frontend} {workload} failed with exit code {result.returncode}")
        milliseconds = elapsed_ms(result.stdout)
        if round_number:
            samples.append({"round": round_number, "frontend": frontend, "workload": workload,
                            "count": counts[workload], "milliseconds": milliseconds})

    try:
        for workload in counts:
            for frontend in FRONTENDS:
                run(frontend, workload, 0)
        for round_number in range(1, rounds + 1):
            offset = (round_number - 1) % len(FRONTENDS)
            order = FRONTENDS[offset:] + FRONTENDS[:offset]
            workloads = list(counts) if round_number % 2 else list(reversed(counts))
            for workload in workloads:
                for frontend in order:
                    run(frontend, workload, round_number)
        complete = True
    finally:
        data = {"complete": complete, "rounds": rounds, "counts": counts,
                "metadata": metadata, "samples": samples, "runs": runs}
        (output_dir / "results.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        (output_dir / "summary.md").write_text(summary(samples, counts, rounds, metadata, complete), encoding="utf-8")


def positive_int(value):
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpp", required=True, type=Path)
    parser.add_argument("--rust", required=True, type=Path)
    parser.add_argument("--rounds", type=positive_int, default=6)
    parser.add_argument("--discriminants", type=positive_int, default=500)
    parser.add_argument("--iterations", type=positive_int, default=100000)
    parser.add_argument("--output-dir", type=Path, default=Path("benchmark-results"))
    parser.add_argument("--metadata", type=Path, required=True)
    args = parser.parse_args()
    commands = {"C++": [str(args.cpp.resolve())], "Rust": [str(args.rust.resolve())],
                "Python": [sys.executable, str(ROOT / "tools/vdf_bench.py")]}
    metadata = json.loads(args.metadata.read_text(encoding="utf-8-sig"))
    compare(commands, {"discr": args.discriminants, "prove": args.iterations},
            args.rounds, args.output_dir, metadata)


if __name__ == "__main__":
    main()
