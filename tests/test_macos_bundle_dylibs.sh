#!/usr/bin/env bash
# Exercises macOS zip staging and verification with fake otool/install_name_tool.
# The macOS job runs the real tools against the shipped zip; this runs on Linux.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGE="$ROOT_DIR/scripts/stage-macos-bundle-dylibs.sh"
VERIFY="$ROOT_DIR/scripts/verify-macos-bundle.sh"

# shellcheck source=../scripts/macos-bundle-tools.sh disable=SC1091
source "$ROOT_DIR/scripts/macos-bundle-tools.sh"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

assert_eq() {
  local got="$1"
  local want="$2"
  local desc="$3"
  if [ "$got" != "$want" ]; then
    fail "$desc: got [$got] want [$want]"
  fi
}

expect_fail() {
  local desc="$1"
  shift
  local output status
  set +e
  output="$("$@" 2>&1)"
  status=$?
  set -e
  if [ "$status" -eq 0 ]; then
    printf '%s\n' "$output" >&2
    fail "$desc (expected non-zero)"
  fi
  echo "ok: $desc"
}

write_otool() {
  local mode="$1"
  local name="$2"
  local body="$3"
  mkdir -p "$OTOOL_FIXTURE/$mode"
  printf '%s\n' "$body" > "$OTOOL_FIXTURE/$mode/$name"
}

new_workspace() {
  WORK="$(mktemp -d)"
  OTOOL_FIXTURE="$WORK/otool"
  INSTALL_NAME_LOG="$WORK/install_name.log"
  XATTR_LOG="$WORK/xattr.log"
  : > "$INSTALL_NAME_LOG"
  : > "$XATTR_LOG"
  mkdir -p "$WORK/bin" "$WORK/mocks" "$WORK/ftdi" "$WORK/libexec"
  cat > "$WORK/mocks/otool" <<'EOF'
#!/bin/bash
set -euo pipefail
mode=""
target=""
for arg in "$@"; do
  case "$arg" in
    -L) mode=loads ;;
    -D) mode=id ;;
    -l) mode=rpaths ;;
    -*)
      echo "unsupported otool flag: $arg" >&2
      exit 1
      ;;
    *) target="$arg" ;;
  esac
done
base="$(basename "$target")"
fixture="${OTOOL_FIXTURE:?}/$mode/$base"
if [ ! -f "$fixture" ]; then
  fixture="${OTOOL_FIXTURE}/$mode/_default"
fi
if [ ! -f "$fixture" ]; then
  echo "no otool fixture for -$mode $target" >&2
  exit 1
fi
cat "$fixture"
EOF
  cat > "$WORK/mocks/install_name_tool" <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >> "${INSTALL_NAME_LOG:?}"
EOF
  cat > "$WORK/mocks/xattr" <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >> "${XATTR_LOG:?}"
if [ "${XATTR_FAIL:-0}" = "1" ]; then
  exit 1
fi
EOF
  chmod +x "$WORK/mocks/otool" "$WORK/mocks/install_name_tool" "$WORK/mocks/xattr"
  printf 'ftd2xx' > "$WORK/ftdi/libftd2xx.dylib"
  printf 'ft4222-real' > "$WORK/ftdi/libft4222.1.4.4.190.dylib"
  ln -s "libft4222.1.4.4.190.dylib" "$WORK/ftdi/libft4222.dylib"
  export OTOOL_FIXTURE INSTALL_NAME_LOG XATTR_LOG
  export PATH="$WORK/mocks:$PATH"
  export XATTR_FAIL=0
}

gmp_paths() {
  GMP_DIR="$WORK/opt/homebrew/opt/gmp/lib"
  mkdir -p "$GMP_DIR"
  GMP="$GMP_DIR/libgmp.10.dylib"
  GMPXX="$GMP_DIR/libgmpxx.4.dylib"
  printf 'gmp-bytes' > "$GMP"
  printf 'gmpxx-bytes' > "$GMPXX"
}

