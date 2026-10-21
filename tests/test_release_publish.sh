#!/usr/bin/env bash
# Truth table for release policy and the single GitHub publish path.
set -euo pipefail

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
policy="${root}/.github/actions/release-policy/policy.sh"
publish="${root}/.github/actions/publish-github-release/publish.sh"
manifest_dir="${root}/.github/actions/publish-github-release/manifests"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

run_policy() {
  local out
  out=$(mktemp)
  EVENT_NAME=$1 \
    EVENT_ACTION=$2 \
    PRERELEASE_JSON=$3 \
    WINDOWS_SIGN_JSON=$4 \
    MACOS_NOTARIZE_JSON=$5 \
    GITHUB_OUTPUT=$out \
    bash "$policy"
  cat "$out"
  rm -f "$out"
}

assert_policy() {
  local label=$1 expected_final=$2 expected_sign=$3 expected_notarize=$4
  shift 4
  local out
  out=$(run_policy "$@")
  got_final=$(printf '%s\n' "$out" | awk -F= '$1 == "is_final" { print $2 }')
  got_sign=$(printf '%s\n' "$out" | awk -F= '$1 == "sign_windows" { print $2 }')
  got_notarize=$(printf '%s\n' "$out" | awk -F= '$1 == "notarize_macos" { print $2 }')
  if [ "$got_final" != "$expected_final" ] || [ "$got_sign" != "$expected_sign" ] || [ "$got_notarize" != "$expected_notarize" ]; then
    fail "${label}: expected final=${expected_final} sign=${expected_sign} notarize=${expected_notarize}, got final=${got_final} sign=${got_sign} notarize=${got_notarize}"
  fi
}

assert_policy "final release" true true true release published false null null
assert_policy "prerelease" false false false release published true null null
assert_policy "prerelease ignores windows input" false false false release published true true true
assert_policy "missing prerelease flag" false false false release published null null null
assert_policy "string false is not boolean false" false false false release published '"false"' null null
assert_policy "release edited" false false false release edited false null null
assert_policy "push" false false false push "" null null null
assert_policy "dispatch signs windows only" false true false workflow_dispatch workflow_dispatch null true null
assert_policy "dispatch notarizes only" false false true workflow_dispatch workflow_dispatch null null true
assert_policy "dispatch string true" false true true workflow_dispatch workflow_dispatch null '"true"' '"true"'
assert_policy "dispatch boolean false" false false false workflow_dispatch workflow_dispatch null false false

tag=1.2.3
assets=$(mktemp -d)
calls=$(mktemp)
output=$(mktemp)
gh_bin=$(mktemp -d)
cat > "${gh_bin}/gh" << 'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$GH_CALLS"
exit "${GH_EXIT:-0}"
EOF
chmod +x "${gh_bin}/gh"

asset_names=()
while IFS= read -r line || [ -n "$line" ]; do
  line=${line%%#*}
  line=${line//[[:space:]]/}
  [ -z "$line" ] && continue
  asset_names+=("${line//\{tag\}/$tag}")
done < <(cat "${manifest_dir}/chiavdf.txt" "${manifest_dir}/chiavdfc.txt")

write_tree() {
  local dest=$1
  rm -rf "$dest"
  mkdir -p "$dest/debian" "$dest/macos" "$dest/windows" "$dest/c"
  local name payload
  for name in "${asset_names[@]}"; do
    case "$name" in
      *.sha256) continue ;;
    esac
    case "$name" in
      *.deb) payload="${dest}/debian/${name}" ;;
      chiavdfc-*) payload="${dest}/c/${name}" ;;
      *windows*|*win64*) payload="${dest}/windows/${name}" ;;
      *) payload="${dest}/macos/${name}" ;;
    esac
    printf 'asset %s\n' "$name" > "$payload"
  done
  for name in "${asset_names[@]}"; do
    case "$name" in
      *.sha256)
        payload=${name%.sha256}
        find "$dest" -type f -name "$payload" -exec sha256sum {} \; | awk '{print $1}' > "${dest}/macos/${name}"
        # Windows sidecars live next to their zip. Keep one copy; basename search allows either directory.
        if [[ "$name" == *windows* || "$name" == *win64* ]]; then
          mv "${dest}/macos/${name}" "${dest}/windows/${name}"
        fi
        ;;
    esac
  done
}

