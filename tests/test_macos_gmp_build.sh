#!/usr/bin/env bash
# Exercises scripts/check-macos-gmp-build.sh with fake segedit and otool.
# The macOS arm64 job runs the real tools against the shipped zip; this runs on
# Linux. Each fake dylib is a text file: header lines for otool, then
# "SECTION:" and the contents of its __TEXT,__chiavdf_gmp section.
# It also covers scripts/build-macos-gmp.sh with mocked uname, sysctl and shasum.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CHECK="$ROOT_DIR/scripts/check-macos-gmp-build.sh"
BUILD="$ROOT_DIR/scripts/build-macos-gmp.sh"

# shellcheck source=../scripts/macos-gmp-release.sh disable=SC1091
source "$ROOT_DIR/scripts/macos-gmp-release.sh"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

expect_pass() {
  local desc="$1"
  shift
  local output
  if ! output="$("$@" 2>&1)"; then
    printf '%s\n' "$output" >&2
    fail "$desc (expected success)"
  fi
  echo "ok: $desc"
}

expect_fail() {
  local desc="$1"
  local want="$2"
  shift 2
  local output status
  set +e
  output="$("$@" 2>&1)"
  status=$?
  set -e
  if [ "$status" -eq 0 ]; then
    printf '%s\n' "$output" >&2
    fail "$desc (expected non-zero)"
  fi
  if ! grep -Fq -- "$want" <<<"$output"; then
    printf '%s\n' "$output" >&2
    fail "$desc (expected output containing [$want])"
  fi
  echo "ok: $desc"
}

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
REAL_UNAME="$(command -v uname)"
mkdir -p "$WORK/mocks"

cat > "$WORK/mocks/segedit" <<'EOF'
#!/bin/bash
# segedit DYLIB -extract __TEXT __chiavdf_gmp OUT
set -euo pipefail
[ "$2" = -extract ] && [ "$3" = __TEXT ] && [ "$4" = __chiavdf_gmp ] || exit 2
grep -q '^SECTION:$' "$1" || { echo "section not found" >&2; exit 1; }
sed '1,/^SECTION:$/d' "$1" > "$5"
EOF

cat > "$WORK/mocks/otool" <<'EOF'
#!/bin/bash
set -euo pipefail
field() { sed -n "s/^$1=//p" "$2" | head -n 1; }
case "$1" in
  -hv)
    printf 'Mach header\n'
    printf 'MH_MAGIC_64 ARM64 ALL 0x00 DYLIB 14 1240 NOUNDEFS DYLDLINK %s\n' "$(field NAMESPACE "$2")"
    ;;
  -l)
    printf 'Load command 8\n      cmd LC_BUILD_VERSION\n  cmdsize 32\n platform 1\n    minos %s\n      sdk 26.0\n' \
      "$(field MINOS "$2")"
    ;;
  *)
    echo "unsupported otool flag: $1" >&2
    exit 1
    ;;
esac
EOF
chmod +x "$WORK/mocks/segedit" "$WORK/mocks/otool"
export PATH="$WORK/mocks:$PATH"

# write_dylib PATH [section-file|none] [namespace] [minos]
write_dylib() {
  local path="$1" section="${2:-}" namespace="${3:-TWOLEVEL}" minos="${4:-$GMP_DEPLOYMENT_TARGET}"
  mkdir -p "$(dirname "$path")"
  {
    printf 'FAKE-DYLIB\nNAMESPACE=%s\nMINOS=%s\n' "$namespace" "$minos"
    if [ "$section" != none ]; then
      printf 'SECTION:\n'
      if [ -n "$section" ]; then
        cat "$section"
      else
        gmp_release_identity
        printf 'compiler=Apple clang version 17.0.0\n'
      fi
    fi
  } > "$path"
}