write_gmp_fixtures() {
  local gmp_id="$1"
  local gmpxx_id="$2"
  write_otool id libgmp.10.dylib "$(printf '%s\n%s' "libgmp.10.dylib:" "$gmp_id")"
  write_otool loads libgmp.10.dylib "$(printf '%s\n\t%s (compatibility version 16.0.0, current version 16.0.0)\n\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1.0.0)' "libgmp.10.dylib:" "$gmp_id")"
  write_otool id libgmpxx.4.dylib "$(printf '%s\n%s' "libgmpxx.4.dylib:" "$gmpxx_id")"
  write_otool loads libgmpxx.4.dylib "$(printf '%s\n\t%s (compatibility version 11.0.0, current version 11.0.0)\n\t%s (compatibility version 16.0.0, current version 16.0.0)\n\t/usr/lib/libc++.1.dylib (compatibility version 1.0.0, current version 1.0.0)\n\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1.0.0)' "libgmpxx.4.dylib:" "$gmpxx_id" "$gmp_id")"
  write_otool id libftd2xx.dylib "$(printf '%s\n%s' "libftd2xx.dylib:" "@rpath/libftd2xx.dylib")"
  write_otool loads libftd2xx.dylib "$(printf '%s\n\t%s\n\t/usr/lib/libSystem.B.dylib' "libftd2xx.dylib:" "@rpath/libftd2xx.dylib")"
  write_otool id libft4222.1.4.4.190.dylib "$(printf '%s\n%s' "libft4222.1.4.4.190.dylib:" "@rpath/libft4222.dylib")"
  write_otool loads libft4222.1.4.4.190.dylib "$(printf '%s\n\t%s\n\t%s\n\t/usr/lib/libSystem.B.dylib' "libft4222.1.4.4.190.dylib:" "@rpath/libft4222.dylib" "@rpath/libftd2xx.dylib")"
}

write_bin_fixture() {
  local name="$1"
  shift
  : > "$WORK/bin/$name"
  write_otool id "$name" "$name:"
  mkdir -p "$OTOOL_FIXTURE/loads"
  {
    echo "$name:"
    for dep in "$@"; do
      printf '\t%s (compatibility version 1.0.0, current version 1.0.0)\n' "$dep"
    done
    printf '\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1.0.0)\n'
  } > "$OTOOL_FIXTURE/loads/$name"
  # No LC_RPATH yet, so the staging pass must add the bundle rpath.
  write_otool rpaths "$name" ""
}

stage_bundle() {
  "$STAGE" "$WORK/bin" "$WORK/libexec" "$WORK/ftdi"
}