run_publish() {
  local final=$1 manifests=$2 dir=$3
  : > "$calls"
  : > "$output"
  set +e
  IS_FINAL=$final \
    RELEASE_TAG=$tag \
    ASSETS_DIR=$dir \
    MANIFESTS=$manifests \
    GLUE_URL=https://glue.example \
    GITHUB_OUTPUT=$output \
    GH_CALLS=$calls \
    GH_EXIT=${GH_EXIT:-0} \
    PATH="${gh_bin}:${PATH}" \
    bash "$publish"
  status=$?
  set -e
}

assert_no_gh() {
  if [ -s "$calls" ]; then
    fail "$1: gh was invoked: $(cat "$calls")"
  fi
}

write_tree "$assets"
run_publish false "chiavdf,chiavdfc" "$assets"
if [ "$status" -ne 0 ]; then
  fail "prerelease publish should skip, status ${status}"
fi
assert_no_gh "prerelease"
if [ "$(awk -F= '$1 == "published" { print $2 }' "$output")" != "false" ]; then
  fail "prerelease should record published=false"
fi

run_publish "" "chiavdf,chiavdfc" "$assets"
if [ "$status" -ne 0 ]; then
  fail "empty IS_FINAL should skip, status ${status}"
fi
assert_no_gh "ambiguous release"

run_publish true "chiavdf,chiavdfc" "$assets"
if [ "$status" -ne 0 ]; then
  fail "final publish failed with status ${status}"
fi
if [ "$(wc -l < "$calls")" -ne 1 ]; then
  fail "final publish should call gh once, got: $(cat "$calls")"
fi
if ! grep -q -- '--clobber' "$calls"; then
  fail "final publish did not pass --clobber: $(cat "$calls")"
fi
if ! grep -q -- "release upload --clobber ${tag} " "$calls"; then
  fail "final publish did not target ${tag}: $(cat "$calls")"
fi
for name in "${asset_names[@]}"; do
  if ! grep -F -q -- "/${name}" "$calls" && ! grep -F -q -- " ${name}" "$calls"; then
    fail "uploaded set is missing ${name}"
  fi
done
if ! grep -F -q '"windows_amd64":"chiavdf-win64.zip"' "$output"; then
  fail "chiavdf glue payload missing: $(cat "$output")"
fi
if ! grep -F -q "glue_chiavdfc={\"release_version\":\"${tag}\"}" "$output"; then
  fail "chiavdfc glue payload missing: $(cat "$output")"
fi

# Re-run after a successful upload still clobbers. The script has no memory of the first call.
run_publish true "chiavdf,chiavdfc" "$assets"
if [ "$status" -ne 0 ] || ! grep -q -- '--clobber' "$calls"; then
  fail "re-run should upload again with --clobber"
fi

# One missing C library archive blocks the client assets too.
rm -f "${assets}/c/"chiavdfc-windows-intel.zip
run_publish true "chiavdf,chiavdfc" "$assets"
if [ "$status" -eq 0 ]; then
  fail "missing asset should fail the publish"
fi
assert_no_gh "missing asset"

# A bad digest fails before gh, same as a missing file.
write_tree "$assets"
printf 'deadbeef\n' > "${assets}/windows/chiavdf-win64.zip.sha256"
run_publish true "chiavdf,chiavdfc" "$assets"
if [ "$status" -eq 0 ]; then
  fail "bad sha256 should fail the publish"
fi
assert_no_gh "bad sha256"

# Two copies of a required basename are ambiguous.
write_tree "$assets"
cp "${assets}/windows/chiavdf-win64.zip" "${assets}/debian/chiavdf-win64.zip"
run_publish true "chiavdf,chiavdfc" "$assets"
if [ "$status" -eq 0 ]; then
  fail "duplicate basename should fail the publish"