# make_zip NAME: zip the staged tree $WORK/NAME into $WORK/NAME.zip
make_zip() {
  (cd "$WORK/$1" && python3 -c 'import os, sys, zipfile
with zipfile.ZipFile(sys.argv[1], "w") as z:
    for d, _, fs in os.walk("."):
        for f in fs:
            z.write(os.path.join(d, f))' "$WORK/$1.zip")
}

libexec() { printf '%s/%s/chiavdf-macos/libexec/chiavdf' "$WORK" "$1"; }

# A good bundle.
write_dylib "$(libexec good)/libgmp.10.dylib"
write_dylib "$(libexec good)/libgmpxx.4.dylib"
write_dylib "$(libexec good)/libft4222.dylib" none
make_zip good
expect_pass "good bundle" bash "$CHECK" "$WORK/good.zip"

# libgmpxx without the section (a Homebrew dylib).
write_dylib "$(libexec nosection)/libgmp.10.dylib"
write_dylib "$(libexec nosection)/libgmpxx.4.dylib" none
make_zip nosection
expect_fail "missing section" "no __TEXT,__chiavdf_gmp section" bash "$CHECK" "$WORK/nosection.zip"

# Wrong CFLAGS (an -mcpu build).
gmp_release_identity | sed 's/^CFLAGS=.*/& -mcpu=apple-m1/' > "$WORK/cflags.txt"
write_dylib "$(libexec cflags)/libgmp.10.dylib" "$WORK/cflags.txt"
write_dylib "$(libexec cflags)/libgmpxx.4.dylib" "$WORK/cflags.txt"
make_zip cflags
expect_fail "wrong CFLAGS" "missing [CFLAGS=$GMP_CFLAGS]" bash "$CHECK" "$WORK/cflags.zip"

# libgmp is good, libgmpxx is built with the wrong CFLAGS.
write_dylib "$(libexec mixed)/libgmp.10.dylib"
write_dylib "$(libexec mixed)/libgmpxx.4.dylib" "$WORK/cflags.txt"
make_zip mixed
expect_fail "good libgmp, libgmpxx with wrong flags" "missing [CFLAGS=$GMP_CFLAGS]" bash "$CHECK" "$WORK/mixed.zip"

# Another tarball (wrong hash on the identity line).
gmp_release_identity | sed "s/$GMP_SHA256/$(printf '0%.0s' {1..64})/" > "$WORK/hash.txt"
write_dylib "$(libexec hash)/libgmp.10.dylib" "$WORK/hash.txt"
write_dylib "$(libexec hash)/libgmpxx.4.dylib"
make_zip hash
expect_fail "wrong tarball hash" "missing [gmp $GMP_VERSION sha256 $GMP_SHA256]" bash "$CHECK" "$WORK/hash.zip"

# Fewer than two libraries.
write_dylib "$(libexec onelib)/libgmp.10.dylib"
make_zip onelib
expect_fail "only libgmp" "found 1 and 0" bash "$CHECK" "$WORK/onelib.zip"

# Two libraries, but not one of each.
write_dylib "$(libexec twogmp)/libgmp.10.dylib"
write_dylib "$(libexec twogmp)/libgmp.11.dylib"
make_zip twogmp
expect_fail "two libgmp, no libgmpxx" "found 2 and 0" bash "$CHECK" "$WORK/twogmp.zip"

# A flat-namespace build.
write_dylib "$(libexec flat)/libgmp.10.dylib" "" FLAT_NAMESPACE
write_dylib "$(libexec flat)/libgmpxx.4.dylib"
make_zip flat
expect_fail "flat namespace" "not a two-level namespace dylib" bash "$CHECK" "$WORK/flat.zip"

# The wrong deployment target.
write_dylib "$(libexec minos)/libgmp.10.dylib"
write_dylib "$(libexec minos)/libgmpxx.4.dylib" "" TWOLEVEL 10.15
make_zip minos
expect_fail "wrong minos" "has minos [10.15], expected $GMP_DEPLOYMENT_TARGET" bash "$CHECK" "$WORK/minos.zip"

# Libraries outside libexec/chiavdf are not the bundled ones.
write_dylib "$WORK/elsewhere/chiavdf-macos/lib/libgmp.10.dylib"
write_dylib "$WORK/elsewhere/chiavdf-macos/lib/libgmpxx.4.dylib"
make_zip elsewhere
expect_fail "libraries outside libexec/chiavdf" "found 0 and 0" bash "$CHECK" "$WORK/elsewhere.zip"

# Mocks for scripts/build-macos-gmp.sh, which only runs on macOS arm64.
mkdir -p "$WORK/mocks-build"
cat > "$WORK/mocks-build/uname" <<EOF
#!/bin/bash
case "\$1" in
  -s) echo Darwin ;;
  -m) echo arm64 ;;
  *) exec "$REAL_UNAME" "\$@" ;;
esac
EOF
cat > "$WORK/mocks-build/sysctl" <<'EOF'
#!/bin/bash
echo 1
EOF
cat > "$WORK/mocks-build/shasum" <<'EOF'
#!/bin/bash
echo mock-shasum-called >&2
exit 1
EOF
chmod +x "$WORK/mocks-build/uname" "$WORK/mocks-build/sysctl" "$WORK/mocks-build/shasum"
BUILD_PATH="$WORK/mocks-build:$PATH"