test_stage_rewrites_homebrew_gmp_with_ftdi() {
  new_workspace
  gmp_paths
  write_gmp_fixtures "$GMP" "$GMPXX"
  write_bin_fixture vdf_client "$GMPXX" "$GMP"
  write_bin_fixture hw_vdf_client "@rpath/libft4222.dylib" "@rpath/libftd2xx.dylib" "$GMPXX" "$GMP"
  write_otool rpaths hw_vdf_client "$(printf '%s\n' \
    '          cmd LC_RPATH' \
    '      cmdsize 48' \
    '         path @executable_path/hw/libft4222 (offset 12)')"
  cp "$GMP" "$WORK/gmp.before"
  cp "$GMPXX" "$WORK/gmpxx.before"

  stage_bundle

  [ -f "$WORK/libexec/libftd2xx.dylib" ] || fail "missing libftd2xx"
  [ -L "$WORK/libexec/libft4222.dylib" ] || fail "libft4222 should stay a symlink"
  assert_eq "$(readlink "$WORK/libexec/libft4222.dylib")" "libft4222.1.4.4.190.dylib" "FTDI symlink target"
  cmp -s "$WORK/libexec/libgmp.10.dylib" "$GMP" || fail "vendored libgmp content"
  cmp -s "$WORK/libexec/libgmpxx.4.dylib" "$GMPXX" || fail "vendored libgmpxx content"
  cmp -s "$GMP" "$WORK/gmp.before" || fail "Homebrew libgmp was modified"
  cmp -s "$GMPXX" "$WORK/gmpxx.before" || fail "Homebrew libgmpxx was modified"

  grep -Fxq -e "-id @rpath/libgmp.10.dylib $WORK/libexec/libgmp.10.dylib" "$INSTALL_NAME_LOG" || fail "missing libgmp -id"
  grep -Fxq -e "-id @rpath/libgmpxx.4.dylib $WORK/libexec/libgmpxx.4.dylib" "$INSTALL_NAME_LOG" || fail "missing libgmpxx -id"
  grep -Fxq -e "-change $GMP @rpath/libgmp.10.dylib $WORK/bin/vdf_client" "$INSTALL_NAME_LOG" || fail "vdf_client libgmp -change"
  grep -Fxq -e "-change $GMPXX @rpath/libgmpxx.4.dylib $WORK/bin/vdf_client" "$INSTALL_NAME_LOG" || fail "vdf_client libgmpxx -change"
  grep -Fxq -e "-change $GMP @rpath/libgmp.10.dylib $WORK/bin/hw_vdf_client" "$INSTALL_NAME_LOG" || fail "hw client libgmp -change"
  grep -Fxq -e "-change $GMP @rpath/libgmp.10.dylib $WORK/libexec/libgmpxx.4.dylib" "$INSTALL_NAME_LOG" || fail "libgmpxx dependency -change"
  if grep -F -e "-change $GMP @rpath/libgmp.10.dylib $WORK/libexec/libgmp.10.dylib" "$INSTALL_NAME_LOG"; then
    fail "libgmp id was passed to -change"
  fi
  if grep -F -e "-id @rpath" "$INSTALL_NAME_LOG" | grep -F "libftd2xx.dylib"; then
    fail "FTDI install name was rewritten a second time"
  fi
  if grep -F -- "$GMP_DIR" "$INSTALL_NAME_LOG" | grep -v -- "-change $GMP \|-change $GMPXX "; then
    fail "install_name_tool touched the Homebrew prefix"
  fi
  grep -Fxq -e "-dr com.apple.provenance $WORK/libexec" "$XATTR_LOG" || fail "provenance xattr was not cleared"
  grep -Fxq -e "-dr com.apple.quarantine $WORK/libexec" "$XATTR_LOG" || fail "quarantine xattr was not cleared"
  grep -Fxq -e "-delete_rpath @executable_path/hw/libft4222 $WORK/bin/hw_vdf_client" "$INSTALL_NAME_LOG" || fail "development rpath was not deleted"
  grep -Fxq -e "-add_rpath @loader_path/../libexec/chiavdf $WORK/bin/vdf_client" "$INSTALL_NAME_LOG" || fail "vdf_client bundle rpath was not added"
  grep -Fxq -e "-add_rpath @loader_path/../libexec/chiavdf $WORK/bin/hw_vdf_client" "$INSTALL_NAME_LOG" || fail "hw_vdf_client bundle rpath was not added"
  if grep -F -- "-delete_rpath" "$INSTALL_NAME_LOG" | grep -F "$WORK/bin/vdf_client"; then
    fail "vdf_client had no development rpath to delete"
  fi
  if grep -E -- '^-delete_rpath |^-add_rpath ' "$INSTALL_NAME_LOG" | grep -F "$WORK/libexec/"; then
    fail "rpath edit was applied to a dylib"
  fi
  add_count="$(grep -c -F -- "-add_rpath " "$INSTALL_NAME_LOG" || true)"
  assert_eq "$add_count" "2" "bundle rpath added once per executable"
  echo "ok: stage rewrites Homebrew GMP next to FTDI"
  rm -rf "$WORK"
}

test_stage_rewrites_usr_local_gmp() {
  new_workspace
  GMP_DIR="$WORK/usr/local/opt/gmp/lib"
  mkdir -p "$GMP_DIR"
  GMP="$GMP_DIR/libgmp.10.dylib"
  GMPXX="$GMP_DIR/libgmpxx.4.dylib"
  printf 'gmp-intel' > "$GMP"
  printf 'gmpxx-intel' > "$GMPXX"
  write_gmp_fixtures "$GMP" "$GMPXX"
  write_bin_fixture vdf_client "$GMPXX" "$GMP"
  stage_bundle
  grep -Fxq -e "-change $GMP @rpath/libgmp.10.dylib $WORK/bin/vdf_client" "$INSTALL_NAME_LOG" || fail "usr/local libgmp was not rewritten"
  grep -Fxq -e "-add_rpath @loader_path/../libexec/chiavdf $WORK/bin/vdf_client" "$INSTALL_NAME_LOG" || fail "usr/local bundle rpath was not added"
  if grep -F -- "-delete_rpath" "$INSTALL_NAME_LOG"; then
    fail "delete_rpath ran without a development rpath"
  fi
  if grep -E -- '^-delete_rpath |^-add_rpath ' "$INSTALL_NAME_LOG" | grep -F "$WORK/libexec/"; then
    fail "rpath edit was applied to a dylib"
  fi
  echo "ok: stage rewrites /usr/local GMP"
  rm -rf "$WORK"
}

test_stage_refuses_missing_dependency() {
  new_workspace
  missing="$WORK/opt/homebrew/opt/gmp/lib/libgmp.10.dylib"
  write_bin_fixture vdf_client "$missing"
  expect_fail "missing GMP dependency fails the bundle" stage_bundle
  rm -rf "$WORK"
}

