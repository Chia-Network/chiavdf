# Chia VDF

![Build](https://github.com/Chia-Network/chiavdf/actions/workflows/build.yml/badge.svg)
![PyPI](https://img.shields.io/pypi/v/chiavdf?logo=pypi)
![PyPI - Format](https://img.shields.io/pypi/format/chiavdf?logo=pypi)
![GitHub](https://img.shields.io/github/license/Chia-Network/chiavdf?logo=Github)

[![CodeQL](https://github.com/Chia-Network/chiavdf/actions/workflows/codeql-analysis.yml/badge.svg)](https://github.com/Chia-Network/chiavdf/actions/workflows/codeql-analysis.yml)

## What this repository provides

chiavdf implements the Verifiable Delay Function used by Chia's timelord. It
builds on Linux (x86_64 and arm64), macOS (Intel and Apple Silicon), and
Windows (x64), and produces:

- `vdf_client` - the CPU software timelord client.
- `vdf_bench` - a CPU benchmark for estimating iterations per second.
- `hw_vdf_client` and `hw_test` - the Chia VDF ASIC client and board test.
- `emu_hw_vdf_client` and `emu_hw_test` - hardware-free emulators for CI and
  development.
- The `chiavdf` Python bindings, plus optional `chiavdfc` shared/static
  libraries.

CI builds the CPU and ASIC binaries on all of those platforms. The ASIC
emulator tests run without a board.

## Run a timelord

A timelord needs a synced Chia full node and one or more VDF clients connected
to the timelord's port 8000. Choose the CPU or ASIC path below.

### Software timelord (CPU)

Use `vdf_client`. The normal installation path is chia-blockchain's
[install-timelord.sh](https://github.com/Chia-Network/chia-blockchain/blob/main/install-timelord.sh),
which installs the chiavdf package and builds `vdf_client` from source. Then
start the full node, timelord, and launcher:

```bash
chia start node timelord
```

The launcher starts the software `vdf_client` processes. For full node and
timelord setup, see the
[Chia timelord install guide](https://docs.chia.net/reference-client/install-and-setup/timelord-install/).
To build and test the client directly from this repository, see
[Building Timelord and related binaries](https://github.com/Chia-Network/chiavdf#building-timelord-and-related-binaries).

### Hardware timelord (Chia VDF ASIC)

Use `hw_vdf_client` with a Chia VDF ASIC board. Hardware support is available
on Linux x86_64 and arm64, macOS Intel and Apple Silicon, and Windows x64.
chia-blockchain does not start the hardware client, so start the node and
timelord without the software launcher, then start the client yourself:

```bash
chia start node timelord-only
hw_vdf_client 8000 3
```

See
[README_ASIC.md](https://github.com/Chia-Network/chiavdf/blob/main/README_ASIC.md)
for board and FTDI requirements, packaged and source builds, `--device-loc`
when using multiple boards, timelord allowlisting, service setup, and
troubleshooting. `emu_hw_vdf_client` and `emu_hw_test` let you exercise the
hardware paths without a board.

## Building the Python wheel

Building the Python bindings requires CMake, a C++17 compiler, and GMP
(MPIR on Windows). Building the timelord client and tests also requires Boost
headers.

```bash
python3 -m venv venv
source venv/bin/activate

pip install wheel setuptools_scm pybind11
BUILD_VDF_CLIENT=N pip wheel .
```

`BUILD_VDF_CLIENT=N` builds only the Python extension. The next section
builds the CPU timelord client and related binaries.

The Python wheel build uses GitHub Actions to build binary wheels for Linux
(x86_64 and arm64), macOS (Intel and Apple Silicon), and Windows (x64), and
publishes them, plus a source distribution, to PyPI. See
`.github/workflows/build.yml`. CMake uses the installed pybind11 package when
available and falls back to
[FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html)
to download [pybind11](https://github.com/pybind/pybind11).
[cibuildwheel](https://github.com/joerick/cibuildwheel) manages the wheel
builds. Install a published wheel with `pip install chiavdf`.

## Building Timelord and related binaries

The same source tree also builds the CPU timelord client and benchmark on
Linux, macOS, and Windows. `vdf_client` is the CPU process that computes the
Proof of Time requested by the timelord. `vdf_bench` estimates iterations per
second on a given CPU. Try `./vdf_bench square_asm 250000` on x86/x64 for the
phased/asm pipeline. On non-x86 architectures, use
`./vdf_bench square 250000` (NUDUPL).

Set `CHIAVDF_LOG_AVX=1` to emit AVX feature detection logs during startup.

For the ASIC client, `hw_vdf_client`, see
[README_ASIC.md](https://github.com/Chia-Network/chiavdf/blob/main/README_ASIC.md).

For direct CMake builds, the following options are available:

- `BUILD_VDF_CLIENT` - build `vdf_client`
- `BUILD_VDF_BENCH` - build `vdf_bench`
- `BUILD_VDF_TESTS` - build test binaries (`1weso_test`, `2weso_test`, `prover_test`) and CTest/GoogleTest targets (for example `vdf_client_session_test`)
- `BUILD_HW_TOOLS` - build the ASIC timelord tools (`hw_vdf_client`, `hw_test`,
  emulators, and `hw_proof_wait_test`)
- `ENABLE_GNU_ASM` - enable GNU-style asm pipeline on x86/x64 (enabled by default)
- `GENERATE_ASM_TRACKING_DATA` - enable `track_asm()` instrumentation in generated asm (off by default to avoid hot-loop overhead)

Example:

```bash
cmake -S src -B build \
  -DBUILD_PYTHON=OFF \
  -DBUILD_CHIAVDFC=OFF \
  -DBUILD_VDF_CLIENT=ON \
  -DBUILD_VDF_BENCH=ON \
  -DBUILD_VDF_TESTS=ON
cmake --build build --target vdf_client vdf_bench 1weso_test 2weso_test prover_test vdf_client_session_test
```

For the ASIC tools, add `-DBUILD_HW_TOOLS=ON` and follow
[README_ASIC.md](https://github.com/Chia-Network/chiavdf/blob/main/README_ASIC.md)
for FTDI libraries and the Windows clang-cl/Ninja setup used by CI.

The legacy `setup.py` + `Makefile.vdf-client` flow runs `make` for native
binaries when either `BUILD_VDF_CLIENT=Y` (the default) or
`BUILD_VDF_BENCH=Y` is set. Set both to `N` to skip native binaries. The
Makefile's default target builds `vdf_client`, `vdf_bench`, and the integration
test binaries; for selective builds, call
`make -f Makefile.vdf-client <target>` directly.

For direct CMake builds, use `-DBUILD_*` flags instead.

AVX runtime flags:

- `CHIAVDF_LOG_AVX=1`: emit AVX detection logs at startup
- `CHIA_DISABLE_AVX2=1`: disable AVX2 path even when supported
- `CHIA_FORCE_AVX2=1`: force AVX2 path
- `CHIA_DISABLE_AVX512_IFMA=1`: disable AVX-512 IFMA path
- `CHIA_ENABLE_AVX512_IFMA=1`: enable AVX-512 IFMA path when CPUID support is present
- `CHIA_FORCE_AVX512_IFMA=1`: force AVX-512 IFMA path

The chia-blockchain
[install-timelord.sh](https://github.com/Chia-Network/chia-blockchain/blob/main/install-timelord.sh)
script automates the software timelord installation and builds the
`vdf_client` binary from the chiavdf package.

The following tests are available, depending on which type of timelord you
are running:

- `./1weso_test` for `sanitizer_mode`.
- `./2weso_test` for a timelord that extends the chain with the slow algorithm.
- `./prover_test` for a timelord that extends the chain with the fast algorithm.

Those tests simulate `vdf_client` and verify that the produced proofs are
correct.

Note: `./prover_test` defaults to a long soak/stress run. Set
`CHIAVDF_PROVER_TEST_FAST=1` to run a short, CI-friendly correctness check.

Regression tests for specific bugs use GoogleTest and run via CTest. Example:

```bash
cmake -S src -B build \
  -DBUILD_PYTHON=OFF \
  -DBUILD_CHIAVDFC=OFF \
  -DBUILD_VDF_CLIENT=OFF \
  -DBUILD_VDF_BENCH=OFF \
  -DBUILD_VDF_TESTS=ON \
  -DBUILD_HW_TOOLS=OFF
cmake --build build --target vdf_client_session_test
ctest --test-dir build --output-on-failure -R '^regression\.'
```

### Testing matrix

- Binary integration tests: `1weso_test`, `2weso_test`, `prover_test`; these
  simulate `vdf_client` and validate proof correctness.
- Regression tests: GoogleTest targets executed via CTest (for example
  `vdf_client_session_test`, typically filtered with `ctest -R '^regression\.'`).
- Hardware tests: standalone binaries such as `hw_test`, `emu_hw_test`, and
  `hw_proof_wait_test`; build and run instructions are in `README_ASIC.md`.

## Fuzzing

Fuzz targets live under `rust_bindings/fuzz`. The `prove` target includes an
iteration cap to avoid out-of-memory conditions in CI. If you want deeper
iteration coverage, raise the cap in `rust_bindings/fuzz/fuzz_targets/prove.rs`
after validating memory usage and exec/s on your runner.

## Contributing and workflow

Contributions are welcome. More details are available in chia-blockchain's
[CONTRIBUTING.md](https://github.com/Chia-Network/chia-blockchain/blob/main/CONTRIBUTING.md).

`main` is the default branch. Tagged releases are published to PyPI and
consumed by chia-blockchain, so `main` can be ahead of the version
chia-blockchain currently pins. Branch from `main`, open a pull request against
`main`, and make sure the CI build and CodeQL analysis pass. Merging requires a
completed review.

## Historical background

The original VDF competition background, attribution, and implementation notes
are in
[README_BACKGROUND_COMPETITION.md](https://github.com/Chia-Network/chiavdf/blob/main/README_BACKGROUND_COMPETITION.md).
