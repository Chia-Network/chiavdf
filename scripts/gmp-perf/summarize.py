#!/usr/bin/env python3
"""Pivot RESULT|label|metric|value lines from gmp-perf reports into Markdown.

Labels look like os/gmp-variant/allocator[/compiler]. Repeated measurements
of the same (label, metric) keep the best value: lowest time, highest K ips.
"""
import sys
from pathlib import Path


def better(metric, a, b):
    return max(a, b) if "ips" in metric else min(a, b)


def main(paths):
    best, labels, metrics, cpus = {}, [], [], []
    for path in paths:
        for line in Path(path).read_text(errors="replace").splitlines():
            line = line.strip()
            if line.startswith("CPU:"):
                cpus.append(f"{Path(path).stem}: {line[4:].strip()}")
            if not line.startswith("RESULT|"):
                continue
            try:
                _, label, metric, value = line.split("|")
                value = float(value)
            except ValueError:
                continue
            if label not in labels:
                labels.append(label)
            if metric not in metrics:
                metrics.append(metric)
            key = (label, metric)
            best[key] = better(metric, best[key], value) if key in best else value

    out = ["## Machines", ""] + [f"- {c}" for c in cpus] + [""]

    vdf = [m for m in metrics if m.startswith("vdf ")]
    vdf_labels = [l for l in labels if any((l, m) in best for m in vdf)]
    if vdf_labels:
        out += ["## vdf_bench (best of runs)", ""]
        out += ["| build | " + " | ".join(vdf) + " |", "|---|" + "---:|" * len(vdf)]
        for l in vdf_labels:
            cells = [f"{best[(l, m)]:.2f}" if (l, m) in best else "-" for m in vdf]
            out.append(f"| {l} | " + " | ".join(cells) + " |")
        out.append("")

    micro = [m for m in metrics if m not in vdf]
    for os_name in sorted({l.split("/")[0] for l in labels}):
        cols = [l for l in labels if l.startswith(os_name + "/") and any((l, m) in best for m in micro)]
        if not cols:
            continue
        out += [f"## {os_name} mpz microbench (ns/op, best of 3)", ""]
        out += ["| op | " + " | ".join(c.split("/", 1)[1] for c in cols) + " |", "|---|" + "---:|" * len(cols)]
        for m in micro:
            cells = [f"{best[(c, m)]:.1f}" if (c, m) in best else "-" for c in cols]
            out.append(f"| {m.removesuffix(' (ns)')} | " + " | ".join(cells) + " |")
        out.append("")

    print("\n".join(out))


if __name__ == "__main__":
    main(sys.argv[1:])