test_stage_refuses_basename_collision() {
  new_workspace
  mkdir -p "$WORK/opt/a" "$WORK/opt/b"
  printf 'one' > "$WORK/opt/a/libgmp.10.dylib"
  printf 'two' > "$WORK/opt/b/libgmp.10.dylib"
  write_bin_fixture vdf_client "$WORK/opt/a/libgmp.10.dylib" "$WORK/opt/b/libgmp.10.dylib"
  expect_fail "different libraries with one basename fail the bundle" stage_bundle
  rm -rf "$WORK"
}

test_stage_refuses_relative_install_name() {
  new_workspace
  write_bin_fixture vdf_client "libgmp.10.dylib"
  expect_fail "relative install name fails the bundle" stage_bundle
  rm -rf "$WORK"
}

test_stage_refuses_unreadable_rpath() {
  new_workspace
  gmp_paths
  write_gmp_fixtures "$GMP" "$GMPXX"
  write_bin_fixture vdf_client "$GMPXX" "$GMP"
  rm -f "$OTOOL_FIXTURE/rpaths/vdf_client"
  expect_fail "unreadable LC_RPATH fails the bundle" stage_bundle
  rm -rf "$WORK"
}

test_stage_refuses_missing_rpath_dylib() {
  new_workspace
  gmp_paths
  write_gmp_fixtures "$GMP" "$GMPXX"
  write_bin_fixture vdf_client "@rpath/libmissing.dylib" "$GMP"
  expect_fail "unresolved @rpath dependency fails the bundle" stage_bundle
  rm -rf "$WORK"
}

bundle_rpath_fixture() {
  cat <<'EOF'
          cmd LC_RPATH
      cmdsize 48
         path @loader_path/../libexec/chiavdf (offset 12)
EOF
}

make_zip() {
  local root="$1"
  local zip_path="$2"
  python3 - "$root" "$zip_path" <<'PY'
import os
import sys
import zipfile

root, zip_path = sys.argv[1], sys.argv[2]
parent = os.path.dirname(root)
with zipfile.ZipFile(zip_path, "w") as zf:
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in filenames:
            full = os.path.join(dirpath, name)
            if os.path.islink(full):
                continue
            info = zipfile.ZipInfo(os.path.relpath(full, parent))
            info.create_system = 3
            info.external_attr = (os.stat(full).st_mode & 0xFFFF) << 16
            with open(full, "rb") as fh:
                zf.writestr(info, fh.read())
PY
}

prepare_verify_tree() {
  new_workspace
  BUNDLE="$WORK/chiavdf-test"
  mkdir -p "$BUNDLE/bin" "$BUNDLE/libexec/chiavdf"
  for tool in "${MACOS_BUNDLE_TOOLS[@]}"; do
    cat > "$BUNDLE/bin/$tool" <<EOF
#!/bin/bash
if [ -n "\${DYLD_LIBRARY_PATH+x}" ] || [ -n "\${DYLD_FALLBACK_LIBRARY_PATH+x}" ] || [ -n "\${DYLD_INSERT_LIBRARIES+x}" ]; then
  echo "dyld path leaked" >&2
  exit 2
fi
if [ "\${1:-}" != "--version" ]; then
  echo "expected --version" >&2
  exit 3
fi
echo "$tool test-version"
EOF
    chmod +x "$BUNDLE/bin/$tool"
  done
  for lib in libgmp.10.dylib libgmpxx.4.dylib libftd2xx.dylib libft4222.dylib libft4222.1.4.4.190.dylib; do
    printf '%s\n' "$lib" > "$BUNDLE/libexec/chiavdf/$lib"
  done

  gmp_loads="$(printf '%s\n\t%s\n\t%s\n\t/usr/lib/libSystem.B.dylib' "bin:" "@rpath/libgmpxx.4.dylib" "@rpath/libgmp.10.dylib")"
  hw_loads="$(printf '%s\n\t%s\n\t%s\n\t%s\n\t/usr/lib/libSystem.B.dylib' "bin:" "@rpath/libft4222.dylib" "@rpath/libgmpxx.4.dylib" "@rpath/libgmp.10.dylib")"
  for tool in "${MACOS_BUNDLE_TOOLS[@]}"; do
    case "$tool" in
      vdf_client|vdf_bench|emu_hw_vdf_client|emu_hw_test)
        write_otool loads "$tool" "$gmp_loads"
        ;;
      hw_vdf_client|hw_test)
        write_otool loads "$tool" "$hw_loads"
        ;;
      *)
        fail "no otool dependency fixture for $tool"
        ;;
    esac
    write_otool rpaths "$tool" "$(bundle_rpath_fixture)"
  done
  for lib in libgmp.10.dylib libgmpxx.4.dylib libftd2xx.dylib libft4222.dylib libft4222.1.4.4.190.dylib; do
    write_otool loads "$lib" "$(printf '%s\n\t@rpath/%s\n\t/usr/lib/libSystem.B.dylib' "$lib:" "$lib")"
  done
  # libgmpxx records a sibling @rpath dependency, matching a rewritten Homebrew dylib.
  write_otool loads libgmpxx.4.dylib "$(printf '%s\n\t@rpath/libgmpxx.4.dylib\n\t@rpath/libgmp.10.dylib\n\t/usr/lib/libc++.1.dylib' "libgmpxx.4.dylib:")"
}

