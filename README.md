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
- The `chiavdf` Python bindings used by chia-blockchain to validate proofs of
  time, plus optional `chiavdfc` shared/static libraries.

CI builds the CPU and ASIC binaries on all of those platforms. The ASIC
emulator tests run without a board.

## Install a published build

Release binaries are available through Homebrew, winget, and apt. These
channels install the same binaries as the GitHub release archives.

### macOS

Install through the [Chia Homebrew tap](https://github.com/Chia-Network/homebrew-chia):

```bash
brew install chia-network/chia/chiavdf
```

This installs `vdf_client`, `vdf_bench`, `hw_vdf_client`, `hw_test`, and the
emulators. The bundled FTDI libraries are installed under `libexec/chiavdf`.
Install the FTDI D2XX driver before using the ASIC tools with a board; see
[README_ASIC.md](README_ASIC.md).

### Windows

Install through winget:

```powershell
winget install --id Chia.chiavdf --exact
```

The winget package is a machine-scope Inno Setup installer. It installs all
six tools (`vdf_client`, `vdf_bench`, `hw_vdf_client`, `hw_test`,
`emu_hw_vdf_client`, and `emu_hw_test`) under
`%ProgramFiles%\Chia\chiavdf\bin`, adds that directory to the system `PATH`,
and stages the FTDI CDM driver (`ftdibus.inf` and `ftdiport.inf`) with
`pnputil`. One install therefore covers software timelords and ASIC hosts.
The installer requires x64 Windows 10 version 2004 or newer and will show a
UAC prompt. If you cannot elevate, use the portable
`chiavdf-<version>-windows-amd64.zip` from a
[release](https://github.com/Chia-Network/chiavdf/releases) instead. The
shared FTDI driver is left installed when chiavdf is uninstalled. Open a new
terminal after installation so the updated `PATH` is visible. See
[README_ASIC.md](README_ASIC.md) and `assets/windows-installer.md`.

### Linux

Add the Chia apt repository and install `chiavdf`:

```bash
sudo apt-get update
sudo apt-get install ca-certificates curl gnupg
curl -sL https://repo.chia.net/FD39E6D3.pubkey.asc | sudo gpg --dearmor -o /usr/share/keyrings/chia.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/chia.gpg] https://repo.chia.net/chiavdf/debian/ stable main" | sudo tee /etc/apt/sources.list.d/chiavdf.list
sudo apt-get update
sudo apt-get install chiavdf
```

The package installs `vdf_client`, `vdf_bench`, `hw_vdf_client`, `hw_test`,
and the emulators under `/usr/bin`, plus `libft4222.so` and the udev rule for
the ASIC board.

The Python bindings are installed separately with `pip install chiavdf`; see
[Building the Python wheel](#building-the-python-wheel).

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

The published `chiavdf` wheels contain the native verifier bindings that
chia-blockchain uses to validate proofs of time. This lets a node install a
prebuilt wheel instead of compiling the C++ library itself.

The Python bindings require Python 3.11 or newer. Building them requires
CMake, a C++17 compiler, and GMP (MPIR on Windows). Building the timelord
client and tests also requires Boost headers.

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

### Blockchain proof corpus

`verify_vdf_corpus` is a standalone Unix tool that validates binary VDF proof corpora
independently of a blockchain database. Supply one or more files on the command
line; there is no default sample or environment-variable configuration. The
existing `ReferenceGoldenRegressionTest` retains the arithmetic regression coverage.

Export more proofs with `export-vdf-proofs` in the `chia_rs` repository:

```bash
cargo run --release -p chia-tools --bin export-vdf-proofs -- \
  /path/to/blockchain.sqlite mainnet-proofs.bin
```

The tool opens the database read-only, includes orphan blocks, reports progress, and
resolves partial-proof inputs using ancestor records. It refuses to overwrite
an existing output file. If extraction fails, discard the partial output.

The file is a sequence of independently serialized Streamable records. There is
no file header or record count. The Rust `CorpusEntry` layout is:

```rust
struct CorpusEntry {
    height: u32,
    input: ClassgroupElement,
    info: VDFInfo,
    proof: VDFProof,
}
```

Streamable integers are big-endian. The 100 raw bytes of `input` follow the
four-byte `height` immediately. The discriminant size is a fixed 1024 bits in
the verifier and is not stored in the corpus. Witness bytes have a `u32` byte
length prefix. `VDFInfo` contains the
32-byte challenge, `u64` iteration count, and output element. `VDFProof` contains
the `u8` witness type, length-prefixed witness, and one-byte normalized flag.
`input` and `info.number_of_iterations` are the resolved verifier arguments,
including adjustments for partial proofs. The C++ reader does not reapply the
normalized flag. Block hashes and proof field names are omitted; failures
identify the file, height and record number.

Exports can be combined with
`cat mainnet-proofs.bin testnet11-proofs.bin testneta-proofs.bin > proofs.bin`.
The corpus omits version, network and proof field metadata. No compression is
required.

Build and run from the chiavdf repository (GoogleTest is not required):

```bash
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_PYTHON=OFF -DBUILD_VDF_TESTS=OFF
cmake --build build --config Release --target verify_vdf_corpus -j 4
./build/verify_vdf_corpus mainnet-proofs.bin testnet11-proofs.bin testneta-proofs.bin
```

For multi-configuration generators such as Xcode, run the executable from
`build/Release/` instead.

Files are processed in argument order. Verification uses one worker per hardware
thread reported by `std::thread::hardware_concurrency()` (with a fallback of one
worker), maps each corpus file and parses records as workers need them. Decoded
proof memory is bounded by the worker count.
Progress is printed every 10,000 proofs, followed by per-file and overall totals.
Failures include the filename, block height and record number. Empty, truncated,
unreadable or malformed corpora cause a nonzero
exit status. A malformed record stops that file, and the tool continues with
subsequent files. Invalid proofs are reported while checking the remaining records.
Exit status is 0 when every file passes, 1 on failure, or 2 if no files were supplied.
Use `--help` for usage. The tool checks VDF validity, not block consensus.

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
