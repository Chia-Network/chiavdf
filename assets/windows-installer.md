# Windows installer

winget does not install kernel INF drivers itself; it runs a vendor installer.
The Windows release therefore ships an installer that stages the FTDI driver,
plus a portable zip for users who cannot elevate.

The Windows release ships two installation paths:

- `chiavdf-<version>-windows-amd64-setup.exe` (stable alias
  `chiavdf-win64-setup.exe`) is the machine-scope installer used by the winget
  package `Chia.chiavdf`.
- `chiavdf-<version>-windows-amd64.zip` (stable alias `chiavdf-win64.zip`) is the
  portable fallback for users who cannot elevate or prefer no system changes.

## Why Inno Setup 6

We use Inno Setup 6, not WiX Burn/MSI. The deciding requirements are:

1. **One signed artifact.** Inno produces a single setup EXE that can be signed
   with the existing Azure Artifact Signing action. A WiX Burn bundle would add an
   MSI plus a bundle EXE, both of which need signing and verification.
2. **Small, direct driver hook.** The driver installation is two `pnputil`
   commands. Inno's Pascal `[Code]` section runs them, inspects the exit code,
   and turns `3010` into a pending-reboot result without a custom MSI action or a
   bootstrapper application.
3. **Machine `PATH` is straightforward.** The installer writes the machine
   `Environment\Path` value, broadcasts the environment change through Inno's
   `ChangesEnvironment=yes`, and removes its entry on uninstall.
4. **winget support.** winget accepts `InstallerType: inno` and its silent
   switches. The manifest can use the stable `chiavdf-win64-setup.exe` URL and
   list `3010` in `InstallerSuccessCodes`.
5. **Lower CI cost.** The Windows jobs download the pinned Inno Setup 6.7.3
   installer, verify its SHA-256, and run `ISCC.exe`. WiX would require the WiX
   SDK, MSI authoring, Burn authoring, and custom-action plumbing for the same
   result.

WiX remains the better choice if this project later needs a first-class MSI for
enterprise deployment. That can be added as a second package without changing
the winget `inno` installer.

## What it installs

The installer requires x64 Windows 10 version 2004 (build 19041) or newer.
That is the tested baseline and matches the winget manifest
`MinimumOSVersion`; `pnputil /add-driver` and the x64 kernel driver package
also require 64-bit Windows 10 or newer. It does not support ARM64 Windows.
The default directory is `%ProgramFiles%\Chia\chiavdf`. `DisableDirPage=auto`
hides the directory page on an upgrade.

The install directory must be strictly under `{autopf}` (`%ProgramFiles%`).
`PrepareToInstall` checks the final `{app}` value, so this applies to the
interactive directory page, `/DIR=...`, and a reused previous directory alike.
The bare `{autopf}` directory itself is rejected. Installations outside
Program Files, including `winget --location` and any directory on another
drive, are not supported; setup stops with an error instead of adding a
user-writable directory to the system `PATH`. A disallowed directory fails
cleanly in silent mode instead of waiting on a message box.

```text
bin\vdf_client.exe
bin\vdf_bench.exe
bin\hw_vdf_client.exe
bin\hw_test.exe
bin\emu_hw_vdf_client.exe
bin\emu_hw_test.exe
bin\LibFT4222-64.dll
bin\FTD2XX.dll
bin\mpir.dll
bin\<MPIR CPU variants>
bin\<VC++ 2015-2022 runtime DLLs>
driver\ftdibus.inf
driver\ftdibus.cat
driver\ftdiport.inf
driver\ftdiport.cat
driver\amd64\...
driver\i386\...
driver\SOURCE.txt
LICENSE.txt
```