test_verify_launches_with_dyld_unset() {
  prepare_verify_tree
  make_zip "$BUNDLE" "$WORK/bundle.zip"
  export DYLD_FALLBACK_LIBRARY_PATH="/opt/homebrew/lib"
  export DYLD_LIBRARY_PATH="/usr/local/lib"
  output="$("$VERIFY" "$WORK/bundle.zip")"
  for tool in "${MACOS_BUNDLE_TOOLS[@]}"; do
    printf '%s\n' "$output" | grep -Fxq "$tool test-version" || fail "$tool did not run from the zip"
  done
  if printf '%s\n' "$output" | grep -F "dyld path leaked"; then
    fail "DYLD paths were visible to a shipped executable"
  fi
  unset DYLD_FALLBACK_LIBRARY_PATH DYLD_LIBRARY_PATH
  echo "ok: verify launches every shipped executable with DYLD paths unset"
  rm -rf "$WORK"
}

test_verify_rejects_homebrew_and_usr_local() {
  prepare_verify_tree
  write_otool loads vdf_client "$(printf '%s\n\t/opt/homebrew/opt/gmp/lib/libgmp.10.dylib\n\t@rpath/libgmpxx.4.dylib' "vdf_client:")"
  make_zip "$BUNDLE" "$WORK/homebrew.zip"
  expect_fail "otool /opt/homebrew fails verification" "$VERIFY" "$WORK/homebrew.zip"

  prepare_verify_tree
  write_otool loads vdf_client "$(printf '%s\n\t/usr/local/opt/gmp/lib/libgmp.10.dylib\n\t@rpath/libgmpxx.4.dylib' "vdf_client:")"
  make_zip "$BUNDLE" "$WORK/usr-local.zip"
  expect_fail "otool /usr/local fails verification" "$VERIFY" "$WORK/usr-local.zip"
  rm -rf "$WORK"
}

add_zip_entries() {
  local zip_path="$1"
  shift
  python3 - "$zip_path" "$@" <<'PY'
import sys
import zipfile

zip_path = sys.argv[1]
with zipfile.ZipFile(zip_path, "a") as zf:
    for spec in sys.argv[2:]:
        name, payload = spec.split("=", 1)
        zf.writestr(name, payload)
PY
}

add_zip_bytes() {
  local zip_path="$1"
  shift
  python3 - "$zip_path" "$@" <<'PY'
import sys
import zipfile

zip_path = sys.argv[1]
with zipfile.ZipFile(zip_path, "a") as zf:
    for spec in sys.argv[2:]:
        name, hex_payload = spec.split("=", 1)
        zf.writestr(name, bytes.fromhex(hex_payload))
PY
}

