#!/usr/bin/env python3
"""Benchmark discriminant creation and proof generation through chiavdf's Python API."""

import argparse
import sys
import time

from chiavdf import create_discriminant, prove

DISCRIMINANT_SIZE_BITS = 1024
MAX_U64 = (1 << 64) - 1


def positive_u64(value: str) -> int:
    try:
        number = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be an integer") from error
    if not 1 <= number <= MAX_U64:
        raise argparse.ArgumentTypeError("must be in the range 1..=2^64-1")
    return number


def benchmark_discriminants(iterations: int) -> None:
    seed = bytearray(range(32))
    start = time.perf_counter()
    for i in range(iterations):
        index = i % len(seed)
        seed[index] = (seed[index] + 1) & 0xFF
        discriminant = create_discriminant(bytes(seed), DISCRIMINANT_SIZE_BITS)
        if not discriminant:
            raise RuntimeError("create_discriminant failed")

    duration_ms = int((time.perf_counter() - start) * 1000)
    if duration_ms == 0:
        print("WARNING: too few iterations, results will be inaccurate!")
        duration_ms = 1

    tenths_ms_per_discriminant = duration_ms * 10 // iterations
    print(
        f"Time: {duration_ms} ms; speed: "
        f"{tenths_ms_per_discriminant // 10}."
        f"{tenths_ms_per_discriminant % 10} ms/discr"
    )


def benchmark_proof(iterations: int) -> None:
    challenge = bytes((0, 0, 1, 2, 3, 3, 4, 4))
    x_s = b"\x08" + bytes(99)

    start = time.perf_counter()
    proof = prove(
        challenge,
        x_s,
        DISCRIMINANT_SIZE_BITS,
        iterations,
        "",
    )
    duration_ms = int((time.perf_counter() - start) * 1000)
    if not proof:
        raise RuntimeError("prove failed")
    if duration_ms == 0:
        print("WARNING: too few iterations, results will be inaccurate!")
        duration_ms = 1

    if iterations > MAX_U64 // 10:
        raise OverflowError("iteration count overflows speed calculation")
    speed_tenths_k_ips = iterations * 10 // duration_ms
    print(
        f"Time: {duration_ms} ms; speed: "
        f"{speed_tenths_k_ips // 10}.{speed_tenths_k_ips % 10}K ips"
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        prog="vdf_bench",
        description="Benchmark the chiavdf Python bindings",
    )
    commands = parser.add_subparsers(dest="command", required=True)
    discr = commands.add_parser("discr", help="benchmark N discriminant creations")
    discr.add_argument("iterations", type=positive_u64)
    proof = commands.add_parser(
        "prove", help="benchmark proof generation for N VDF iterations"
    )
    proof.add_argument("iterations", type=positive_u64)
    args = parser.parse_args()

    try:
        if args.command == "discr":
            benchmark_discriminants(args.iterations)
        else:
            benchmark_proof(args.iterations)
    except (OverflowError, RuntimeError) as error:
        print(f"vdf_bench: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
