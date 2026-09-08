#!/usr/bin/env bash
# Download the OpenOrbis PS4 Toolchain release (built against LLVM 18, which is
# the clang this port is tested with) into $1 (default: ./ps4-toolchain) and
# print the export line to put in your shell.
#
#   tools/ps4/fetch-toolchain.sh [dest-dir] [llvm-version]
#
# The release tarball already contains musl, libc++, the library stubs, the
# SDL2 port, create-fself, create-gp4, readoelf and PkgTool.Core.
set -euo pipefail

DEST="${1:-$(pwd)/ps4-toolchain}"
LLVM_VER="${2:-18}"
URL="https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/latest/download/toolchain-llvm-${LLVM_VER}.tar.gz"

mkdir -p "$DEST"
echo "Downloading $URL"
curl -sSL --fail -o "$DEST/toolchain.tar.gz" "$URL"
echo "Extracting to $DEST"
tar -xzf "$DEST/toolchain.tar.gz" -C "$DEST" --strip-components=2
rm -f "$DEST/toolchain.tar.gz"

if [ ! -f "$DEST/link.x" ] || [ ! -f "$DEST/lib/crt1.o" ]; then
    echo "error: $DEST does not contain link.x / lib/crt1.o after extraction" >&2
    exit 1
fi
chmod +x "$DEST"/bin/linux/* 2>/dev/null || true

echo
echo "OpenOrbis toolchain installed."
echo "  export OO_PS4_TOOLCHAIN=$DEST"
echo "Host requirements: clang and lld (clang-${LLVM_VER} recommended), cmake >= 3.20, ninja."
