#!/usr/bin/env bash
# Exercises the macOS release mode/artifact-name derivation without macOS tools.
set -euo pipefail

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
script="${root}/scripts/package-macos-bundle.sh"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

run_plan() {
  env -i PATH="$PATH" \
    MACOS_ARCH=arm64 \
    RELEASE_TAG=v1.2.3 \
    "$@" \
    bash "$script" --print-plan
}

assert_has() {
  local output="$1" want="$2" desc="$3"
  printf '%s\n' "$output" | grep -Fxq "$want" || fail "$desc: missing [$want] in [$output]"
}

assert_plan_fails() {
  local desc="$1" want="$2"
  shift 2
  local output status
  set +e
  output="$(run_plan "$@" 2>&1)"
  status=$?
  set -e
  if [ "$status" -eq 0 ]; then
    fail "$desc (expected non-zero)"
  fi
  printf '%s\n' "$output" | grep -Fq "$want" || fail "$desc: expected [$want], got [$output]"
  echo "ok: $desc"
}

output="$(run_plan)"
assert_has "$output" "mode=adhoc" "non-release defaults to ad-hoc"
assert_has "$output" "asset_name=chiavdf-v1.2.3-macos-arm64-unsigned.zip" "ad-hoc zip name"
assert_has "$output" "pkg_name=chiavdf-v1.2.3-macos-arm64-unsigned.pkg" "ad-hoc pkg name"
assert_has "$output" "bundle_status=ad-hoc signed (no Developer ID; codesign -s - after install_name_tool)" "ad-hoc bundle status"

output="$(run_plan \
  IS_RELEASE=true \
  REQUEST_NOTARIZE=false \
  HAS_APPLE_SECRET=true \
  HAS_INSTALLER_SECRET=true \
  HAS_NOTARIZE_SECRETS=true \
  APP_SIGNING_IDENTITY=app-hash \
  INSTALLER_SIGNING_IDENTITY=installer-name)"
assert_has "$output" "mode=developer-id" "release without notarization stays Developer ID"
assert_has "$output" "asset_name=chiavdf-v1.2.3-macos-arm64.zip" "Developer ID zip name"
assert_has "$output" "pkg_name=chiavdf-v1.2.3-macos-arm64.pkg" "Developer ID pkg name"
assert_has "$output" "bundle_status=signed (notarization not requested)" "Developer ID bundle status"
assert_has "$output" "pkg_status=signed (Developer ID Installer; notarization not requested)" "Developer ID pkg status"

output="$(run_plan \
  IS_RELEASE=false \
  REQUEST_NOTARIZE=true \
  HAS_APPLE_SECRET=true \
  HAS_INSTALLER_SECRET=true \
  HAS_NOTARIZE_SECRETS=true \
  APP_SIGNING_IDENTITY=app-hash \
  INSTALLER_SIGNING_IDENTITY=installer-name \
  APPLE_NOTARIZE_USERNAME=user \
  APPLE_NOTARIZE_PASSWORD=pass \
  APPLE_TEAM_ID=TEAM123)"
assert_has "$output" "mode=notarized" "manual notarization request selects notarized mode"
assert_has "$output" "pkg_name=chiavdf-v1.2.3-macos-arm64.pkg" "notarized pkg name"
assert_has "$output" "pkg_status=signed + notarized + stapled (Developer ID Installer)" "notarized pkg status"

output="$(run_plan \
  IS_RELEASE=true \
  REQUEST_NOTARIZE=true \
  HAS_APPLE_SECRET=true \
  HAS_INSTALLER_SECRET=true \
  HAS_NOTARIZE_SECRETS=true \
  APP_SIGNING_IDENTITY=app-hash \
  INSTALLER_SIGNING_IDENTITY=installer-name \
  APPLE_NOTARIZE_USERNAME=user \
  APPLE_NOTARIZE_PASSWORD=pass \
  APPLE_TEAM_ID=TEAM123)"
assert_has "$output" "mode=notarized" "final release with notarization selects notarized mode"

assert_plan_fails "release without app secrets" \
  "require Apple signing certificate secrets" \
  IS_RELEASE=true REQUEST_NOTARIZE=false HAS_APPLE_SECRET=false
assert_plan_fails "notarization without installer secrets" \
  "require the macOS installer identity" \
  IS_RELEASE=false REQUEST_NOTARIZE=true HAS_APPLE_SECRET=true HAS_INSTALLER_SECRET=false HAS_NOTARIZE_SECRETS=true
assert_plan_fails "release without notarization secrets" \
  "require notarization secrets" \
  IS_RELEASE=true REQUEST_NOTARIZE=false HAS_APPLE_SECRET=true HAS_INSTALLER_SECRET=true HAS_NOTARIZE_SECRETS=false \
  APP_SIGNING_IDENTITY=app-hash INSTALLER_SIGNING_IDENTITY=installer-name
assert_plan_fails "Developer ID build without an app hash" \
  "APP_SIGNING_IDENTITY is required" \
  IS_RELEASE=true REQUEST_NOTARIZE=false HAS_APPLE_SECRET=true HAS_INSTALLER_SECRET=true HAS_NOTARIZE_SECRETS=true \
  APP_SIGNING_IDENTITY= INSTALLER_SIGNING_IDENTITY=installer-name
assert_plan_fails "Developer ID build without an installer name" \
  "INSTALLER_SIGNING_IDENTITY (the installer common name) is required" \
  IS_RELEASE=true REQUEST_NOTARIZE=false HAS_APPLE_SECRET=true HAS_INSTALLER_SECRET=true HAS_NOTARIZE_SECRETS=true \
  APP_SIGNING_IDENTITY=app-hash INSTALLER_SIGNING_IDENTITY=
assert_plan_fails "notarization without credentials" \
  "APPLE_NOTARIZE_USERNAME is required" \
  IS_RELEASE=false REQUEST_NOTARIZE=true HAS_APPLE_SECRET=true HAS_INSTALLER_SECRET=true HAS_NOTARIZE_SECRETS=true \
  APP_SIGNING_IDENTITY=app-hash INSTALLER_SIGNING_IDENTITY=installer-name \
  APPLE_NOTARIZE_USERNAME= APPLE_NOTARIZE_PASSWORD=pass APPLE_TEAM_ID=TEAM123

echo "macOS bundle release plan checks passed"
