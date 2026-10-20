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

- Hardware binaries may be built with a development-time rpath, then are rewritten during bundle assembly.
- The bundle assembly step removes development-time rpaths and sets `@loader_path/../libexec/chiavdf` on macOS.
- FTDI dylibs are copied into `libexec/chiavdf` with the `@rpath` install names set by `scripts/get-libft4222.sh`.
- GMP is linked from Homebrew at build time. The same staging step copies those dylibs into `libexec/chiavdf` and rewrites their install names to `@rpath/<basename>` with `install_name_tool`.
- Dylibs and binaries are signed together with Developer ID and the hardened runtime. Library validation stays enabled.
- CI unzips the shipped zip, rejects any Mach-O load path or rpath under `/opt/homebrew` or `/usr/local`, and runs `bin/vdf_client --version` with `DYLD_*` unset. `--version` calls `init_gmp()`, so a missing or still-absolute GMP library fails this step. Notarization does not run the binary.

## Signing and notarization behavior

- Release run with Apple secrets available:
  - Sign FTDI dylibs and binaries with Developer ID.
  - Notarize the final release zip with `notarytool`.
- Release run without Apple secrets:
  - Publish an unsigned fallback zip with `-unsigned` suffix.
  - Upload a signing status metadata file with the release assets.

## Local development

- `scripts/get-libft4222.sh install` does not require code signing.
- The script clears macOS download attributes on fetched FTDI files.
- Optional local escape hatch:
  - `CHIAVDF_ADHOC_SIGN_FTDI=1 ./scripts/get-libft4222.sh install`
