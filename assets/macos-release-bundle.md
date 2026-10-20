# macOS ARM64 release bundle for Homebrew

This repository publishes a macOS ARM64 release archive intended for Homebrew/cask consumption.

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
  - `libft4222.1.4.4.190.dylib`
  - `libftd2xx.dylib`
  - versioned `libgmp` and `libgmpxx` (the sonames the binaries were linked against)

## Dynamic library path policy

- Hardware binaries may be built with a development-time rpath of `@executable_path/hw/libft4222`.
- `scripts/stage-macos-bundle-dylibs.sh` deletes that rpath and adds `@loader_path/../libexec/chiavdf` in the same `install_name_tool` pass that rewrites GMP install names. Dylibs do not get that rpath edit.
- FTDI dylibs are copied into `libexec/chiavdf` with the `@rpath` install names set by `scripts/get-libft4222.sh`.
- GMP is linked from Homebrew at build time. The same staging step copies those dylibs into `libexec/chiavdf` and rewrites their install names to `@rpath/<basename>` with `install_name_tool`.
- Dylibs and binaries are signed together. A Developer ID signature uses `codesign --force --timestamp --options runtime`, and library validation stays enabled. A run without Apple secrets ad-hoc signs every staged Mach-O with `codesign -s - --force` so arm64 can launch after `install_name_tool`. The `-unsigned` archive name means there is no Developer ID. `--disable-library-validation` is not used.
- CI unzips the shipped zip, rejects any Mach-O load path or rpath under `/opt/homebrew` or `/usr/local`, and runs every shipped executable with `--version` (`vdf_client`, `vdf_bench`, `hw_vdf_client`, `emu_hw_vdf_client`, `hw_test`, `emu_hw_test`) with `DYLD_*` unset. `vdf_client` and `vdf_bench` call `init_gmp()` before printing the version. The other tools still load GMP through dyld before `--version` returns. Notarization does not run the binaries.

## Signing and notarization behavior

- Release run with Apple secrets available:
  - Sign vendored dylibs and binaries with Developer ID and the hardened runtime.
  - Submit the final zip with `notarytool --wait`. `stapler staple` and `stapler validate` accept an app, a disk image, or an installer package, and they reject a bare zip, so this archive is not stapled. The job gates on the notarytool status.
- Release run without Apple secrets:
  - Ad-hoc sign the staged Mach-O files, then publish the zip with an `-unsigned` suffix.
  - Upload a signing status metadata file with the release assets.

## Local development

- `scripts/get-libft4222.sh install` does not require code signing.
- The script clears macOS download attributes on fetched FTDI files.
- Optional local escape hatch:
  - `CHIAVDF_ADHOC_SIGN_FTDI=1 ./scripts/get-libft4222.sh install`