The setup exe embeds the **already signed** `bin\` tree. It does not download
anything at install time. The driver files are the unmodified FTDI CDM
2.12.36.20 package that `scripts/get-libft4222.ps1` stages under
`src/hw/libft4222/driver`; the catalog signatures are validated before the
installer is compiled. The INF files are copied byte-for-byte, including
FTDI's license text, which keeps the package within FTDI's redistribution
terms. `driver\SOURCE.txt` records the source URL and SHA-256.

## Driver installation

After copying files, the installer adds the `bin` directory to the machine
`PATH` and then runs:

```text
pnputil.exe /add-driver "%ProgramFiles%\Chia\chiavdf\driver\ftdibus.inf" /install
pnputil.exe /add-driver "%ProgramFiles%\Chia\chiavdf\driver\ftdiport.inf" /install
```

`ftdibus.inf` declares `USB\VID_0403&PID_601C` and the FT4222H interfaces.
`ftdiport.inf` is the VCP child driver from the same CDM package. Installing
both matches the FTDI CDM package and keeps the ASIC D2XX path and any VCP use
consistent.

`pnputil` exit codes `0`, `259`, `3010`, and `1641` are treated as success.
`259` means the package was staged but no device matched, or a newer driver is
already active; that is normal for a software-only timelord host. `3010`/`1641`
set the installer's pending-reboot state, and `GetCustomSetupExitCode` returns
`3010`; the winget manifest must list it in `InstallerSuccessCodes`. Any other
nonzero `pnputil` code fails setup with a diagnostic in the setup log.

A driver staging failure is logged and returns exit code `1603` instead of
raising an exception. The `PATH` entry is written before the driver runs, so a
driver failure does not leave the installed files without a usable `PATH`.
Exit code `1603` therefore means the files and PATH entry were installed but
the driver failed. Interactive users see the failure on the finish page and in
a suppressible message box; silent users see the exit code and the setup log.
The setup exit code is otherwise `0` or `3010`. `/DRIVER=0` skips driver
staging for software-only installs and for tests that must not change the
driver store.

The driver is **not removed on uninstall**. It is shared with other FTDI devices
and may be in use by a device that is not part of this package. Removing it would
be a surprising, system-wide change.

## `PATH` and uninstall

The installer appends `%ProgramFiles%\Chia\chiavdf\bin` to the machine
`PATH` only if an equivalent entry is not already present. Add and remove use
the same comparison, which ignores case and a trailing backslash, so a
reinstall does not create a duplicate. `ChangesEnvironment` broadcasts the
update so new shells see it without a logout; already-open terminals must be
restarted. The uninstaller removes every equivalent entry and leaves other
`PATH` components alone. A failed registry read aborts the add instead of
writing a `PATH` that contains only the new directory.

`[InstallDelete]` removes the old `bin` and `driver` directories only when the
new install directory matches the previous install directory. A fresh install,
or an upgrade to a different directory, does not delete user files.

## Signing and verification

The release flow is:

1. Build and test Windows; upload `windows-amd64-binaries`, including the staged
   driver directory.
2. Install the pinned Inno Setup 6.7.3 release after verifying its SHA-256, then
   install the Azure signing client.
3. Sign the six executables and MPIR DLLs.
4. Package and verify the portable zip.
5. Build the setup exe from the signed bundle and driver files. `ISCC` receives
   a `SignTool` through `/S`, so Inno signs the setup exe and, with
   `SignedUninstaller=yes`, `unins000.exe` during compilation. There is no
   separate installer-signing step.
6. Finalize the hash and stable alias from the signed EXE, then verify its
   checksum, Authenticode chain, timestamp, and expected signer.
7. Run a silent install with `/DRIVER=1`, accept exit codes `0` and `3010`,
   check all six `--version` probes, confirm `pnputil /enum-drivers` lists the
   driver, reinstall to confirm `PATH` contains the entry exactly once, and
   uninstall.

`TestInstaller` runs on the ephemeral CI runner. The PR job compiles the
installer without signing and runs the same smoke test; a non-elevated process
fails in CI instead of skipping the test.

## winget manifest

The manifest is maintained outside this repository. The package should use:

```yaml
InstallerType: inno
Scope: machine
Architecture: x64
MinimumOSVersion: 10.0.19041.0
InstallerUrl: https://github.com/Chia-Network/chiavdf/releases/download/<tag>/chiavdf-win64-setup.exe
InstallerSha256: <sha256 of the stable alias>
InstallerSuccessCodes:
  - 3010
InstallerSwitches:
  Silent: /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /RESTARTEXITCODE=3010
  SilentWithProgress: /SILENT /SUPPRESSMSGBOXES /NORESTART /RESTARTEXITCODE=3010
UpgradeBehavior: install
InstallModes:
  - interactive
  - silent
  - silentWithProgress
```

The explicit `/RESTARTEXITCODE=3010` makes a pending driver reboot visible to
winget even if Inno's own restart handling differs between versions.

Use the stable `chiavdf-win64-setup.exe` URL for upgrades. It is produced only
when `IS_RELEASE=true`; manual dispatch and pre-release builds have the versioned
setup exe only. Keep the versioned
`chiavdf-<version>-windows-amd64-setup.exe` and its `.sha256` alongside it for
auditing and direct downloads. The portable zip remains published for users who
cannot elevate.

The release action's Glue payload now exposes both
`windows_amd64: "chiavdf-win64.zip"` and
`windows_amd64_installer: "chiavdf-win64-setup.exe"`. The external Glue template
and winget manifest must consume the installer field (or use the stable URL
directly) to make `winget install Chia.chiavdf` install the driver. Keeping the
zip field preserves existing consumers that still expect the portable asset.
