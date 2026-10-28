#!/usr/bin/env bash
# Assemble, sign, package, and notarize the macOS release artifacts.
#
# The workflow passes release intent and secret-presence flags. This script
# derives one signing mode from them, so the zip name, pkg name, signing calls,
# notarization calls, and status file cannot drift apart:
#
#   adhoc         no Developer ID; ad-hoc sign the staged Mach-O files
#   developer-id  Developer ID Application + Installer; do not notarize
#   notarized     Developer ID Application + Installer; notarize/staple the pkg
#
# Notarization submits only the pkg. Notarization is recorded per code object
# (CDHash), and pkgbuild does not modify the staged binaries, so the submission
# covers the exact signatures the zip ships. A zip cannot be stapled; Gatekeeper
# finds the online records during the zip's first launch.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

# shellcheck source=scripts/macos-bundle-tools.sh
source "$script_dir/macos-bundle-tools.sh"

usage() {
  cat <<'EOF'
Usage: scripts/package-macos-bundle.sh [--print-plan]

Environment:
  RELEASE_TAG                  release tag or CI version (required)
  MACOS_ARCH                   arm64 or intel (default: host architecture)
  IS_RELEASE                   true for a final release
  REQUEST_NOTARIZE             true to notarize and staple the pkg
  HAS_APPLE_SECRET             true when the app signing certificate is available
  HAS_NOTARIZE_SECRETS         true when notarization credentials are available
  HAS_INSTALLER_SECRET         true when the installer certificate is available
  APP_SIGNING_IDENTITY         Developer ID Application SHA-1 hash
  INSTALLER_SIGNING_IDENTITY   Developer ID Installer common name (productsign)
  APPLE_NOTARIZE_USERNAME      Apple ID for notarytool
  APPLE_NOTARIZE_PASSWORD      app-specific password for notarytool
  APPLE_TEAM_ID                Apple team ID

--print-plan prints the derived mode and artifact names without touching the
filesystem. It is useful for debugging the release inputs and for tests.
EOF
}

die() {
  echo "package-macos-bundle: $*" >&2
  exit 1
}

print_plan=false
if [ "$#" -gt 1 ]; then
  usage >&2
  exit 1
fi
case "${1:-}" in
  '') ;;
  --print-plan) print_plan=true ;;
  -h|--help)
    usage
    exit 0
    ;;
  *)
    echo "Unknown argument: $1" >&2
    usage >&2
    exit 1
    ;;
esac

release_tag=${RELEASE_TAG:-}
[ -n "$release_tag" ] || die "RELEASE_TAG is required"

macos_arch=${MACOS_ARCH:-}
if [ -z "$macos_arch" ]; then
  case "$(uname -m)" in
    arm64) macos_arch=arm64 ;;
    x86_64) macos_arch=intel ;;
    *) die "Unsupported macOS architecture: $(uname -m)" ;;
  esac
fi
case "$macos_arch" in
  arm64|intel) ;;
  *) die "MACOS_ARCH must be arm64 or intel, got: $macos_arch" ;;
esac

is_release=${IS_RELEASE:-false}
request_notarize=${REQUEST_NOTARIZE:-false}
has_app_secret=${HAS_APPLE_SECRET:-false}
has_notarize_secrets=${HAS_NOTARIZE_SECRETS:-false}
has_installer_secret=${HAS_INSTALLER_SECRET:-false}
app_signing_identity=${APP_SIGNING_IDENTITY:-}
installer_signing_identity=${INSTALLER_SIGNING_IDENTITY:-}

if [ "$is_release" = true ] || [ "$request_notarize" = true ]; then
  [ "$has_app_secret" = true ] ||
    die "Release/notarized builds require Apple signing certificate secrets (APPLE_DEV_ID_APP/APPLE_DEV_ID_APP_PASS)."
  [ "$has_installer_secret" = true ] ||
    die "Release/notarized builds require the macOS installer identity (APPLE_DEV_ID_INSTALLER/APPLE_DEV_ID_INSTALLER_PASS)."
  [ -n "$app_signing_identity" ] ||
    die "APP_SIGNING_IDENTITY is required for a Developer ID build."
  [ -n "$installer_signing_identity" ] ||
    die "INSTALLER_SIGNING_IDENTITY (the installer common name) is required for a Developer ID build."
  # Match the existing release gate: even a final release that did not request
  # notarization must have the notarization credentials present.
  [ "$has_notarize_secrets" = true ] ||
    die "Release/notarized builds require notarization secrets (APPLE_NOTARIZE_USERNAME/APPLE_NOTARIZE_PASSWORD/APPLE_TEAM_ID)."
