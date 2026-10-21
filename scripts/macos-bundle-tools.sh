#!/usr/bin/env bash
# Canonical list of executables shipped in the macOS release bundle.
#
# The bundle assembler, verifier, pkg builder, and tests all source this file,
# so adding or removing a shipped tool is a one-line change.
# shellcheck disable=SC2034 # Sourced by the packaging and verification scripts.
MACOS_BUNDLE_TOOLS=(
  vdf_client
  vdf_bench
  hw_vdf_client
  emu_hw_vdf_client
  hw_test
  emu_hw_test
)
