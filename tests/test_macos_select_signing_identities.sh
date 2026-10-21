#!/usr/bin/env bash
# Hermetic tests for scripts/select-macos-signing-identities.sh.
#
# The real security command only exists on macOS, so fake it and verify that
# identities are selected by team and emitted as SHA-1 hashes.
set -euo pipefail

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
script="${root}/scripts/select-macos-signing-identities.sh"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

command -v jq >/dev/null 2>&1 || fail "jq is required for the identity-selection test"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

fakebin="${tmp}/fakebin"
out="${tmp}/output"
security_log="${tmp}/security.log"
mkdir -p "$fakebin"

cat > "${fakebin}/security" << 'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" > "${SECURITY_LOG:?}"
cat "${SECURITY_FIXTURE:?}"
EOF
chmod +x "${fakebin}/security"

cat > "${tmp}/basic.txt" << 'EOF'
  1) AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA "Developer ID Application: Chia Network Inc. (OTHERTEAM)"
  2) 1111111111111111111111111111111111111111 "Developer ID Installer: Old Installer (OTHERTEAM)"
  3) 2222222222222222222222222222222222222222 "Developer ID Installer: Chia Network Inc. (TEAM123)"
EOF

cat > "${tmp}/no-installer.txt" << 'EOF'
  1) AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA "Developer ID Application: Chia Network Inc. (TEAM123)"
EOF

app_identities='[
  {"hash":"BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB","name":"Developer ID Application: Other (OTHERTEAM)"},
  {"hash":"CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC","name":"Developer ID Application: Chia Network Inc. (TEAM123)"}
]'

run_select() {
  local app_json="$1" fixture="$2"
  APP_IDENTITIES="$app_json" \
    APPLE_TEAM_ID=TEAM123 \
    KEYCHAIN_NAME=signing_temp \
    GITHUB_OUTPUT="$out" \
    SECURITY_LOG="$security_log" \
    SECURITY_FIXTURE="$fixture" \
    PATH="${fakebin}:${PATH}" \
    bash "$script"
}

: > "$out"
run_select "$app_identities" "${tmp}/basic.txt"
grep -Fxq 'app_identity_hash=CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC' "$out" \
  || fail "the app identity for the requested team was not selected: $(cat "$out")"
grep -Fxq 'installer_identity_hash=2222222222222222222222222222222222222222' "$out" \
  || fail "the installer identity for the requested team was not selected: $(cat "$out")"
grep -Fxq 'installer_identity_name=Developer ID Installer: Chia Network Inc. (TEAM123)' "$out" \
  || fail "the installer common name for the requested team was not selected: $(cat "$out")"
grep -Fxq 'find-identity -v -p basic signing_temp.keychain' "$security_log" \
  || fail "the installer identity was not queried from the imported keychain: $(cat "$security_log")"

assert_fail() {
  local desc="$1" want="$2" app_json="$3" fixture="$4"
  local output status
  : > "$out"
  set +e
  output="$(run_select "$app_json" "$fixture" 2>&1)"
  status=$?
  set -e
  if [ "$status" -eq 0 ]; then
    fail "$desc (expected non-zero)"
  fi
  printf '%s\n' "$output" | grep -Fq "$want" || fail "$desc: expected [$want], got [$output]"
  echo "ok: $desc"
}

assert_fail "missing app identity for the team" \
  "No Developer ID Application identity for team TEAM123." '[]' "${tmp}/basic.txt"
assert_fail "missing installer identity for the team" \
  "No Developer ID Installer identity for team TEAM123 in keychain signing_temp.keychain." \
  "$app_identities" "${tmp}/no-installer.txt"

echo "macOS signing identity selection checks passed"
