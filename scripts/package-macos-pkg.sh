#!/usr/bin/env bash
# Build the macOS installer package for the staged chiavdf binaries.
#
# The payload keeps bin/ and libexec/chiavdf/ as siblings. Every shipped
# executable resolves its dylibs through @loader_path/../libexec/chiavdf, so
# installing the same tree under /usr/local keeps the rpath valid.
#
# An empty installer identity produces an unsigned package. Release builds pass
# a "Developer ID Installer" common name, which is what productsign requires;
# the "Developer ID Application" certificate used for the binaries cannot sign a
# package.
set -euo pipefail

# shellcheck source=scripts/macos-bundle-tools.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/macos-bundle-tools.sh"

usage() {
  echo "Usage: scripts/package-macos-pkg.sh <bundle-root> <output.pkg> <version> [installer-identity]" >&2
  exit 1
}

if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
  usage
fi

bundle_root=$1
output=$2
version=$3
identity=${4:-}

pkg_identifier=${PKG_IDENTIFIER:-com.chia.chiavdf}
install_location=${PKG_INSTALL_LOCATION:-/usr/local}

if [ ! -d "$bundle_root" ]; then
  echo "Missing bundle root: $bundle_root" >&2
  exit 1
fi
for tool in "${MACOS_BUNDLE_TOOLS[@]}"; do
  if [ ! -x "$bundle_root/bin/$tool" ]; then
    echo "Missing executable bin/$tool in $bundle_root" >&2
    exit 1
  fi
done
if [ ! -d "$bundle_root/libexec/chiavdf" ]; then
  echo "Missing libexec/chiavdf in $bundle_root" >&2
  exit 1
fi

# pkgbuild versions are numeric. Release tags such as v1.2.0-rc1 must not leak
# a suffix into the installer receipt, and a non-release run_id version must
# still produce a usable package.
numeric=${version#v}
numeric=${numeric%%[!0-9.]*}
numeric=${numeric%.}
case "$numeric" in
  ''|.*) numeric=0.0.1 ;;
esac

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
unsigned="${tmp}/package.pkg"

pkgbuild \
  --root "$bundle_root" \
  --identifier "$pkg_identifier" \
  --version "$numeric" \
  --install-location "$install_location" \
  --ownership recommended \
  "$unsigned"

if [ -n "$identity" ]; then
  productsign --sign "$identity" "$unsigned" "$output"
else
  cp "$unsigned" "$output"
fi

# pkgutil lists payload paths relative to the install location. Normalize a
# leading ./ and a trailing / before asserting the layout that lands on disk.
payload_list="${tmp}/payload.txt"
pkgutil --payload-files "$output" | sed 's#^\./##; s#/$##' > "$payload_list"

fail=0
for tool in "${MACOS_BUNDLE_TOOLS[@]}"; do
  if ! grep -Fxq "bin/$tool" "$payload_list"; then
    echo "Package payload is missing bin/$tool" >&2
    fail=1
  fi
done
if ! grep -Fq 'libexec/chiavdf/' "$payload_list"; then
  echo "Package payload is missing libexec/chiavdf" >&2
  fail=1
fi
# Ignore top-level AppleDouble/Finder metadata, but reject every other entry
# that is outside bin/ and libexec/, including top-level files. The previous
# NF > 1 filter skipped top-level files entirely.
while IFS= read -r unexpected; do
  case "$unexpected" in
    ''|.|.DS_Store|._*) ;;
    bin|libexec) ;;
    *)
      echo "Unexpected top-level payload entry: $unexpected" >&2
      fail=1
      ;;
  esac
done < <(awk -F/ '{ print $1 }' "$payload_list" | sort -u)

if [ -n "$identity" ]; then
  if ! pkgutil --check-signature "$output" | grep -Fq 'Developer ID Installer'; then
    echo "Package is not signed by a Developer ID Installer identity" >&2
    fail=1
  fi
fi

if [ "$fail" -ne 0 ]; then
  exit 1
fi

printf 'Packaged %s (identifier %s, version %s, install location %s)\n' \
  "$output" "$pkg_identifier" "$numeric" "$install_location"
