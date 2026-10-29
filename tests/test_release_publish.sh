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
    MACOS_NOTARIZE_JSON=$4 \
    GITHUB_OUTPUT=$out \
    bash "$policy"
  cat "$out"
  rm -f "$out"
}

assert_policy() {
  local label=$1 expected_final=$2 expected_notarize=$3
  shift 3
  local out
  out=$(run_policy "$@")
  got_final=$(printf '%s\n' "$out" | awk -F= '$1 == "is_final" { print $2 }')
  got_notarize=$(printf '%s\n' "$out" | awk -F= '$1 == "notarize_macos" { print $2 }')
  if [ "$got_final" != "$expected_final" ] || [ "$got_notarize" != "$expected_notarize" ]; then
    fail "${label}: expected final=${expected_final} notarize=${expected_notarize}, got final=${got_final} notarize=${got_notarize}"
  fi
}

assert_policy "final release" true true release published false null
assert_policy "prerelease" false false release published true null
assert_policy "missing prerelease flag" false false release published null null
assert_policy "string false is not boolean false" false false release published '"false"' null
assert_policy "release edited" false false release edited false null
assert_policy "push" false false push "" null null
assert_policy "dispatch notarizes" false true workflow_dispatch workflow_dispatch null true
assert_policy "dispatch string true" false true workflow_dispatch workflow_dispatch null '"true"'
assert_policy "dispatch boolean false" false false workflow_dispatch workflow_dispatch null false

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
  fail "chiavdf glue payload missing the portable zip: $(cat "$output")"
fi
if ! grep -F -q '"windows_amd64_installer":"chiavdf-win64-setup.exe"' "$output"; then
  fail "chiavdf glue payload missing the installer: $(cat "$output")"
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
    in_job && /^  [A-Za-z0-9_.-]+:/ { exit }
    in_job && $0 ~ ("^[[:space:]]+" key ":") {
      sub("^[[:space:]]+" key ":[[:space:]]*", "")
      print
      exit
    }
  ' "$file"
}

job_block() {
  local file=$1 job=$2
  awk -v job="  ${job}:" '
    $0 == job { in_job = 1; print; next }
    in_job && /^  [A-Za-z0-9_.-]+:/ { exit }
    in_job { print }
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

  publish_block=$(job_block "$packages" publish-release)
  if ! printf '%s\n' "$publish_block" | grep -q "needs.package.result == 'success'"; then
    fail "publish-release does not require the package job"
  fi
  if printf '%s\n' "$publish_block" | grep -Eq 'needs\.(sanitize|build)(\.|[[:space:]])'; then
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
    if printf '%s\n' "$(job_block "$packages" "$job")" | grep -q '^    if:'; then
      fail "${job} filters matrix entries with a job if"
    fi
  done

  if ! grep -q 'is_final: ${{ steps.policy.outputs.is_final }}' "$packages"; then
    fail "release-policy output is_final was renamed"
  fi
  if ! grep -q 'is_final: ${{ needs.release-policy.outputs.is_final }}' "$packages"; then
    fail "package does not pass needs.release-policy.outputs.is_final"
  fi
  if grep -q 'sign_windows\|windows_sign' "$packages" "$client"; then
    fail "Windows signing is still gated by sign_windows/windows_sign"
  fi

  if grep -q 'config: \[optimized=1, TSAN=1, ASAN=1\]' "$packages" "$client"; then
    fail "artifact and sanitizer configs share one matrix"
  fi
  if ! grep -q 'config: ${{ fromJSON(inputs.configs) }}' "$client"; then
    fail "client matrix is not selected by the caller"
  fi
}