fi

if [ "$request_notarize" = true ]; then
  [ -n "${APPLE_NOTARIZE_USERNAME:-}" ] || die "APPLE_NOTARIZE_USERNAME is required for notarization."
  [ -n "${APPLE_NOTARIZE_PASSWORD:-}" ] || die "APPLE_NOTARIZE_PASSWORD is required for notarization."
  [ -n "${APPLE_TEAM_ID:-}" ] || die "APPLE_TEAM_ID is required for notarization."
  mode=notarized
elif [ "$is_release" = true ]; then
  mode=developer-id
else
  mode=adhoc
fi

base_name="chiavdf-${release_tag}-macos-${macos_arch}"
if [ "$mode" = adhoc ]; then
  asset_name="${base_name}-unsigned.zip"
else
  asset_name="${base_name}.zip"
fi
pkg_name="${asset_name%.zip}.pkg"

case "$mode" in
  adhoc)
    bundle_status="ad-hoc signed (no Developer ID; codesign -s - after install_name_tool)"
    pkg_status="unsigned (no Developer ID Installer; not for distribution)"
    ;;
  developer-id)
    bundle_status="signed (notarization not requested)"
    pkg_status="signed (Developer ID Installer; notarization not requested)"
    ;;
  notarized)
    bundle_status="zip: signed, binaries notarized via the pkg submission (a zip cannot be stapled); pkg: signed + notarized + stapled"
    pkg_status="signed + notarized + stapled (Developer ID Installer)"
    ;;
esac

if [ "$print_plan" = true ]; then
  printf 'mode=%s\n' "$mode"
  printf 'asset_name=%s\n' "$asset_name"
  printf 'pkg_name=%s\n' "$pkg_name"
  printf 'bundle_status=%s\n' "$bundle_status"
  printf 'pkg_status=%s\n' "$pkg_status"
  exit 0
fi

cd "$repo_root"

bundle_root="dist/macos/${base_name}"
bin_dir="${bundle_root}/bin"
libexec_dir="${bundle_root}/libexec/chiavdf"
mkdir -p "$bin_dir" "$libexec_dir"

for tool in "${MACOS_BUNDLE_TOOLS[@]}"; do
  cp "src/$tool" "$bin_dir/"
done

# FTDI and GMP share one libexec directory. The staging script rewrites
# install names and the executable rpath in one install_name_tool pass, before
# the codesign pass below.
scripts/stage-macos-bundle-dylibs.sh "$bin_dir" "$libexec_dir"