test_verify_appledouble_and_layout() {
  prepare_verify_tree
  make_zip "$BUNDLE" "$WORK/apple.zip"
  add_zip_bytes "$WORK/apple.zip" \
    "chiavdf-test/bin/._vdf_client=00051607" \
    "._chiavdf-test=00051607"
  add_zip_entries "$WORK/apple.zip" \
    "__MACOSX/chiavdf-test/bin/._vdf_client=ignored" \
    "chiavdf-test/bin/.DS_Store=dsstore"
  "$VERIFY" "$WORK/apple.zip" >/dev/null
  echo "ok: real AppleDouble, .DS_Store, a top-level __MACOSX sibling, and ditto's root ._* pass"

  prepare_verify_tree
  make_zip "$BUNDLE" "$WORK/stray-dot.zip"
  add_zip_entries "$WORK/stray-dot.zip" "chiavdf-test/bin/._stray=appledouble"
  expect_fail "bin/._stray without a sibling fails verification" "$VERIFY" "$WORK/stray-dot.zip"

  prepare_verify_tree
  make_zip "$BUNDLE" "$WORK/notes.zip"
  add_zip_entries "$WORK/notes.zip" "chiavdf-test/libexec/chiavdf/._notes.txt=notes"
  expect_fail "libexec AppleDouble without a sibling fails verification" "$VERIFY" "$WORK/notes.zip"

  prepare_verify_tree
  make_zip "$BUNDLE" "$WORK/nested-macosx.zip"
  add_zip_entries "$WORK/nested-macosx.zip" "chiavdf-test/__MACOSX/payload.txt=payload"
  expect_fail "an in-bundle __MACOSX file fails verification" "$VERIFY" "$WORK/nested-macosx.zip"

  prepare_verify_tree
  make_zip "$BUNDLE" "$WORK/fake-magic.zip"
  add_zip_entries "$WORK/fake-magic.zip" "chiavdf-test/bin/._vdf_client=appledouble"
  expect_fail "._vdf_client whose body is not AppleDouble fails verification" "$VERIFY" "$WORK/fake-magic.zip"

  prepare_verify_tree
  make_zip "$BUNDLE" "$WORK/readme.zip"
  add_zip_entries "$WORK/readme.zip" "README=hello"
  expect_fail "a root README fails verification" "$VERIFY" "$WORK/readme.zip"

  prepare_verify_tree
  make_zip "$BUNDLE" "$WORK/bad-root-apple.zip"
  add_zip_entries "$WORK/bad-root-apple.zip" "._chiavdf-test=appledouble"
  expect_fail "a root AppleDouble without the magic header fails verification" "$VERIFY" "$WORK/bad-root-apple.zip"

  prepare_verify_tree
  make_zip "$BUNDLE" "$WORK/orphan-root-apple.zip"
  add_zip_bytes "$WORK/orphan-root-apple.zip" "._other=00051607"
  expect_fail "a root AppleDouble without a sibling fails verification" "$VERIFY" "$WORK/orphan-root-apple.zip"

  prepare_verify_tree
  make_zip "$BUNDLE" "$WORK/extra-dir.zip"
  add_zip_entries "$WORK/extra-dir.zip" "notes/README=not metadata"
  expect_fail "a second top-level directory fails verification" "$VERIFY" "$WORK/extra-dir.zip"

  prepare_verify_tree
  make_zip "$BUNDLE" "$WORK/stray.zip"
  add_zip_entries "$WORK/stray.zip" "chiavdf-test/bin/README.txt=not a mach-o"
  expect_fail "a non-Mach-O bundle file fails verification" "$VERIFY" "$WORK/stray.zip"
  rm -rf "$WORK"
}

test_verify_rejects_failed_launch_and_missing_gmp() {
  prepare_verify_tree
  printf '#!/bin/bash\nexit 7\n' > "$BUNDLE/bin/vdf_client"
  chmod +x "$BUNDLE/bin/vdf_client"
  make_zip "$BUNDLE" "$WORK/crash.zip"
  expect_fail "failing vdf_client fails verification" "$VERIFY" "$WORK/crash.zip"

  prepare_verify_tree
  printf '#!/bin/bash\nexit 0\n' > "$BUNDLE/bin/hw_test"
  chmod +x "$BUNDLE/bin/hw_test"
  make_zip "$BUNDLE" "$WORK/silent.zip"
  expect_fail "a --version with no output fails verification" "$VERIFY" "$WORK/silent.zip"

  prepare_verify_tree
  write_otool loads vdf_client "$(printf '%s\n\t/usr/lib/libSystem.B.dylib' "vdf_client:")"
  make_zip "$BUNDLE" "$WORK/no-gmp.zip"
  expect_fail "missing bundled GMP fails verification" "$VERIFY" "$WORK/no-gmp.zip"
  rm -rf "$WORK"
}

test_stage_rewrites_homebrew_gmp_with_ftdi
test_stage_rewrites_usr_local_gmp
test_stage_refuses_missing_dependency
test_stage_refuses_basename_collision
test_stage_refuses_relative_install_name
test_stage_refuses_unreadable_rpath
test_stage_refuses_missing_rpath_dylib
test_verify_launches_with_dyld_unset
test_verify_appledouble_and_layout
test_verify_rejects_homebrew_and_usr_local
test_verify_rejects_failed_launch_and_missing_gmp
echo "all macOS bundle dylib tests passed"