# shellcheck disable=SC2016
assert_windows_signing_wiring() {
  client="${root}/.github/workflows/build-client.yml"
  packages="${root}/.github/workflows/build-packages.yml"
  script="${root}/scripts/package-windows-bundle.ps1"
  installer_script="${root}/scripts/windows-installer.ps1"
  driver_script="${root}/scripts/get-libft4222.ps1"
  ftdi_script="${root}/scripts/windows-ftdi-driver.ps1"
  installer="${root}/installer/windows/chiavdf.iss"
  inno_action="${root}/.github/actions/install-inno-setup/action.yml"
  sign_action_sha='b2a0d3030aa5d5c8f9b8f3b8d1e4bda2d09bf916'
  build_block="$(job_block "$client" build)"
  test_installer_block="$(job_block "$client" test-windows-installer)"
  sign_block="$(job_block "$client" sign-windows)"
  publish_block="$(job_block "$packages" publish-release)"

  if [ "$(job_field "$client" sign-windows environment)" != "windows-code-signing" ]; then
    fail "Windows signing job does not use the windows-code-signing environment"
  fi
  if [ "$(job_field "$client" sign-windows needs)" != "[build]" ]; then
    fail "Windows signing job does not need the build job"
  fi
  if [ "$(job_field "$client" sign-windows if)" != "contains(inputs.configs, 'optimized=1') && github.event_name != 'pull_request'" ]; then
    fail "Windows signing job is not tied to optimized non-PR Windows builds"
  fi
  if [ "$(job_field "$client" sign-windows runs-on)" != "windows-latest" ]; then
    fail "Windows signing job does not run on windows-latest"
  fi
  if [ "$(job_field "$client" sign-windows timeout-minutes)" != "30" ]; then
    fail "Windows signing job does not have a bounded timeout"
  fi
  if [ "$(job_field "$client" sign-windows contents)" != "read" ]; then
    fail "Windows signing job does not limit contents permission to read"
  fi
  if [ "$(job_field "$client" sign-windows id-token)" != "write" ]; then
    fail "Windows signing job does not request id-token: write"
  fi
  if [ "$(job_field "$packages" package id-token)" != "write" ]; then
    fail "package caller does not grant id-token: write to the signing workflow"
  fi
  # build-client.yml contains the sign-windows job, so every caller must allow
  # id-token even when the optimized Windows matrix is absent.
  if [ "$(job_field "$packages" sanitize id-token)" != "write" ]; then
    fail "sanitize caller must grant id-token: write for the nested signing job"
  fi

  if ! printf '%s\n' "$build_block" | grep -Fq 'name: windows-amd64-binaries'; then
    fail "build job does not upload windows-amd64-binaries"
  fi
  if ! printf '%s\n' "$sign_block" | grep -Fq 'name: windows-amd64-binaries'; then
    fail "sign job does not download windows-amd64-binaries"
  fi
  if printf '%s\n' "$build_block" | grep -Fq 'name: windows-amd64-bundle'; then
    fail "build job still uploads an unsigned windows-amd64-bundle"
  fi
  if ! printf '%s\n' "$sign_block" | grep -Fq 'name: windows-amd64-bundle'; then
    fail "sign job does not upload the signed windows-amd64-bundle"
  fi
  if printf '%s\n' "$build_block" | grep -Fq "REQUIRE_SIGNATURES: 'false'" ||
    printf '%s\n' "$sign_block" | grep -Fq "REQUIRE_SIGNATURES: 'false'"; then
    fail "unsigned Windows release packaging path still exists"
  fi
  if ! printf '%s\n' "$sign_block" | grep -Fq "REQUIRE_SIGNATURES: 'true'"; then
    fail "signed Windows packaging does not require signature verification"
  fi
  if ! printf '%s\n' "$sign_block" | grep -Fq 'IS_RELEASE: ${{ inputs.is_final }}'; then
    fail "signed Windows packaging does not read inputs.is_final"
  fi
  if ! printf '%s\n' "$sign_block" | grep -Fq "Chia-Network/actions/sign/azure-windows@${sign_action_sha} # main"; then
    fail "Windows signing does not use the pinned Azure Windows Code Sign action"
  fi
  if ! printf '%s\n' "$sign_block" | grep -Fq 'file: ${{ steps.sign_files.outputs.files }}'; then
    fail "Windows signing action does not receive the selected file list"
  fi
  if printf '%s\n' "$sign_block" | grep -Fq 'ListInstallerSignFile'; then
    fail "Windows signing still lists the installer with ListInstallerSignFile"
  fi
  if ! printf '%s\n' "$sign_block" | grep -Fq -- '-Action ListSignFiles'; then
    fail "Windows signing does not list files with ListSignFiles"
  fi
  if ! printf '%s\n' "$sign_block" | grep -Fq -- '-Action BuildInstaller'; then
    fail "Windows signing does not build the installer"
  fi
  if ! printf '%s\n' "$sign_block" | grep -Fq -- '-Action FinalizeInstaller'; then
    fail "Windows signing does not finalize the signed installer"
  fi
  if ! printf '%s\n' "$sign_block" | grep -Fq -- '-Action VerifyInstaller'; then
    fail "Windows signing does not verify the installer"
  fi
  if ! printf '%s\n' "$sign_block" | grep -Fq -- '-Action TestInstaller'; then
    fail "Windows signing does not smoke-test the installer"
  fi
  if grep -Fq 'choco install innosetup' "$client" "$inno_action"; then
    fail "Windows workflow still installs Inno Setup with Chocolatey"
  fi
  if ! grep -Fq 'innosetup-6.7.3.exe' "$inno_action" ||
    ! grep -Fq '9c73c3bae7ed48d44112a0f48e66742c00090bdb5bef71d9d3c056c66e97b732' "$inno_action" ||
    ! grep -Fq 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe' "$inno_action"; then
    fail "Inno Setup action does not pin the official 6.7.3 installer and its SHA-256"
  fi
  if [ "$(printf '%s\n' "$sign_block" | grep -Fc 'uses: ./.github/actions/install-inno-setup')" -ne 1 ] ||
    [ "$(printf '%s\n' "$test_installer_block" | grep -Fc 'uses: ./.github/actions/install-inno-setup')" -ne 1 ]; then
    fail "Inno Setup is not installed exactly once in each Windows job"
  fi
  inno_line=$(printf '%s\n' "$sign_block" | grep -n -F 'uses: ./.github/actions/install-inno-setup' | cut -d: -f1)
  first_sign_line=$(printf '%s\n' "$sign_block" | grep -n -F "Chia-Network/actions/sign/azure-windows@${sign_action_sha}" | head -1 | cut -d: -f1)
  if [ -z "$inno_line" ] || [ -z "$first_sign_line" ] || [ "$inno_line" -ge "$first_sign_line" ]; then
    fail "Inno Setup is not installed before the first Azure signing step"
  fi
  if grep -Fq 'Get-ChildItem -Path (Join-Path $base' "$client"; then
    fail "Windows workflow still has the redundant ISCC lookup loop"
  fi
  if [ "$(printf '%s\n' "$sign_block" | grep -Fc "Chia-Network/actions/sign/azure-windows@${sign_action_sha}")" -ne 2 ]; then
    fail "Windows signing should install the client and sign the binaries in one Azure signing call"
  fi
  if ! printf '%s\n' "$sign_block" | grep -Fq 'INSTALLER_SIGNTOOL'; then
    fail "Windows signing job does not pass an installer SignTool to ISCC"
  fi
  signtool_line=$(printf '%s\n' "$sign_block" | grep -F 'INSTALLER_SIGNTOOL')
  if ! printf '%s\n' "$signtool_line" | grep -Fq 'sign-windows-file.ps1$q $f'; then
    fail "INSTALLER_SIGNTOOL does not quote the helper path with Inno's \$q placeholder"
  fi
  build_line=$(printf '%s\n' "$sign_block" | grep -n -F 'name: Build Windows installer' | cut -d: -f1)
  finalize_line=$(printf '%s\n' "$sign_block" | grep -n -F 'name: Finalize signed Windows installer' | cut -d: -f1)
  verify_line=$(printf '%s\n' "$sign_block" | grep -n -F 'name: Verify Windows installer' | cut -d: -f1)
  if [ -z "$build_line" ] || [ -z "$finalize_line" ] || [ -z "$verify_line" ] ||
    [ "$build_line" -ge "$finalize_line" ] || [ "$finalize_line" -ge "$verify_line" ]; then
    fail "Windows installer build/finalize/verify steps are out of order"
  fi
  if ! printf '%s\n' "$build_block" | grep -Fq 'src/hw/libft4222/driver/**'; then
    fail "Windows build artifact does not upload the staged FTDI driver"
  fi
  if ! printf '%s\n' "$sign_block" | grep -Fq 'src/hw/libft4222/driver/ftdibus.inf'; then
    fail "Windows signing does not verify the staged FTDI driver"
  fi
  if [ -z "$test_installer_block" ]; then
    fail "Windows workflow does not smoke-test the installer on pull requests"
  fi
  if ! printf '%s\n' "$test_installer_block" | grep -Fq -- '-Action Assemble' ||
    ! printf '%s\n' "$test_installer_block" | grep -Fq -- '-Action BuildInstaller' ||
    ! printf '%s\n' "$test_installer_block" | grep -Fq -- '-Action TestInstaller'; then
    fail "PR installer job does not assemble, build, and smoke-test the installer"
  fi
  if printf '%s\n' "$test_installer_block" | grep -Fq 'AZURE_SIGNING' ||
    printf '%s\n' "$test_installer_block" | grep -Fq 'environment: windows-code-signing'; then
    fail "PR installer job must not enter the signing environment"
  fi
  if ! printf '%s\n' "$test_installer_block" | grep -Fq 'tests/test_windows_installer_version.ps1'; then
    fail "PR installer job does not run the installer version regression test"
  fi
  if ! printf '%s\n' "$build_block" | grep -Fq 'step-security/msvc-dev-cmd@v1.13.1'; then
    fail "Windows build job does not set up the MSVC environment"
  fi
  if ! printf '%s\n' "$sign_block" | grep -Fq 'step-security/msvc-dev-cmd@v1.13.1'; then
    fail "Windows signing job does not set up the MSVC environment"
  fi
  if grep -Fq 'ilammy/msvc-dev-cmd' "$client"; then
    fail "Windows workflow still uses ilammy/msvc-dev-cmd"
  fi
  if ! printf '%s\n' "$sign_block" | grep -Fq 'EXPECTED_SIGNER_SUBJECT' ||
    ! printf '%s\n' "$sign_block" | grep -Fq 'EXPECTED_SIGNER_THUMBPRINT' ||
    ! printf '%s\n' "$sign_block" | grep -Fq 'EXPECTED_VENDOR_SUBJECT'; then
    fail "Windows signing job does not pass expected signer variables"
  fi
  if printf '%s\n' "$build_block" | grep -Fq 'AZURE_SIGNING'; then
    fail "Azure signing secrets are referenced outside the sign-windows job"
  fi
  if ! printf '%s\n' "$sign_block" | grep -Fq 'AZURE_SIGNING_CLIENT_ID' ||
    ! printf '%s\n' "$sign_block" | grep -Fq 'AZURE_SIGNING_TENANT_ID'; then
    fail "Windows signing job does not read the Azure signing secrets"
  fi
  if ! printf '%s\n' "$publish_block" | grep -Fq 'pattern: windows-amd64-bundle'; then
    fail "publish-release does not download windows-amd64-bundle"
  fi

  if ! grep -Fq "ValidateSet('Assemble', 'ListSignFiles', 'Package', 'BuildInstaller', 'FinalizeInstaller', 'VerifyInstaller', 'TestInstaller')" "$script"; then
    fail "Windows script does not expose the installer actions"
  fi
  if grep -Fq 'ListInstallerSignFile' "$script" "$installer_script" "$client"; then
    fail "ListInstallerSignFile is still present"
  fi
  for ps_file in "$script" "$installer_script"; do
    lines=$(wc -l < "$ps_file")
    if [ "$lines" -ge 1000 ]; then
      fail "$(basename "$ps_file") has $lines lines (limit is 999)"
    fi
  done
  if ! grep -Fq 'ftdibus.inf' "$installer" || ! grep -Fq 'pnputil.exe' "$installer"; then
    fail "Inno Setup script does not stage the FTDI driver"
  fi
  if ! grep -Fq 'DisableDirPage=auto' "$installer"; then
    fail "Inno Setup script does not use DisableDirPage=auto"
  fi
  if ! grep -Fq 'SignedUninstaller=yes' "$installer" ||
    ! grep -Fq 'SignTool={#MySignTool}' "$installer"; then
    fail "Inno Setup script does not enable a signed uninstaller"
  fi
  if grep -Fq 'DefaultGroupName=' "$installer" ||
    grep -Fq 'RestartIfNeededByRun' "$installer" ||
    grep -Fq 'PrivilegesRequiredOverridesAllowed=' "$installer"; then
    fail "Inno Setup script still has directives that do nothing"
  fi
  if ! grep -Fq 'UninstallDisplayName=chiavdf' "$installer" ||
    grep -Eq 'UninstallDisplayName=chiavdf \{#' "$installer"; then
    fail "Inno Setup uninstall display name still contains the version"
  fi
  # Installer behaviour is covered by compiling and running the .iss under a
  # real Inno Setup, not by grepping its Pascal source in this test.
  if ! grep -Fq 'Install-Ftd2xxDriver' "$driver_script" ||
    ! grep -Fq 'windows-ftdi-driver.ps1' "$driver_script" ||
    ! grep -Fq 'Assert-FtdiDriverSource' "$driver_script"; then
    fail "FT4222 fetch script does not use the shared FTDI driver validator"
  fi
  if ! grep -Fq 'windows-ftdi-driver.ps1' "$script" ||
    ! grep -Fq 'Assert-FtdiDriverSource' "$installer_script"; then
    fail "Windows packaging does not use the shared FTDI driver validator"
  fi
  if ! grep -Fq 'Microsoft Windows Hardware Compatibility Publisher' "$ftdi_script" ||
    grep -Fq 'EXPECTED_VENDOR_SUBJECT' "$ftdi_script" ||
    grep -Fq 'Microsoft|FTDI|Future Technology' "$ftdi_script"; then
    fail "FTDI catalog signer check is not pinned to the exact publisher CN"
  fi
  if ! grep -Fq 'Assert-Authenticode -Path' "$installer_script" ||
    grep -Fq 'Assert-InstallerSignature' "$installer_script"; then
    fail "Installer signature verification does not reuse Assert-Authenticode"
  fi
  if ! grep -Fq 'Publish-ReleaseAlias' "$script" ||
    ! grep -Fq 'Publish-ReleaseAlias' "$installer_script" ||
    grep -Fq 'Publish-InstallerAlias' "$installer_script"; then
    fail "Installer alias publishing does not reuse the shared alias helper"
  fi
  if ! grep -Fq "Require-Text 'INSTALLER_SIGNTOOL'" "$installer_script" ||
    ! grep -Fq '/SChiaSignTool=' "$installer_script"; then
    fail "Installer build does not pass the workflow SignTool to ISCC"
  fi
  if ! grep -Fq '/DRIVER=1' "$installer_script" ||
    ! grep -Fq '/enum-drivers' "$installer_script" ||
    ! grep -Fq 'Get-MachinePathEntryCount' "$installer_script"; then
    fail "Installer smoke test does not exercise the driver, driver store, and PATH dedupe"
  fi
  if ! grep -Fq 'if ($env:CI)' "$installer_script" ||
    ! grep -Fq 'must run elevated in CI' "$installer_script"; then
    fail "Installer smoke test does not fail when non-elevated in CI"
  fi
  if ! grep -Fq "Require-Flag 'REQUIRE_SIGNATURES'" "$script"; then
    fail "Windows script does not require REQUIRE_SIGNATURES for Package"
  fi
  if ! grep -Fq 'Get-AuthenticodeSignature' "$script"; then
    fail "Windows script does not verify publisher signatures"
  fi
  if ! grep -Fq 'TimeStamperCertificate' "$script"; then
    fail "Windows script does not require a signature timestamp"
  fi
  if ! grep -Fq 'EXPECTED_SIGNER_SUBJECT' "$script" ||
    ! grep -Fq 'EXPECTED_SIGNER_THUMBPRINT' "$script" ||
    ! grep -Fq 'EXPECTED_VENDOR_SUBJECT' "$script"; then
    fail "Windows script does not support expected signer variables"
  fi
  if grep -Fq 'REQUEST_WINDOWS_SIGNING' "$script" || grep -Fq 'HAS_WINDOWS_SIGNING_SECRET' "$script"; then
    fail "Windows script still references the old DigiCert signing flags"
  fi
  if grep -Fq "ValidateSet('Assemble', 'Sign', 'Package')" "$script"; then
    fail "Windows script still exposes the removed local Sign action"
  fi
  if grep -E 'REQUIRE_SIGNATURES:|IS_RELEASE:' "$client" | grep -q 'github.event_name'; then
    fail "signing or IS_RELEASE still keys off github.event_name"
  fi
  if grep -Eq 'SM_(API_KEY|CLIENT_CERT_FILE_B64|CODE_SIGNING_CERT_SHA1_HASH|CERTKEY_ALIAS)' "$client"; then
    fail "client workflow still references DigiCert SM_* secrets"
  fi
  for asset in \
    'chiavdf-{tag}-windows-amd64-setup.exe' \
    'chiavdf-{tag}-windows-amd64-setup.exe.sha256' \
    'chiavdf-win64-setup.exe' \
    'chiavdf-win64-setup.exe.sha256'; do
    if ! grep -Fq "$asset" "${manifest_dir}/chiavdf.txt"; then
      fail "release manifest is missing ${asset}"
    fi
  done
}

assert_publish_dependencies
assert_windows_signing_wiring

rm -rf "$assets" "$gh_bin"
rm -f "$calls" "$output"
echo "release publish checks passed"