fi
assert_no_gh "duplicate basename"

# Unsafe tags never reach gh.
write_tree "$assets"
: > "$calls"
: > "$output"
set +e
IS_FINAL=true RELEASE_TAG='1.2.3;rm' ASSETS_DIR=$assets MANIFESTS='chiavdf,chiavdfc' \
  GLUE_URL=https://glue.example GITHUB_OUTPUT=$output GH_CALLS=$calls \
  PATH="${gh_bin}:${PATH}" bash "$publish"
status=$?
set -e
if [ "$status" -eq 0 ]; then
  fail "unsafe tag should fail"
fi
assert_no_gh "unsafe tag"

# gh failing mid-upload does not record a successful publish.
write_tree "$assets"
: > "$calls"
: > "$output"
set +e
IS_FINAL=true RELEASE_TAG=$tag ASSETS_DIR=$assets MANIFESTS='chiavdf,chiavdfc' \
  GLUE_URL=https://glue.example GITHUB_OUTPUT=$output GH_CALLS=$calls GH_EXIT=1 \
  PATH="${gh_bin}:${PATH}" bash "$publish"
status=$?
set -e
if [ "$status" -eq 0 ]; then
  fail "gh failure should fail the publish"
fi
if grep -q '^published=true$' "$output"; then
  fail "gh failure should not record published=true"
fi
if grep -q 'glue_chiavdf=' "$output" || grep -q 'glue_chiavdfc=' "$output"; then
  fail "gh failure should not record glue payloads"
fi

job_field() {
  local file=$1 job=$2 key=$3
  awk -v job="  ${job}:" -v key="$key" '
    $0 == job { in_job = 1; next }
    in_job && /^  [A-Za-z0-9-]+:/ { exit }
    in_job && $0 ~ ("^[[:space:]]+" key ":") {
      sub("^[[:space:]]+" key ":[[:space:]]*", "")
      print
      exit
    }
  ' "$file"
}

# publish-release waits on the jobs that produce or validate release assets.
# ASAN/TSAN stay in their own matrix and are not part of that set.
# shellcheck disable=SC2016
assert_publish_dependencies() {
  packages="${root}/.github/workflows/build-packages.yml"
  client="${root}/.github/workflows/build-client.yml"

  needs=$(job_field "$packages" publish-release needs)
  if [ "$needs" != "[release-policy, package, c-libraries, release-publish-checks]" ]; then
    fail "publish-release needs ${needs:-<missing>}"
  fi

  publish_if=$(awk '
    $0 == "  publish-release:" { in_job = 1; next }
    in_job && /^  [A-Za-z0-9-]+:/ { exit }
    in_job { print }
  ' "$packages")
  if ! printf '%s\n' "$publish_if" | grep -q "needs.package.result == 'success'"; then
    fail "publish-release does not require the package job"
  fi
  if printf '%s\n' "$publish_if" | grep -Eq 'needs\.(sanitize|build)(\.|[[:space:]])'; then
    fail "publish-release still depends on a sanitizer or mixed build job"
  fi

  pkg_configs=$(job_field "$packages" package configs)
  san_configs=$(job_field "$packages" sanitize configs)
  if [ "$pkg_configs" != "'[\"optimized=1\"]'" ]; then
    fail "package configs are ${pkg_configs:-<missing>}"
  fi
  case "$san_configs" in
    *TSAN=1*) ;;
    *) fail "sanitize configs missing TSAN: ${san_configs:-<missing>}" ;;
  esac
  case "$san_configs" in
    *ASAN=1*) ;;
    *) fail "sanitize configs missing ASAN: ${san_configs:-<missing>}" ;;
  esac
  case "$san_configs" in
    *optimized=1*) fail "sanitize configs include the packaging build" ;;
  esac

  for job in package sanitize; do
    uses=$(job_field "$packages" "$job" uses)
    if [ "$uses" != "./.github/workflows/build-client.yml" ]; then
      fail "${job} does not call the client matrix workflow: ${uses:-<missing>}"
    fi
    if awk -v job="  ${job}:" '
      $0 == job { in_job = 1; next }
      in_job && /^  [A-Za-z0-9-]+:/ { exit }
      in_job && /^    if:/ { found = 1 }
      END { exit !found }
    ' "$packages"; then
      fail "${job} filters matrix entries with a job if"
    fi
  done

  if ! grep -q 'is_final: ${{ steps.policy.outputs.is_final }}' "$packages"; then
    fail "release-policy output is_final was renamed"
  fi
  if ! grep -q 'sign_windows: ${{ steps.policy.outputs.sign_windows }}' "$packages"; then
    fail "release-policy output sign_windows was renamed"
  fi
  if ! grep -q 'is_final: ${{ needs.release-policy.outputs.is_final }}' "$packages"; then
    fail "package does not pass needs.release-policy.outputs.is_final"
  fi
  if ! grep -q 'sign_windows: ${{ needs.release-policy.outputs.sign_windows }}' "$packages"; then
    fail "package does not pass needs.release-policy.outputs.sign_windows"
  fi

  if grep -q 'config: \[optimized=1, TSAN=1, ASAN=1\]' "$packages" "$client"; then
    fail "artifact and sanitizer configs share one matrix"
  fi
  if ! grep -q 'config: ${{ fromJSON(inputs.configs) }}' "$client"; then
    fail "client matrix is not selected by the caller"
  fi
}