dylib_files=()
for dylib in "$libexec_dir"/*; do
  if [ -L "$dylib" ] || [ ! -f "$dylib" ]; then
    continue
  fi
  dylib_files+=("$dylib")
done
if [ "${#dylib_files[@]}" -eq 0 ]; then
  die "No dylibs staged into ${libexec_dir}"
fi

sign_targets=()
for exe in "$bin_dir/"*; do
  if [ ! -f "$exe" ] || [ -L "$exe" ]; then
    die "Unexpected bundle entry: $exe"
  fi
  sign_targets+=("$exe")
done
for dylib in "${dylib_files[@]}"; do
  sign_targets+=("$dylib")
done

if [ "$mode" != adhoc ]; then
  codesign_summary_path="dist/macos/${base_name}.codesign-summary.txt"

  is_valid_dev_id_signature() {
    local path="$1"
    local details
    if ! codesign --verify --strict --verbose=2 "$path" >/dev/null 2>&1; then
      return 1
    fi
    details="$(codesign -dv "$path" 2>&1 || true)"
    if printf '%s\n' "$details" | grep -Fq "Authority=Developer ID Application"; then
      return 0
    fi
    # Some runners may omit Authority lines in codesign -dv output. Treat a
    # strict-valid signature with a populated TeamIdentifier as acceptable for
    # Developer ID verification.
    if printf '%s\n' "$details" | grep -Eq '^TeamIdentifier=[^[:space:]]+' &&
      ! printf '%s\n' "$details" | grep -Fq 'TeamIdentifier=not set'; then
      return 0
    fi
    return 1
  }

  echo "Checking bundled dylib signatures before signing..."
  for dylib in "${dylib_files[@]}"; do
    if is_valid_dev_id_signature "$dylib"; then
      echo "Dylib already has a valid Developer ID signature: $dylib"
    else
      echo "Dylib needs signing: $dylib"
    fi
  done

  for target in "${sign_targets[@]}"; do
    if is_valid_dev_id_signature "$target"; then
      echo "Already signed with Developer ID: $target"
      continue
    fi
    codesign --force --timestamp --options runtime --sign "$app_signing_identity" "$target"
  done

  echo "Verifying all bundled Mach-O binaries are signed..."
  for target in "${sign_targets[@]}"; do
    if ! is_valid_dev_id_signature "$target"; then
      echo "Unsigned or invalid Developer ID signature: $target" >&2
      codesign -dv "$target" 2>&1 || true
      exit 1
    fi
  done

  : > "$codesign_summary_path"
  printf "codesign -dv summary for %s\n\n" "$base_name" >> "$codesign_summary_path"
  echo "codesign -dv summary (post-sign verification):"
  for target in "${sign_targets[@]}"; do
    printf "==== %s ====\n" "$target" | tee -a "$codesign_summary_path"
    codesign -dv "$target" 2>&1 |
      grep -E '^(Identifier=|Format=|CodeDirectory v=|Authority=|TeamIdentifier=|Timestamp=)' |
      tee -a "$codesign_summary_path"
    echo | tee -a "$codesign_summary_path"
  done
else
  # install_name_tool breaks the linker signature. arm64 will not execute that
  # Mach-O until it is signed again. This path has no Developer ID, so re-sign
  # ad-hoc.
  for target in "${sign_targets[@]}"; do
    codesign -s - --force "$target"
  done
fi

(cd dist/macos && ditto -c -k --keepParent "$base_name" "$asset_name")
# Launch the zip that will be published. Notarization does not run it, and a
# signed binary with a Homebrew absolute path fails library validation only on a
# user's machine.
scripts/verify-macos-bundle.sh "dist/macos/${asset_name}"

# Build the installer package from the same staged tree the zip was verified
# from. Installer writes payload files without the quarantine attribute, so
# first launch needs no Gatekeeper lookup. The package is also the only supported
# format here that can carry a stapled notarization ticket.
pkg_path="dist/macos/${pkg_name}"
installer_identity=""
if [ "$mode" != adhoc ]; then
  installer_identity="$installer_signing_identity"
fi
scripts/package-macos-pkg.sh "$bundle_root" "$pkg_path" "$release_tag" "$installer_identity"

if [ "$mode" = notarized ]; then
  if ! xcrun notarytool submit "$pkg_path" \
    --apple-id "$APPLE_NOTARIZE_USERNAME" \
    --password "$APPLE_NOTARIZE_PASSWORD" \
    --team-id "$APPLE_TEAM_ID" \
    --wait; then
    die "Notarization failed for $pkg_path"
  fi
  xcrun stapler staple "$pkg_path"
  xcrun stapler validate "$pkg_path"
  spctl -a -vv -t install "$pkg_path"
fi

status_path="dist/macos/${base_name}.signing-status.txt"
{
  printf "macOS %s release bundle status: %s\n" "$macos_arch" "$bundle_status"
  printf "macOS %s installer package status: %s\n" "$macos_arch" "$pkg_status"
} > "$status_path"

shasum -a 256 "dist/macos/${asset_name}" | awk '{print $1}' > "dist/macos/${asset_name}.sha256"
shasum -a 256 "$pkg_path" | awk '{print $1}' > "${pkg_path}.sha256"

if [ "$is_release" = true ]; then
  if [ "$macos_arch" = arm64 ]; then
    brew_arch=arm64
  else
    brew_arch=amd64
  fi
  brew_asset_name="chiavdf-darwin-${brew_arch}.zip"
  cp "dist/macos/${asset_name}" "dist/macos/${brew_asset_name}"
  cp "dist/macos/${asset_name}.sha256" "dist/macos/${brew_asset_name}.sha256"
fi

cat "$status_path"
