# ASIC hardware timelord

`hw_vdf_client` drives a Chia VDF ASIC over USB and feeds proofs to a Chia timelord. The board enumerates as an FTDI FT4222H. The client links FTDI's FT4222/D2XX libraries (`libft4222`, and on macOS and Windows also `ftd2xx`). Fetch those with `scripts/get-libft4222.sh` or `scripts/get-libft4222.ps1`.

chia-blockchain never starts `hw_vdf_client`. The timelord opens `timelord.vdf_server` (default port 8000) and accepts TCP clients listed in `timelord.vdf_clients.ip`. You start the node and timelord, then start this process yourself.

Operator install notes also live in the Chia docs:

- https://docs.chia.net/reference-client/install-and-setup/timelord-install/
- https://docs.chia.net/reference-client/cli-reference/asic-hwvdf-cli/

This file is the build and operations guide for the client in this repository. The website CLI table can lag the flags below.

## What you need

- A Chia VDF ASIC board. Each ASIC has three VDF engines. Each board has two USB-C ports: one for plain 5 V power only, and one FT4222H data link to the host. See [Engines, ports, and cooling](#engines-ports-and-cooling).
- A host running Linux x86_64, Linux aarch64/arm64, macOS (Apple Silicon or Intel), or 64-bit Windows. Those are the architectures the package workflow builds.
- A synced Chia full node on the timelord machine. The timelord reads peaks from that node.
- Proof work runs on the host CPU (`--vdf-threads`, `--proof-threads`). A board at the default clock still needs a host that can keep up with three engines.

Leave the clock and voltage at the defaults (1100 MHz, 0.88 V) unless you have a reason to change them. See [Clock and voltage](#clock-and-voltage).

## Engines, ports, and cooling

Each Chia VDF ASIC has three internal VDF engines (`N_HW_VDFS` is 3). `--vdfs-mask` enables them: bit 0 (value 1) is engine 0, bit 1 (value 2) is engine 1, and bit 2 (value 4) is engine 2. The default mask is 7, all three engines. `hw_vdf_client` opens one TCP connection to the timelord for each enabled engine, so `hw_vdf_client 8000 3` is the match for the timelord's three chains.

Three ASIC boards can supply one engine each (`hw_vdf_client 8000 1`, or a one-bit `--vdfs-mask`). Attach those boards to one, two, or three hosts. Use `--device-loc` to select a board when a host has more than one; see [Multiple boards and proof construction](#multiple-boards-and-proof-construction). Point each client at the same timelord with `--ip`, add each source address to `vdf_clients.ip`, and set `vdf_server.host` to `0.0.0.0`.

Each board has two USB-C ports. The power-only port takes plain 5 V USB-C power, standard USB-C default power, with no USB Power Delivery negotiation. Any ordinary USB-C port or 5 V USB-C supply can power it. The other port is the data port: the FT4222H USB link to the host. FTDI DS_FT4222H (document FT_001011) describes the FT4222H as a USB 2.0 Hi-Speed device (480 Mbit/s). Any USB 2.0 or faster data port works for that link. `FtdiDriver::Open()` programs the chip as a quad-SPI master (`SPI_IO_QUAD`) with system clock `SYS_CLK_80` and divider `CLK_DIV_8` (one eighth of the system clock; `src/libft4222/libft4222.h`). The same datasheet lists 80 MHz among the system clocks `FT4222_SetClock` can select, and lists 1/8 as an SCK divider. That is the clock this client programs. This tree does not record a measured throughput for the USB-C data port.

`hw_test` and `hw_vdf_client` log the ASIC rail at startup, before the job:

- `Board voltage is now %1.3f V` from `GetBoardVoltage()`, `(24375 + vid * 625) / 100000`. Each VID step is 6.25 mV.
- `Board current is %2.3f A` from `GetBoardCurrent()`, `(cs * 440 mV) / (8 * 4096)`.
- `Board power is %2.3f W` from `GetPower()`, which multiplies those two readings (`src/hw/vdf_driver.cpp`, logged in `src/hw/hw_interface.cpp`).

These lines are the ASIC rail. They are not the wattage drawn at the power-only USB-C port.

On an Apple Silicon Mac mini, `hw_test` at the default request of 1100 MHz and 0.88 V (`HW_VDF_DEF_FREQ`, `HW_VDF_DEF_VOLTAGE`) reported board voltage 0.875 V, current 0.590 A, and power 0.516 W. 0.875 V is the VID step for a 0.88 V request. 0.875 × 0.590 = 0.51625, which the `%2.3f` power log prints as 0.516.

On an Apple Silicon Mac mini powering the board from its own USB-C port, macOS showed no PD contract on any port (`IOAccessoryPowerCurrentLimits` was 0 everywhere) and did not register the power-only connection.

The boards are passively air cooled. The client writes 100 °C external and engine alarm thresholds into the PVT sensor and logs `ASIC Temp`.

## Install a published build

The easiest install path on each platform is the package manager: Homebrew on
macOS, winget on Windows, and apt on Debian and Ubuntu. Release archives are
also attached to each GitHub release.

### Linux (apt)

Add the Chia apt repository and install the package:

```bash
sudo apt-get update
sudo apt-get install ca-certificates curl gnupg
curl -sL https://repo.chia.net/FD39E6D3.pubkey.asc | sudo gpg --dearmor -o /usr/share/keyrings/chia.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/chia.gpg] https://repo.chia.net/chiavdf/debian/ stable main" | sudo tee /etc/apt/sources.list.d/chiavdf.list
sudo apt-get update
sudo apt-get install chiavdf
```

The package installs:

- `/usr/bin/vdf_client`, `vdf_bench`, `hw_vdf_client`, `hw_test`, `emu_hw_vdf_client`, `emu_hw_test`
- `/usr/lib/libft4222.so` (SONAME `libft4222.so`)
- `/etc/udev/rules.d/99-chiavdf.rules`

The package depends on `libgmp-dev` and `libboost-system-dev`. The apt index
publishes `amd64` and `arm64`; other architectures build from source. A
`.deb` built by this tree is `chiavdf_<version>-1_<arch>.deb` with
`Package: chiavdf`. The `chiavdf-hw` package in the
`https://repo.chia.net/chiavdf-hw/debian/` repository is an amd64-only ASIC
index that installs the same binaries.

The package workflow (`.github/workflows/build-packages.yml` calling
`build-client.yml`) runs `dpkg-deb` on that tree.

### macOS (Homebrew)

Install through the [Chia Homebrew tap](https://github.com/Chia-Network/homebrew-chia):

```bash
brew install chia-network/chia/chiavdf
```

This installs `hw_vdf_client`, `hw_test`, and the emulators alongside
`vdf_client` and `vdf_bench`. The bundled FTDI libraries are installed under
`libexec/chiavdf`. Install the FTDI D2XX driver from
https://ftdichip.com/products/ft4222h/ before connecting a board.

### Windows (winget)

```powershell
winget install --id Chia.chiavdf --exact
```

winget installs `vdf_client.exe` and puts `vdf_client` on `PATH`. For
`hw_vdf_client.exe`, `hw_test.exe`, and the emulators, download
`chiavdf-<version>-windows-amd64.zip` from a
[release](https://github.com/Chia-Network/chiavdf/releases) and run the
binaries from the extracted `bin\` directory. Install the FTDI D2XX driver
before using the ASIC tools.

### macOS release archive and package

A release of this repo uploads `chiavdf-<tag>-macos-arm64.zip` (Apple Silicon, `macos-15`), `chiavdf-<tag>-macos-intel.zip`, and a `.pkg` for each. The release job signs the binaries and the pkg, notarizes the pkg, and staples the ticket to it. The zip ships the same signed binaries, so Gatekeeper verifies it through the online notarization lookup; a zip cannot be stapled. Final releases require `APPLE_DEV_ID_APP`/`APPLE_DEV_ID_APP_PASS`, `APPLE_DEV_ID_INSTALLER`/`APPLE_DEV_ID_INSTALLER_PASS`, and `APPLE_NOTARIZE_USERNAME`/`APPLE_NOTARIZE_PASSWORD`/`APPLE_TEAM_ID`; the build fails before packaging if any are missing. Layout:

```text
bin/hw_vdf_client
bin/hw_test
bin/emu_hw_vdf_client
bin/emu_hw_test
bin/vdf_client
bin/vdf_bench
libexec/chiavdf/libft4222.dylib
libexec/chiavdf/libft4222.1.4.4.190.dylib
libexec/chiavdf/libftd2xx.dylib
libexec/chiavdf/libgmp.<version>.dylib
libexec/chiavdf/libgmpxx.<version>.dylib
```

Run `bin/hw_vdf_client`. The bundle's rpath is `@loader_path/../libexec/chiavdf`, so the FTDI and GMP dylibs resolve next to `bin/` with no `DYLD_LIBRARY_PATH`. Release staging rewrites the Homebrew GMP install names to `@rpath/<basename>` and copies those dylibs into `libexec/chiavdf`. The `.pkg` installs the same tree under `/usr/local` (`/usr/local/bin`, `/usr/local/libexec/chiavdf`). Details of signing are in `assets/macos-release-bundle.md`.

### Windows release zip

A release uploads `chiavdf-<version>-windows-amd64.zip` and a copy named `chiavdf-win64.zip` (the winget asset). Layout:

```text
bin\hw_vdf_client.exe
bin\hw_test.exe
bin\emu_hw_vdf_client.exe
bin\emu_hw_test.exe
bin\vdf_client.exe
bin\vdf_bench.exe
bin\LibFT4222-64.dll
bin\FTD2XX.dll
bin\mpir.dll
bin\<mpir cpu variants>
bin\<VC++ 2015-2022 runtime DLLs, such as VCRUNTIME140.dll and MSVCP140.dll>
```

Optimized Windows builds outside pull requests sign the six chiavdf executables and the MPIR DLLs with Azure Artifact Signing before the zip is uploaded or published; the FTDI and VC++ runtime DLLs keep their vendor signatures. Details are in `assets/windows-release-signing.md`.

The zip has a flat `bin\` and no `lib\`. The loader looks in the executable's directory, then System32, the Windows directory, the current directory, and `PATH`; it does not search a sibling `lib\`. Every non-system import of every shipped executable and DLL is in `bin\` under the exact name in its import table. That includes the MPIR CPU variants beside `mpir.dll` and the VC++ 2015-2022 runtime copied from the MSVC redist. Run the extracted binaries directly:

```powershell
.\bin\hw_vdf_client.exe --list
```

Install the FTDI D2XX driver before that. `scripts/get-libft4222.ps1` stages `LibFT4222-64.dll` plus the `ftd2xx.lib` import library, and fetches the native amd64 `FTD2XX.dll` from FTDI CDM 2.12.36.20. The kernel driver is the D2XX/CDM package from the FT4222H downloads page: https://ftdichip.com/products/ft4222h/

## Build from source

Versions pinned by the fetch scripts:

| Host | Script | Library |
| --- | --- | --- |
| Linux x86_64 and aarch64/arm64 | `scripts/get-libft4222.sh` | libft4222 1.4.4.170 from `download.chia.net` |
| macOS | same script | LibFT4222 1.4.4.190 plus `libftd2xx.dylib` |
| Windows | `scripts/get-libft4222.ps1` | LibFT4222 1.4.8 plus native `FTD2XX.dll` from CDM 2.12.36.20 |

`install` downloads and stages headers and libraries. `clean` removes them. Linux and macOS place a symlink at `src/hw/libft4222`. Windows copies headers, import libraries, and DLLs into `src\hw\libft4222`.

### Linux

Hardware targets need `build-essential` and `libgmp-dev`. The workflow also installs CMake, Boost, and Python headers because that job builds `vdf_client` and runs CMake tests. The hardware binaries below do not include Boost.

```bash
sudo apt-get update
sudo apt-get install build-essential libgmp-dev
./scripts/get-libft4222.sh install
cd src
make -f Makefile.vdf-client emu_hw_test hw_test emu_hw_vdf_client hw_vdf_client hw_proof_wait_test
```

`hw_proof_wait_test` links the emulator path and needs neither a board nor the FTDI shared library.

The software regression tests are separate from the hardware checks: they use CMake + CTest + GoogleTest (`regression_unit_tests`, `regression_io_tests`). The `hw_*` binaries above are standalone.

`make` compiles `lzcnt.c` with `$(CC)` and everything else with `$(CXX)`, then links with LTO (`-flto=auto` for GCC, `-flto` for Clang). Both must be the same compiler. `build-essential` supplies gcc and g++. If `cc` is Clang and `g++` is GCC, the link fails with `lzcnt.o: file format not recognized`. Run `make clean` first, then rebuild with `CC=gcc CXX=g++` so the old `lzcnt.o` is not reused.

The script selects the library directory from `uname -m`:

| `uname -m` | Directory | Run path from `src/` |
| --- | --- | --- |
| `x86_64` | `build-x86_64` | `LD_LIBRARY_PATH=hw/libft4222/build-x86_64` |
| `aarch64` or `arm64` | `build-arm-v8` | `LD_LIBRARY_PATH=hw/libft4222/build-arm-v8` |

Any other architecture stops the script and the Makefile. The binary's `NEEDED` entry is the SONAME `libft4222.so`, and the link adds no `RUNPATH`. `ldd hw_vdf_client` reports `libft4222.so => not found` until `LD_LIBRARY_PATH` includes the directory that holds the `libft4222.so` symlink.

The source tree ships no udev rule. The package installs this one, for the FT4222H id used by the workflow and by the published `chiavdf-hw` package (`0403:601c`):

```bash
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="0403", ATTRS{idProduct}=="601c", MODE="0666"' | sudo tee /etc/udev/rules.d/99-chiavdf.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Unplug and replug the board after that. `MODE="0666"` is what the package uses, so a non-root user can open the device.

### macOS

Install the Xcode command line tools (`install_name_tool` is required by the fetch script; `hdiutil` mounts the FTDI disk image) and the Homebrew packages the workflow installs:

```bash
xcode-select --install
brew install cmake gmp boost
./scripts/get-libft4222.sh install
cd src
make -f Makefile.vdf-client emu_hw_test hw_test emu_hw_vdf_client hw_vdf_client hw_proof_wait_test
codesign -f -s - hw/libft4222/*.dylib hw_vdf_client hw_test
```

The `codesign` line is required for a local source build. The FT4222 and D2XX dylibs from `get-libft4222.sh` are unsigned, and macOS kills a process that loads them. `hw_vdf_client` then exits 137 (SIGKILL) before `--list` or a timelord connection. Run the command from `src/` after every rebuild of `hw_vdf_client` or `hw_test`. It ad-hoc signs those two binaries and every dylib in `hw/libft4222/`.

`CHIAVDF_ADHOC_SIGN_FTDI=1 ./scripts/get-libft4222.sh install` signs the dylibs at download time and does not sign the binaries. It does not replace the `codesign` line above. Release zips are Developer ID signed, and their binaries are covered by the pkg's notarization ticket, so that line is for a tree you built yourself.

`Makefile.vdf-client` adds `-I` and `-L` for `/opt/homebrew` or `/usr/local` when `boost/asio.hpp` is visible there. Homebrew Boost is often keg-only. The workflow locates the header and passes it explicitly when it also builds `vdf_client`:

```bash
BOOST_VERSION="$(brew list --versions boost | awk '{print $2}')"
BOOST_INCLUDE=""
for cand in \
  "$(brew --prefix boost)/include" \
  "$(brew --prefix)/include" \
  "$(brew --cellar boost)/${BOOST_VERSION}/include"; do
  if [ -f "$cand/boost/asio.hpp" ]; then
    BOOST_INCLUDE="$cand"
    break
  fi
done
# still in src/; sign again after this rebuild
make CPPFLAGS="-I${BOOST_INCLUDE}" -f Makefile.vdf-client emu_hw_test hw_test emu_hw_vdf_client hw_vdf_client hw_proof_wait_test
codesign -f -s - hw/libft4222/*.dylib hw_vdf_client hw_test
```

The hardware link line is still `-lgmpxx -lgmp`. `Makefile.vdf-client` adds `-L/opt/homebrew/lib` (or `/usr/local/lib`) only when it finds `boost/asio.hpp` there. If GMP is installed elsewhere, add its `-L` path to the `LDFLAGS +=` line in `Makefile.vdf-client`; do not pass `LDFLAGS=...` on the `make` command line, because that replaces the Makefile's LTO, debug, and rpath flags.

`hw_vdf_client` and `hw_test` get rpath `@executable_path/hw/libft4222`. The rpath is relative to the executable, not the current directory; keep the binaries in `src/` (or copy the whole `src/hw/libft4222` tree alongside them). Copying only the executable leaves the loader without `libft4222.dylib` and `libftd2xx.dylib`.

The fetch script clears `com.apple.quarantine` and `com.apple.provenance` on the downloaded files. Clearing those attributes still leaves the dylibs unsigned. The `codesign -f -s -` step above is what allows `hw_vdf_client` to start.

`DYLD_LIBRARY_PATH` is stripped for SIP-protected launchers. The rpath above is how a source build finds the FTDI dylibs. This tree has no `tools/build_native_gmp.sh`; system GMP from Homebrew is what the Makefile links.

Keep the Mac awake while the client runs. Sleep drops the USB device.

```bash
caffeinate -is ./hw_vdf_client 8000 3
```

`pmset` can change system sleep settings if you want the machine itself to stay up when nobody is logged in. `caffeinate -is` covers the client process.

### Windows

The Windows workflow job configures CMake with Ninja and `clang-cl`. Run it from a shell that already has the x64 MSVC environment (the "x64 Native Tools" prompt, or `vcvars64.bat`), LLVM's `clang-cl` (`C:\Program Files\LLVM\bin`), and Ninja. `bash` here is Git Bash or any bash that can see `scripts/`.

```bat
bash scripts/get-mpir-gc-x64.sh
powershell -ExecutionPolicy Bypass -File scripts/get-libft4222.ps1 install
```

`get-mpir-gc-x64.sh` clones https://github.com/Chia-Network/mpir_gc_x64.git at the commit in `scripts/mpir_gc_x64.commit` into `mpir_gc_x64\`. The workflow installs the other tools with Chocolatey when they are missing: `choco install llvm ninja -y` and `choco install boost-msvc-14.3 -y` (headers under `C:\local\boost_*`).

Set `BOOST_INCLUDE_DIR` to that Boost directory, then configure and build the same targets the workflow builds:

```powershell
cmake -S src -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TRY_COMPILE_CONFIGURATION=Release `
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL `
  -DCMAKE_C_COMPILER=clang-cl `
  -DCMAKE_CXX_COMPILER=clang-cl `
  -DCMAKE_ASM_COMPILER=clang-cl `
  -DBOOST_INCLUDE_DIR=$env:BOOST_INCLUDE_DIR `
  -DBUILD_PYTHON=OFF `
  -DBUILD_CHIAVDFC=OFF `
  -DBUILD_VDF_CLIENT=ON `
  -DBUILD_VDF_BENCH=ON `
  -DBUILD_VDF_TESTS=ON `
  -DBUILD_HW_TOOLS=ON `
  -DENABLE_GNU_ASM=ON
cmake --build build --target vdf_client vdf_bench 1weso_test 2weso_test prover_test regression_unit_tests regression_io_tests hw_proof_wait_test emu_hw_test hw_test emu_hw_vdf_client hw_vdf_client
```

Binaries land in `build\`. The workflow's smoke test puts the build directory, `src\hw\libft4222`, and `mpir_gc_x64` on `PATH` before starting them:

```powershell
$env:PATH = "$PWD\build;$PWD\src\hw\libft4222;$PWD\mpir_gc_x64;$env:PATH"
.\build\emu_hw_vdf_client.exe --list
.\build\hw_vdf_client.exe --list
```

Install the FTDI D2XX driver as described under the release zip. The import libraries satisfy the link; the DLLs have to be loadable at run time.

## Check the board

From `src/` on Linux, prefix the hardware binaries with `LD_LIBRARY_PATH` as in the table above. On macOS the rpath covers a binary that is still in `src/`. On Windows use the source-build `PATH` from the previous section; a release zip needs no `PATH` change.

```bash
./hw_vdf_client --list
```

A board prints one line per FT4222H interface whose description ends in `A`:

```text
List of available devices:
Device 0: '<description>', loc <id>
```

`<description>` is the D2XX string for that interface. `<id>` is the location id. Pass that number to `--device-loc` (or as the third argument to `hw_test`) to select one board. With no FTDI device, this Linux host printed `FT_CreateDeviceInfoList(&num_devs) failed (error 18)` and exited 1. Error 18 is `FT_OTHER_ERROR`.

`hw_test` opens interface A at the default clock and voltage and runs a fixed job. The default location is 0, which selects the first available board. Defaults are 3 engines and 1,000,000 iterations. Optional arguments are `hw_test [n_vdfs] [n_iters] [device_loc]` with `n_vdfs` from 1 to 3. A count outside that range is treated as 2. `hw_test --version` prints the version and exits.

```bash
./hw_test 1 1000000 0
```

A working engine logs `VDF <n>: <iters> HW iters done in <seconds>s, HW speed: <ips> ips`. The speed follows the clock. Startup logs board voltage, current, and power. The run logs `ASIC Temp` on stderr.

Without hardware, the emulator binaries still run. CI uses:

```bash
./emu_hw_test 1 2000
./emu_hw_vdf_client --list
./hw_proof_wait_test
```

On this host, `emu_hw_vdf_client --list` printed `Device 0: 'A', loc 559240` and exited 0. Keep `emu_hw_vdf_client` away from a timelord that is extending the chain. It speaks the same TCP protocol as the hardware client. `hw_proof_wait_test` exercises proof publication, cancellation, worker ownership, and proof snapshots through the emulator with no board or FTDI library.

## Connect to the timelord

On the timelord machine, start the full node and the timelord together:

```bash
chia start node timelord-only
```

`node` is `chia_full_node`. `timelord-only` is `chia_timelord`. The group named `timelord` is those two plus `chia_timelord_launcher`. `chia start timelord` therefore starts the launcher. The launcher's `find_vdf_client()` execs the software `vdf_client` from the chiavdf package (`timelord_launcher.process_count` defaults to 3). Those processes connect to the same port and compete for the timelord's three chains. Extra clients sit in `free_clients` until a chain is free. The pool caps at 10, and further connections are closed.

Default `~/.chia/mainnet/config/config.yaml` (chia-blockchain `initial-config.yaml`):

```yaml
timelord:
  vdf_clients:
    ip:
      - localhost     # self_hostname is also listed; it defaults to localhost
      - 127.0.0.1
  vdf_server:
    host: localhost   # self_hostname
    port: 8000
```

`vdf_server.host` follows `self_hostname`, which defaults to `localhost`. `vdf_clients.ip` is the allowlist. A peer that is not listed is closed and logged as `Rejected VDF client from non-whitelisted IP`.

For a client on another machine, set `vdf_server.host` to `0.0.0.0` (or the address that should listen) and add that machine's IPv4 address to `vdf_clients.ip`. Restart the timelord after editing the file.

The client speaks IPv4. The default address is 127.0.0.1, which the usage text calls localhost.

```bash
# Linux source build, from src/
LD_LIBRARY_PATH=hw/libft4222/build-x86_64 ./hw_vdf_client 8000 3

# packaged binary, or a macOS/Windows build already on PATH
hw_vdf_client --ip 192.0.2.10 8000 3
```

`8000` is the timelord port. `3` is how many engines to open (1–3, default 3). Each engine opens its own TCP connection. Stderr shows `Connecting to 127.0.0.1:8000`, then `VDF N: Connected to timelord, waiting for challenge`, then `VDF N: Received challenge, running` once the timelord sends work.

Check the connections from the timelord host. One established connection per running engine:

```bash
ss -tnp | grep ':8000'
```

When a proof finishes, the timelord writes `Estimated IPS:` to its log (default `~/.chia/mainnet/log/debug.log`), with the chain name. The client writes `HW speed:` on its own stderr periodically and when an engine stops.

Stop the client with Ctrl+C or `SIGTERM`. It handles both, tells the timelord to stop, and exits. Then:

```bash
chia stop timelord-only
```

That leaves the full node up. Stop the node separately when you want it down.

## Options

`--list` and `--version` take no value and do not need a port. Flags that take a value accept `--name VALUE` or `--name=VALUE`.

```text
Usage: ./hw_vdf_client [OPTIONS] PORT [N_VDFS]
List of options [default, min - max]:
  --freq N - set ASIC frequency [1100, 200 - 2200]
  --voltage N - set board voltage [0.88, 0.7 - 1.0]
  --ip A.B.C.D - timelord IP address [localhost]
  --device-loc N - FTDI board location from --list [0: first available]
  --vdfs-mask N - mask for enabling VDF engines [7, 1 - 7]
  --vdf-threads N - number of software threads per VDF engine [4, 2 - 64]
  --proof-threads N - number of proof threads per VDF engine
  --retain-asic-values - use recent ASIC samples for off-grid proof endpoints
  --checkpoint-threshold N - keep each segment at most N iterations when that fits in 61 checkpoints [1000000, 8000 - 4294967295]
  --auto-freq-period N - auto-adjust frequency every N seconds [0, 10 - inf]
  --max-freq N - max frequency while auto-freq is on [3000]
  --version - print version and exit
  --list - list available devices and exit
```

`PORT` is required otherwise (1–65535). `N_VDFS` defaults to 3 and must be 1, 2, or 3.

| Flag | Behavior |
| --- | --- |
| `--freq` | MHz. Values outside 200–2200 are rejected. The programmed clock is the first PLL-table entry at or above the request. Default 1100. |
| `--voltage` | Volts, 0.70–1.00. Default 0.88. The log line after the set is the value read back, which can differ slightly from the request. |
| `--ip` | IPv4 of the timelord. Default 127.0.0.1. |
| `--device-loc` | Location id printed by `--list`. `0`, the default, selects the first interface-A board. A nonzero value must match a location id exactly. |
| `--vdfs-mask` | Bit 0 (value 1) is engine 0, bit 1 (value 2) is engine 1, bit 2 (value 4) is engine 2. `0`, the parser default, means "the first `N_VDFS` engines", which is 7 when `N_VDFS` is 3. That is why the usage line shows 7. A non-zero mask is used as given, and `N_VDFS` does not change it. Values above 7 are rejected. |
| `--vdf-threads` | Host threads per engine for intermediates and proofs. Default 4. Range 2–64. |
| `--proof-threads` | How many of those threads may sit in proofs. Omitted, or set to 0, it is `vdf-threads - ceil(vdf-threads / 8)` (3 when `vdf-threads` is 4). Any other explicit value must be less than `--vdf-threads`. |
| `--retain-asic-values` | Opt-in. Keeps up to 64 recent validated ASIC forms and reuses one for an off-grid proof endpoint when the forward check matches. The cache clears on recovery and on each new challenge. |
| `--checkpoint-threshold` | Iterations per checkpoint segment, aligned to the 4,000-iteration value interval. Accepted range 8000–4294967295; default 1,000,000. A request is split only while the protocol's 61-checkpoint room allows it; a longer span uses a larger chunk. |
| `--auto-freq-period` | Enables auto-freq and sets the period in seconds. Minimum 10. Omitted, auto-freq stays off. |
| `--max-freq` | Upper bound used only while auto-freq is on. Default is the last PLL entry, 3000 MHz, which is above the `--freq` ceiling. The parser stores the number `strtod` returns. |
| `--list` | Print interface-A devices and exit. |
| `--version` | Print `<argv0> <git describe>` on stdout and exit. |

`HW speed` falling behind, or `Warning: too much work for VDF aux threads`, means the host proof threads are the bottleneck. Raise `--vdf-threads` before raising the ASIC clock.

This tree does not read `CHIAVDF_HW_DIRECT_STATUS` or `CHIAVDF_HW_TELEMETRY`. The flags above are the supported interface.

## Multiple boards and proof construction

`--device-loc` selects a board. `--list` prints each interface-A location; `0`, the default, selects the first available board. Run one client per board with distinct locations. If simultaneous FTDI enumeration proves unreliable on your macOS host, initialize the clients sequentially: wait for one client to connect before starting the next. Location values can change when USB cables or hubs change.

The standalone test accepts the same location as an optional third argument: `hw_test N_VDFS N_ITERS DEVICE_LOC`.

The checkpoint threshold changes how requests are divided into proof segments; its default remains one million iterations. Smaller thresholds can reduce final segment latency while adding checkpoint work. Endpoint retention is opt-in, bounded to 64 recent validated samples, and cleared on recovery and challenge changes. These options do not change the proof format or disable verification.

`hw_proof_wait_test` runs publication, cancellation, worker ownership, and proof snapshot regressions through the emulator without a physical board. Ports, the rail log, and cooling are in [Engines, ports, and cooling](#engines-ports-and-cooling).

## Clock and voltage

Defaults are 1100 MHz and 0.88 V (`HW_VDF_DEF_FREQ`, `HW_VDF_DEF_VOLTAGE`). `hw_test` always uses those. Stay there for a normal timelord.

`--freq` rejects values outside 200–2200 MHz, and `--voltage` rejects values outside 0.70–1.00 V. Day-to-day operation stays on the defaults. Moving toward those ceilings draws more power and raises die temperature.

Auto-freq is off unless you pass `--auto-freq-period`. While it is on, the client steps one PLL entry up at each period when the next entry is at or below `--max-freq`, and one entry down after a bad VDF value. The default `--max-freq` is 3000, which is above the `--freq` ceiling. Set `--max-freq` to the highest clock you are willing to run, at or below 2200, whenever you turn auto-freq on:

```bash
./hw_vdf_client --freq 1100 --auto-freq-period 60 --max-freq 1200 8000 3
```

This example allows auto-frequency to step up to 1200 MHz; use `--max-freq 1100` if you want the cap to hold the default clock.

The process writes 100 °C external and engine alarm thresholds into the PVT sensor and logs `ASIC Temp = ...` with the current frequency. The boards are passively air cooled. A climbing `ASIC Temp` line, or a stream of `Bad VDF value` restarts, means lower `--freq` or set `--max-freq` back to the clock you want to hold.

## Run it as a service

Logs go to stderr. `--version` is the line that goes to stdout.

Linux, packaged binary. The unit assumes the udev rule from the package is installed.

```ini
[Unit]
Description=Chia ASIC VDF client
After=network-online.target
Wants=network-online.target

[Service]
ExecStart=/usr/bin/hw_vdf_client 8000 3
Restart=on-failure
StandardError=journal

[Install]
WantedBy=multi-user.target
```

For a source build, set `ExecStart` to the absolute path of the binary, `WorkingDirectory` to `.../src`, and `Environment=LD_LIBRARY_PATH=hw/libft4222/build-x86_64` (or `build-arm-v8`).

The unit runs as root unless you add `User=` and `Group=`. The packaged udev rule grants an unprivileged account access to the board.

macOS launchd, with `caffeinate` so the USB session survives idle sleep. Replace the executable path. A release zip uses `.../bin/hw_vdf_client`.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>net.chia.hw-vdf-client</string>
  <key>ProgramArguments</key>
  <array>
    <string>/usr/bin/caffeinate</string>
    <string>-is</string>
    <string>/PATH/TO/hw_vdf_client</string>
    <string>8000</string>
    <string>3</string>
  </array>
  <key>RunAtLoad</key>
  <true/>
  <key>KeepAlive</key>
  <true/>
  <key>StandardErrorPath</key>
  <string>/tmp/hw_vdf_client.err</string>
</dict>
</plist>
```

Install the plist as a LaunchAgent for the logged-in user; a LaunchDaemon runs it as root.

This repo does not ship a Windows service. Run `hw_vdf_client.exe` in a console that will not sleep, or register that command with Task Scheduler at logon. A release zip has every DLL in `bin\`, so no `PATH` change is needed.

## Troubleshooting

**`libft4222.so: cannot open shared object file`.** Linux source build with no rpath. Export `LD_LIBRARY_PATH` to `hw/libft4222/build-x86_64` or `build-arm-v8` before starting the binary. The apt package's copy lives in `/usr/lib`.

**`lzcnt.o: file format not recognized`.** `CC` and `CXX` are different compilers under LTO. Run `make clean`, then use `CC=gcc CXX=g++`.

**`--list` prints `FT_CreateDeviceInfoList` error 18, or `No FTDI devices connected`.** The D2XX stack sees no device. Confirm the board is plugged in and powered. On Linux, `lsusb -d 0403:601c` should show the id the udev rule matches. Re-run the udev reload and replug if the device node is root-only. On Windows, install the D2XX driver from the FT4222H downloads page so the device enumerates for D2XX. On macOS, an immediate exit 137 (SIGKILL) means the FT4222/D2XX dylibs are unsigned. From `src/`, run `codesign -f -s - hw/libft4222/*.dylib hw_vdf_client hw_test` and start the binary again with the executable still in `src/`. A release zip is already signed. Launch it from `bin/`.

**`Failed to open device`.** `init_hw` prints this when `FtdiDriver::Open()` returns an error. With no board attached, that follows the FTDI error above (error 18 or `No FTDI devices connected`). **`No FT4222H detected`** means the device list was readable and no interface A matched. Another `hw_vdf_client` or `hw_test` may already hold the board. Use `--list` and pass the location with `--device-loc` when more than one board is attached.

**Gatekeeper / exit 137.** A browser-downloaded release zip can be quarantined. A notarized release opens after the normal macOS prompt: the zip needs an online lookup, while the `.pkg` carries a stapled ticket and verifies offline. A source build that skips `codesign -f -s - hw/libft4222/*.dylib hw_vdf_client hw_test` (from `src/`) exits 137 (SIGKILL) on launch. The fetch script's quarantine clear does not sign the dylibs.

**No TCP session on port 8000.** The timelord is down, `vdf_server.host` is unreachable, or the source IP is missing from `vdf_clients.ip` (`Rejected VDF client from non-whitelisted IP` in `debug.log`). Software `vdf_client` processes from `chia_timelord_launcher` compete for the three chains. Stop the launcher with `chia stop timelord-launcher-only`, or set `timelord_launcher.process_count` to 0 and restart it. Until something listens, the hardware client's stderr repeats `Connecting to ...` and `connect: Connection refused`.

**Client connects, timelord log never shows `Estimated IPS`.** The node is behind, so the timelord has no peak to assign, or the host cannot finish proofs (`too much work for VDF aux threads`). `HW speed` on the client stderr is the ASIC iteration rate. `Estimated IPS` is the timelord's view after a proof is delivered.

**Temperature, auto-freq, sleep.** `ASIC Temp` is the sensor log. The boards are passively air cooled. Auto-freq steps down only after a bad value, and only when `--auto-freq-period` was set. Sleep on a Mac or laptop suspends USB. Wrap the client in `caffeinate -is`, or otherwise keep the host awake.
