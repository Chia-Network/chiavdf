#!/usr/bin/env bash
# How a Mach-O install name is treated in the macOS release zip.
# system: the OS copy is the one dyld must load.
# bundled: @rpath, @loader_path, or @executable_path, resolved inside libexec.
# absolute: a build-machine path. It has to be copied into the zip.
# unknown: not shippable.

macos_install_name_kind() {
  case "$1" in
    /usr/lib/*|/System/*)
      printf '%s\n' system
      ;;
    @rpath/*|@loader_path/*|@executable_path/*)
      printf '%s\n' bundled
      ;;
    /*)
      printf '%s\n' absolute
      ;;
    "")
      printf '%s\n' empty
      ;;
    *)
      printf '%s\n' unknown
      ;;
  esac
}

# Basename dyld looks up for a flat @rpath/@loader_path/@executable_path name.
macos_bundled_relpath() {
  local rel="${1#@rpath/}"
  rel="${rel#@loader_path/}"
  rel="${rel#@executable_path/}"
  printf '%s\n' "$rel"
}

# LC_RPATH paths on one Mach-O, one per line. Status is otool's.
macos_rpaths() {
  otool -l "$1" | awk '
    $1 == "cmd" && $2 == "LC_RPATH" { in_rpath = 1; next }
    in_rpath && $1 == "path" { print $2; in_rpath = 0 }
  '
}
