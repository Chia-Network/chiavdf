#!/usr/bin/env bash
# shellcheck disable=SC2034  # the variables are used by the scripts that source this file
# The GMP that ships in the macOS arm64 release bundle. Sourced by
# scripts/build-macos-gmp.sh, which builds it and links these lines into every
# dylib, and by scripts/check-macos-gmp-build.sh, which requires them.
#
# Homebrew's bottle uses GMP's default two-limb division in mpn_hgcd2. The
# Lehmer loops in mpz_gcdext dominate class-group squaring and composition, and
# HGCD2_DIV2_METHOD=1 is markedly faster on Apple cores. No -mcpu flag: the
# bundle has to run on every Apple Silicon Mac, and -mcpu=apple-m1 measured no
# faster than plain -O3.

GMP_VERSION=6.3.0
GMP_SHA256=a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898
GMP_CFLAGS="-O3 -DHGCD2_DIV2_METHOD=1"
GMP_CXXFLAGS="-O3"
# GMP 6.3.0's libtool links -flat_namespace when the deployment target is unset
# or 10.0-10.2. 11.0 is the first Apple Silicon release.
GMP_DEPLOYMENT_TARGET=11.0

# The lines that identify the release build. The build links them into the
# __TEXT,__chiavdf_gmp section; the check requires each of them exactly.
gmp_release_identity() {
  printf 'gmp %s sha256 %s\n' "$GMP_VERSION" "$GMP_SHA256"
  printf 'CFLAGS=%s\n' "$GMP_CFLAGS"
  printf 'CXXFLAGS=%s\n' "$GMP_CXXFLAGS"
  printf 'MACOSX_DEPLOYMENT_TARGET=%s\n' "$GMP_DEPLOYMENT_TARGET"
}
