#!/usr/bin/env bash
# Select the Developer ID Application and Installer identities to use.
#
# The import-codesign-certs action exposes valid codesigning identities as JSON
# (one entry per SHA-1 hash and common name). Renewed certificates keep the same
# common name, so select by team; pass the app hash to codesign and the installer
# common name to productsign, which resolves identities by name.
#
# The action lists its keychain with the codesigning policy, which excludes
# Developer ID Installer identities. Query the same keychain with the basic
# policy for the installer identity; productsign is what needs it.
set -euo pipefail

die() {
  echo "select-macos-signing-identities: $*" >&2
  exit 1
}

identities=${APP_IDENTITIES:-}
team=${APPLE_TEAM_ID:-}
keychain=${KEYCHAIN_NAME:-signing_temp}
output=${GITHUB_OUTPUT:-}

[ -n "$identities" ] || die "APP_IDENTITIES is required"
[ -n "$team" ] || die "APPLE_TEAM_ID is required"
[ -n "$output" ] || die "GITHUB_OUTPUT is required"
command -v jq >/dev/null 2>&1 || die "jq is required"
command -v security >/dev/null 2>&1 || die "security is required"
case "$keychain" in
  *.keychain) ;;
  *) keychain="${keychain}.keychain" ;;
esac

app_hash="$(
  jq -r --arg team "$team" '
    [ .[] |
      select(.name | startswith("Developer ID Application:")) |
      select(.name | endswith("(" + $team + ")")) |
      .hash
    ] | first // empty
  ' <<<"$identities"
)"
[ -n "$app_hash" ] || die "No Developer ID Application identity for team ${team}."

# productsign resolves identities by common name, not by SHA-1 hash.
installer_hash=""
installer_name=""
if ! installer_listing="$(
  security find-identity -v -p basic "$keychain" |
    sed -nE 's/^[[:space:]]*[0-9]+\)[[:space:]]+([0-9A-Fa-f]{40})[[:space:]]+"(.*)"$/\1 \2/p'
)"; then
  die "Failed to query keychain ${keychain} for a Developer ID Installer identity."
fi
while IFS=' ' read -r hash name; do
  case "$name" in
    "Developer ID Installer: "*"(${team})")
      installer_hash=$hash
      installer_name=$name
      break
      ;;
  esac
done <<<"$installer_listing"
if [ -z "$installer_hash" ] || [ -z "$installer_name" ]; then
  die "No Developer ID Installer identity for team ${team} in keychain ${keychain}."
fi

{
  printf 'app_identity_hash=%s\n' "$app_hash"
  printf 'installer_identity_hash=%s\n' "$installer_hash"
  printf 'installer_identity_name=%s\n' "$installer_name"
} >> "$output"
