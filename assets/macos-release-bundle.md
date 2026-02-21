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

## Dynamic library path policy

- Hardware binaries are linked with `@loader_path/../libexec/chiavdf` rpath on macOS.
- FTDI dylibs use `@rpath` install names.
- CI verifies with `otool` that release binaries do not reference local absolute build paths.

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