assert_windows_signing_wiring() {
  client="${root}/.github/workflows/build-client.yml"

  if [ "$(job_field "$client" sign-windows environment)" != "windows-code-signing" ]; then
    fail "Windows signing job does not use the windows-code-signing environment"
  fi
  if [ "$(job_field "$client" sign-windows id-token)" != "write" ]; then
    fail "Windows signing job does not request id-token: write"
  fi
  if ! grep -Fq 'AZURE_SIGNING_CLIENT_ID' "$client" || ! grep -Fq 'AZURE_SIGNING_TENANT_ID' "$client"; then
    fail "Windows signing job does not read the Azure signing secrets"
  fi
  if [ "$(grep -Fc 'name: windows-amd64-binaries' "$client")" -lt 2 ]; then
    fail "Windows build artifact and sign job do not share windows-amd64-binaries"
  fi
  if ! grep -Fq 'Chia-Network/actions/sign/azure-windows@main' "$client"; then
    fail "Windows signing does not use the shared Azure Windows Code Sign action"
  fi
  if ! grep -Fq "REQUIRE_SIGNATURES: 'false'" "$client"; then
    fail "unsigned Windows packaging does not disable signature verification"
  fi
  if ! grep -Fq "REQUIRE_SIGNATURES: 'true'" "$client"; then
    fail "signed Windows packaging does not require signature verification"
  fi
  if ! grep -Fq 'IS_RELEASE: ${{ inputs.is_final }}' "$client"; then
    fail "IS_RELEASE does not read inputs.is_final"
  fi
  if ! grep -Fq "inputs.sign_windows == 'true'" "$client"; then
    fail "sign steps do not check inputs.sign_windows"
  fi
  if grep -E 'sign_windows|REQUIRE_SIGNATURES:|IS_RELEASE:' "$client" | grep -q 'github.event_name'; then
    fail "signing or IS_RELEASE still keys off github.event_name"
  fi
  if grep -Eq 'SM_(API_KEY|CLIENT_CERT_FILE_B64|CODE_SIGNING_CERT_SHA1_HASH|CERTKEY_ALIAS)' "$client"; then
    fail "client workflow still references DigiCert SM_* secrets"
  fi
}

assert_publish_dependencies
assert_windows_signing_wiring

rm -rf "$assets" "$gh_bin"
rm -f "$calls" "$output"
echo "release publish checks passed"
