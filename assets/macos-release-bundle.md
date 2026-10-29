# macOS release bundles and installer packages

This repository publishes macOS ARM64 and Intel release archives intended for Homebrew/cask consumption, plus a signed installer package for direct installs.

## Archive layout

- `bin/`
  - `hw_vdf_client`
  - `emu_hw_vdf_client`
  - `hw_test`
  - `emu_hw_test`
  - `vdf_client`
  - `vdf_bench`
- `libexec/chiavdf/`
  - `libft4222.dylib`
  - `libft4222.1.4.4.221.dylib`
  - `libftd2xx.dylib`
  - versioned `libgmp` and `libgmpxx` (the sonames the binaries were linked against)

## Installer package

`chiavdf-<tag>-macos-<arch>.pkg` contains the same `bin/` and `libexec/chiavdf/` tree and installs it under `/usr/local`, so the `@loader_path/../libexec/chiavdf` rpath resolves to `/usr/local/libexec/chiavdf`. Uninstall with `pkgutil --forget com.chia.chiavdf` and remove the installed files.

## Dynamic library path policy

- Hardware binaries may be built with a development-time rpath of `@executable_path/hw/libft4222`.
- `scripts/stage-macos-bundle-dylibs.sh` deletes that rpath and adds `@loader_path/../libexec/chiavdf` in the same `install_name_tool` pass that rewrites GMP install names. Dylibs do not get that rpath edit.
- FTDI dylibs are copied into `libexec/chiavdf` with the `@rpath` install names set by `scripts/get-libft4222.sh`.
- GMP is linked from Homebrew at build time. The same staging step copies those dylibs into `libexec/chiavdf` and rewrites their install names to `@rpath/<basename>` with `install_name_tool`.
- Dylibs and binaries are signed together. A Developer ID signature uses `codesign --force --timestamp --options runtime`, and library validation stays enabled. When a run is neither a final release nor a notarization request, every staged Mach-O is ad-hoc signed with `codesign -s - --force` so arm64 can launch after `install_name_tool`. The `-unsigned` archive name means there is no Developer ID. `--disable-library-validation` is not used.
- CI unzips the shipped zip, rejects any Mach-O load path or rpath under `/opt/homebrew` or `/usr/local`, and runs every executable in `scripts/macos-bundle-tools.sh` with `--version` and `DYLD_*` unset. `vdf_client` and `vdf_bench` call `init_gmp()` before printing the version. The other tools still load GMP through dyld before `--version` returns. The notary service never runs the binaries.

## Signing and notarization behavior

- Final release or manual run with `notarize_macos` enabled:
  - Sign vendored dylibs and binaries with Developer ID and the hardened runtime.
  - Build the `chiavdf-<tag>-macos-<arch>.pkg` from the same staged tree and sign it with Developer ID Installer.
  - Submit the pkg with `notarytool --wait`, then `stapler staple` and `stapler validate` it. It is the only shipped format that can carry a stapled ticket.
  - Do not submit the zip separately. Notarization is recorded per code object (CDHash), and the pkg contains the same binaries, so Gatekeeper finds them during the zip's online lookup. A bare zip cannot be stapled.
  - Final releases require `APPLE_DEV_ID_APP`/`APPLE_DEV_ID_APP_PASS`, `APPLE_DEV_ID_INSTALLER`/`APPLE_DEV_ID_INSTALLER_PASS`, and `APPLE_NOTARIZE_USERNAME`/`APPLE_NOTARIZE_PASSWORD`/`APPLE_TEAM_ID`. The build fails instead of publishing unsigned release assets when any are missing.
- Run that is neither a final release nor a notarization request:
  - Ad-hoc sign the staged Mach-O files and produce the zip and pkg with an `-unsigned` suffix as workflow artifacts.
  - Upload a signing status metadata file with the other artifacts.

## Local development

- `scripts/get-libft4222.sh install` does not require code signing.
- The script clears macOS download attributes on fetched FTDI files.
- Optional local escape hatch:
  - `CHIAVDF_ADHOC_SIGN_FTDI=1 ./scripts/get-libft4222.sh install`
