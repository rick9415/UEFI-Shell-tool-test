#!/usr/bin/env bash
# Build all UefiShellTools applications against an EDK2 workspace.
#
# Prerequisites:
#   - EDK2 source tree with BaseTools already built
#   - EDK2_DIR set to the edk2 root (or passed as $1)
#   - ARCH: IA32 or X64 (default X64)
#   - TOOLCHAIN: GCC5, CLANGPDB, VS2019 … (default GCC5)
#
# Usage:
#   EDK2_DIR=/opt/edk2 bash build.sh
#   bash build.sh /opt/edk2 X64 GCC5

set -euo pipefail

EDK2_DIR="${1:-${EDK2_DIR:-}}"
ARCH="${2:-${ARCH:-X64}}"
TOOLCHAIN="${3:-${TOOLCHAIN:-GCC5}}"
TARGET="${BUILD_TARGET:-DEBUG}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [[ -z "$EDK2_DIR" ]]; then
  echo "ERROR: Set EDK2_DIR or pass it as the first argument." >&2
  exit 1
fi

if [[ ! -f "$EDK2_DIR/edksetup.sh" ]]; then
  echo "ERROR: '$EDK2_DIR' does not look like an EDK2 root (edksetup.sh not found)." >&2
  exit 1
fi

echo "==> EDK2  : $EDK2_DIR"
echo "==> Arch  : $ARCH"
echo "==> Chain : $TOOLCHAIN"
echo "==> Target: $TARGET"

# Symlink (or copy) this package into the EDK2 workspace
LINK_TARGET="$EDK2_DIR/UefiShellTools"
if [[ ! -e "$LINK_TARGET" ]]; then
  ln -s "$SCRIPT_DIR/UefiShellTools" "$LINK_TARGET"
  echo "==> Linked UefiShellTools into $EDK2_DIR"
fi

# Source EDK2 environment
# shellcheck source=/dev/null
source "$EDK2_DIR/edksetup.sh"

build \
  -p UefiShellTools/UefiShellTools.dsc \
  -a "$ARCH" \
  -t "$TOOLCHAIN" \
  -b "$TARGET"

echo
echo "==> Build complete."
echo "    EFI binaries in: $EDK2_DIR/Build/UefiShellTools/${TARGET}_${TOOLCHAIN}/${ARCH}/"
