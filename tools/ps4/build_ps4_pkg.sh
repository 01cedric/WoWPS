#!/usr/bin/env bash
# Configure, build and package the PS4 client with OpenOrbis.
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$REPO_DIR/build-ps4"
CONFIG="Release"
TITLE_ID="${WOWEE_PS4_TITLE_ID:-WOWE00001}"
JOBS=""
CLEAN=0

die() { printf 'error: %s\n' "$*" >&2; exit 1; }
usage() {
    cat <<'USAGE'
Build WoWPS for PlayStation 4 and produce an installable .pkg.

Usage: tools/ps4/build_ps4_pkg.sh [options]

  --clean                Remove the selected build directory before building.
  --jobs N, -j N         Parallel compile jobs; defaults to the host CPU count.
  --build-dir DIR        Build directory; defaults to ./build-ps4.
  --config CONFIG        Release, Debug, RelWithDebInfo or MinSizeRel.
  --title-id ID          Four uppercase letters and five digits; WOWE00001.
  -h, --help             Show this help.

Environment:
  OO_PS4_TOOLCHAIN         Root of the OpenOrbis PS4 Toolchain.
  WOWEE_PS4_LIBSSL11_DIR   Optional directory with OpenSSL 1.1 host libraries.
  WOWEE_PS4_TITLE          Package title; defaults to WoWPS.
  WOWEE_PS4_VERSION        Package version; defaults to 01.61.
  WOWEE_PS4_CONTENT_ID     Optional full content ID override.

Required external link inputs:
  ps4/third_party/ps4_vulkan/lib/libopengnm.a
  ps4/third_party/ps4_vulkan/lib/libpsbc.orbis.a

See README.md for prerequisites, library compatibility and game-data setup.
Output: <build-dir>/pkg/<content-id>.pkg
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --clean) CLEAN=1; shift ;;
        -j|--jobs|--build-dir|--config|--title-id)
            [ "$#" -ge 2 ] && [ -n "$2" ] || die "$1 requires a value"
            case "$1" in
                -j|--jobs) JOBS="$2" ;;
                --build-dir) BUILD_DIR="$2" ;;
                --config) CONFIG="$2" ;;
                --title-id) TITLE_ID="$2" ;;
            esac
            shift 2 ;;
        --jobs=*) JOBS="${1#*=}"; shift ;;
        --build-dir=*) BUILD_DIR="${1#*=}"; shift ;;
        --config=*) CONFIG="${1#*=}"; shift ;;
        --title-id=*) TITLE_ID="${1#*=}"; shift ;;
        -h|--help) usage; exit 0 ;;
        *) die "unknown option '$1'; use --help" ;;
    esac
done

case "$CONFIG" in
    Release|Debug|RelWithDebInfo|MinSizeRel) ;;
    *) die "unsupported build configuration '$CONFIG'" ;;
esac
case "$TITLE_ID" in
    [A-Z][A-Z][A-Z][A-Z][0-9][0-9][0-9][0-9][0-9]) ;;
    *) die "--title-id must be four uppercase letters followed by five digits" ;;
esac
if [ -z "$JOBS" ]; then
    JOBS="$( (nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4) | tr -d '[:space:]')"
fi
[[ "$JOBS" =~ ^[0-9]+$ ]] && [[ "$JOBS" =~ [1-9] ]] || die "--jobs must be a positive integer"

for tool in cmake ninja python3; do
    command -v "$tool" >/dev/null 2>&1 || die "$tool was not found on PATH; see README.md"
done
for tool in clang clang++ ld.lld; do
    command -v "${tool}-18" >/dev/null 2>&1 || command -v "$tool" >/dev/null 2>&1 || \
        die "$tool was not found on PATH; LLVM 18 is recommended"
done
python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)' || die "Python 3.10 or newer is required"

[ -n "${OO_PS4_TOOLCHAIN:-}" ] || die "set OO_PS4_TOOLCHAIN to the OpenOrbis toolchain root; see README.md"
for f in link.x lib/crt1.o include/stdio.h include/c++/v1/vector; do
    [ -f "$OO_PS4_TOOLCHAIN/$f" ] || die "OpenOrbis toolchain is missing $f"
done
case "$(uname -s)" in
    Linux) TOOLS_DIR="$OO_PS4_TOOLCHAIN/bin/linux" ;;
    Darwin) TOOLS_DIR="$OO_PS4_TOOLCHAIN/bin/macos" ;;
    *) die "use Linux, macOS or WSL for the OpenOrbis host tools" ;;
esac
for tool in create-fself create-gp4 PkgTool.Core; do
    [ -x "$TOOLS_DIR/$tool" ] || die "$TOOLS_DIR/$tool is missing or not executable"
done
for lib in libopengnm.a libpsbc.orbis.a; do
    [ -s "$REPO_DIR/ps4/third_party/ps4_vulkan/lib/$lib" ] || \
        die "missing PS4 link input: ps4/third_party/ps4_vulkan/lib/$lib; see README.md"
done
for f in world.json catalog/manifest.json catalog/cells.idx catalog/contacts.pack \
         catalog/items.pack catalog/npcs.pack catalog/quests.pack catalog/spawns.pack; do
    [ -s "$REPO_DIR/assets/local_realm/$f" ] || die "missing bundled runtime data: assets/local_realm/$f"
done

# Resolve paths before allowing a recursive clean. Never delete a source tree,
# its parent, the home directory, or a toolchain directory.
[ -n "$BUILD_DIR" ] || die "--build-dir cannot be empty"
BUILD_DIR="$(python3 - "$BUILD_DIR" "$REPO_DIR" "$OO_PS4_TOOLCHAIN" <<'PY'
from pathlib import Path
import sys
build, repo, sdk = (Path(p).expanduser().resolve() for p in sys.argv[1:])
if (build in (Path('/'), Path.home().resolve()) or build == repo or build in repo.parents
    or build == sdk or build in sdk.parents or sdk in build.parents):
    sys.exit('error: unsafe build directory: ' + str(build))
if repo in build.parents:
    top = build.relative_to(repo).parts[0]
    if top not in ('build-ps4', 'out') and not top.startswith('build-'):
        sys.exit('error: use build-* or out/ for an in-repository build directory')
print(build)
PY
)"
if [ "$CLEAN" -eq 1 ] && [ -d "$BUILD_DIR" ]; then
    [ -f "$BUILD_DIR/CMakeCache.txt" ] || die "refusing to clean a directory without CMakeCache.txt: $BUILD_DIR"
    printf '==> removing %s\n' "$BUILD_DIR"
    rm -rf -- "$BUILD_DIR"
fi

printf '==> configure (%s)\n' "$CONFIG"
cmake -S "$REPO_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$REPO_DIR/cmake/ps4.toolchain.cmake" \
    -DCMAKE_BUILD_TYPE="$CONFIG"
printf '==> build (%s jobs)\n' "$JOBS"
cmake --build "$BUILD_DIR" --parallel "$JOBS"
[ -s "$BUILD_DIR/wowee.elf" ] || die "build did not produce $BUILD_DIR/wowee.elf"
printf '==> package\n'
WOWEE_PS4_TITLE_ID="$TITLE_ID" "$REPO_DIR/tools/ps4/package.sh" "$BUILD_DIR"

PKG=""
for file in "$BUILD_DIR/pkg/"*.pkg; do
    [ -s "$file" ] || continue
    PKG="$file"
done
[ -n "$PKG" ] || die "packaging did not produce a .pkg file"
printf '\nPackage: %s\nSize: %s bytes\n' "$PKG" "$(wc -c < "$PKG" | tr -d '[:space:]')"
