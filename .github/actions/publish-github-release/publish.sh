#!/usr/bin/env bash
# Upload one complete GitHub release asset set, then record glue payloads.
# A non-final release skips. A final release with a missing file, a bad
# digest, or an unsafe tag uploads nothing.
set -euo pipefail

die() {
  echo "publish-github-release: $*" >&2
  exit 1
}

write_output() {
  if [ -n "${GITHUB_OUTPUT:-}" ]; then
    printf '%s\n' "$1" >> "$GITHUB_OUTPUT"
  fi
}

if [ "${IS_FINAL:-}" != "true" ]; then
  echo "publish-github-release: skipping because IS_FINAL is not true" >&2
  write_output "published=false"
  exit 0
fi

if [ -z "${RELEASE_TAG:-}" ] || [ -z "${ASSETS_DIR:-}" ] || [ -z "${MANIFESTS:-}" ] || [ -z "${GLUE_URL:-}" ]; then
  die "tag, assets directory, manifests, and glue URL are required"
fi
if [ ! -d "$ASSETS_DIR" ]; then
  die "assets directory does not exist"
fi
if [[ ! "$RELEASE_TAG" =~ ^[0-9A-Za-z][0-9A-Za-z._+-]*$ ]]; then
  die "release tag must be a single safe token"
fi

script_dir=$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
manifest_dir="${script_dir}/manifests"

declare -A required=()
required_order=()
manifest_names=()

IFS=',' read -r -a manifest_tokens <<< "$MANIFESTS"
for token in "${manifest_tokens[@]}"; do
  name=${token//[[:space:]]/}
  if [ -z "$name" ]; then
    die "empty manifest name"
  fi
  case "$name" in
    chiavdf|chiavdfc) ;;
    *) die "unknown manifest: ${name}" ;;
  esac
  manifest_path="${manifest_dir}/${name}.txt"
  if [ ! -f "$manifest_path" ]; then
    die "unknown manifest: ${name}"
  fi
  manifest_names+=("$name")
  while IFS= read -r line || [ -n "$line" ]; do
    line=${line%%#*}
    line=${line//[[:space:]]/}
    if [ -z "$line" ]; then
      continue
    fi
    asset=${line//\{tag\}/$RELEASE_TAG}
    if [[ "$asset" == *"/"* || "$asset" == *".."* || -z "$asset" ]]; then
      die "manifest entry is not a basename: ${line}"
    fi
    if [ -n "${required[$asset]:-}" ]; then
      die "asset listed more than once: ${asset}"
    fi
    required[$asset]=1
    required_order+=("$asset")
  done < "$manifest_path"
done

if [ "${#required_order[@]}" -eq 0 ]; then
  die "manifest list produced no assets"
fi

declare -A matches=()
while IFS= read -r -d '' path; do
  base=$(basename "$path")
  if [ -z "${required[$base]:-}" ]; then
    continue
  fi
  if [ -n "${matches[$base]:-}" ]; then
    die "duplicate asset basename: ${base}"
  fi
  matches[$base]=$path
done < <(find "$ASSETS_DIR" -type f -print0)

missing=0
for asset in "${required_order[@]}"; do
  if [ -z "${matches[$asset]:-}" ]; then
    echo "publish-github-release: missing ${asset}" >&2
    missing=1
  fi
done
if [ "$missing" -ne 0 ]; then
  die "required assets are incomplete; nothing was uploaded"
fi

verify_sha256() {
  local sidecar=$1
  local payload=$2
  local expected actual
  expected=$(tr -d '[:space:]' < "$sidecar" | tr '[:upper:]' '[:lower:]')
  if [[ ! "$expected" =~ ^[0-9a-f]{64}$ ]]; then
    die "sha256 sidecar is not a 64-digit digest: $(basename "$sidecar")"
  fi
  actual=$(sha256sum "$payload" | awk '{print $1}')
  if [ "$actual" != "$expected" ]; then
    die "sha256 mismatch: $(basename "$payload")"
  fi
}

for asset in "${required_order[@]}"; do
  case "$asset" in
    *.sha256)
      payload=${asset%.sha256}
      if [ -z "${matches[$payload]:-}" ]; then
        die "sha256 sidecar has no payload in the publish set: ${asset}"
      fi
      verify_sha256 "${matches[$asset]}" "${matches[$payload]}"
      ;;
  esac
done

files=()
for asset in "${required_order[@]}"; do
  files+=("${matches[$asset]}")
done

glue_chiavdf=""
glue_chiavdfc=""
for name in "${manifest_names[@]}"; do
  case "$name" in
    chiavdf)
      glue_chiavdf=$(jq -nc --arg release_version "$RELEASE_TAG" --arg windows_amd64 "chiavdf-win64.zip" \
        '{release_version:$release_version,windows_amd64:$windows_amd64}')
      ;;
    chiavdfc)
      glue_chiavdfc=$(jq -nc --arg release_version "$RELEASE_TAG" '{release_version:$release_version}')
      ;;
  esac
done

echo "publish-github-release: uploading ${#files[@]} assets to ${RELEASE_TAG}"
gh release upload --clobber "$RELEASE_TAG" "${files[@]}"

if [ -n "$glue_chiavdf" ]; then
  write_output "glue_chiavdf=${glue_chiavdf}"
fi
if [ -n "$glue_chiavdfc" ]; then
  write_output "glue_chiavdfc=${glue_chiavdfc}"
fi
write_output "published=true"