# GMP_TARBALL is already the build dir's own copy: skip the cp, still hash.
mkdir -p "$WORK/gmpdir"
printf 'dummy tarball\n' > "$WORK/gmpdir/gmp-$GMP_VERSION.tar.xz"
expect_fail "GMP_TARBALL is the build dir's own copy" "mock-shasum-called" \
  env PATH="$BUILD_PATH" GMP_BUILD_DIR="$WORK/gmpdir" \
  GMP_TARBALL="$WORK/gmpdir/gmp-$GMP_VERSION.tar.xz" bash "$BUILD" "$WORK/prefix"

# GMP_TARBALL is a symlink to the build dir's own copy: skip the cp too.
mkdir -p "$WORK/gmpdir3"
printf 'dummy tarball\n' > "$WORK/gmpdir3/gmp-$GMP_VERSION.tar.xz"
printf 'dummy tarball\n' > "$WORK/symlink-reference"
ln -s "$WORK/gmpdir3/gmp-$GMP_VERSION.tar.xz" "$WORK/link-to-tarball"
expect_fail "GMP_TARBALL is a symlink to the build dir's own copy" "mock-shasum-called" \
  env PATH="$BUILD_PATH" GMP_BUILD_DIR="$WORK/gmpdir3" \
  GMP_TARBALL="$WORK/link-to-tarball" bash "$BUILD" "$WORK/prefix"
if [ ! -f "$WORK/gmpdir3/gmp-$GMP_VERSION.tar.xz" ] || [ -L "$WORK/gmpdir3/gmp-$GMP_VERSION.tar.xz" ]; then
  fail "symlinked GMP_TARBALL replaced the build dir's tarball"
fi
if ! cmp -s "$WORK/symlink-reference" "$WORK/gmpdir3/gmp-$GMP_VERSION.tar.xz"; then
  fail "symlinked GMP_TARBALL clobbered or truncated the build dir's tarball"
fi

# GMP_TARBALL spelled relatively (with ./ and ..) resolves to the same file.
mkdir -p "$WORK/gmpdir4"
printf 'dummy tarball\n' > "$WORK/gmpdir4/gmp-$GMP_VERSION.tar.xz"
printf 'dummy tarball\n' > "$WORK/relative-reference"
(cd "$WORK" && expect_fail "GMP_TARBALL is a relative path to the build dir's own copy" "mock-shasum-called" \
  env PATH="$BUILD_PATH" GMP_BUILD_DIR="$WORK/gmpdir4" \
  GMP_TARBALL=./gmpdir4/../gmpdir4/gmp-"$GMP_VERSION".tar.xz bash "$BUILD" "$WORK/prefix")
if [ ! -f "$WORK/gmpdir4/gmp-$GMP_VERSION.tar.xz" ] || [ -L "$WORK/gmpdir4/gmp-$GMP_VERSION.tar.xz" ]; then
  fail "relative GMP_TARBALL replaced the build dir's tarball"
fi
if ! cmp -s "$WORK/relative-reference" "$WORK/gmpdir4/gmp-$GMP_VERSION.tar.xz"; then
  fail "relative GMP_TARBALL clobbered or truncated the build dir's tarball"
fi

# GMP_TARBALL is a different file: copy it in, then reach the hash check.
printf 'dummy tarball\n' > "$WORK/source-tarball"
mkdir -p "$WORK/gmpdir2"
expect_fail "GMP_TARBALL copied into the build dir" "mock-shasum-called" \
  env PATH="$BUILD_PATH" GMP_BUILD_DIR="$WORK/gmpdir2" \
  GMP_TARBALL="$WORK/source-tarball" bash "$BUILD" "$WORK/prefix"
if ! cmp -s "$WORK/source-tarball" "$WORK/gmpdir2/gmp-$GMP_VERSION.tar.xz"; then
  fail "GMP_TARBALL was not copied into the build dir"
fi

# A comma in the build dir would be split by the linker.
expect_fail "comma in GMP_BUILD_DIR" "must not contain a comma" \
  env PATH="$BUILD_PATH" GMP_BUILD_DIR="$WORK/with,comma" bash "$BUILD" "$WORK/prefix"

echo "check-macos-gmp-build tests passed"
