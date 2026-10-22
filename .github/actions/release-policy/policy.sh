#!/usr/bin/env bash
# Classify the current GitHub event. Publishing is allowed only when is_final
# is the string true. Anything else, including a missing prerelease flag, is
# not a final release.
set -euo pipefail

event_name=${EVENT_NAME:-}
event_action=${EVENT_ACTION:-}
prerelease_json=${PRERELEASE_JSON:-null}
macos_notarize_json=${MACOS_NOTARIZE_JSON:-null}

is_final=false
if [ "$event_name" = "release" ] && [ "$event_action" = "published" ] && [ "$prerelease_json" = "false" ]; then
  is_final=true
fi

json_true() {
  # toJSON(true) is true. toJSON("true") is "true", quotes included.
  [ "$1" = "true" ] || [ "$1" = '"true"' ]
}

notarize_macos=false
if [ "$is_final" = "true" ]; then
  notarize_macos=true
fi
# A release event cannot be rehearsed: prereleases stay off even if an input is
# present, and final releases are already on above.
if [ "$event_name" = "workflow_dispatch" ] && json_true "$macos_notarize_json"; then
  notarize_macos=true
fi

if [ -z "${GITHUB_OUTPUT:-}" ]; then
  echo "release policy: GITHUB_OUTPUT is required" >&2
  exit 1
fi

{
  printf 'is_final=%s\n' "$is_final"
  printf 'notarize_macos=%s\n' "$notarize_macos"
} >> "$GITHUB_OUTPUT"
