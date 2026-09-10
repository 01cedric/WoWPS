#!/usr/bin/env bash
# Turn a built wowee ELF into eboot.bin and an installable PS4 .pkg using the
# OpenOrbis tools (create-fself, create-gp4, PkgTool.Core).
#
#   tools/ps4/package.sh <build-dir> [out-dir]
#
# <build-dir> is the CMake build directory configured with
# cmake/ps4.toolchain.cmake; it must contain wowee.elf. The package stages
# everything the client reads at runtime from its own directory (assets/,
# Data/, addons/) next to eboot.bin. Game data is NOT packaged: users put their
# own MPQs in /data/wow_ps/Data on the console (see README.md).
set -euo pipefail

BUILD_DIR="${1:?usage: package.sh <build-dir> [out-dir]}"
OUT_DIR="${2:-$BUILD_DIR/pkg}"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
: "${OO_PS4_TOOLCHAIN:?OO_PS4_TOOLCHAIN must point at the OpenOrbis toolchain (tools/ps4/fetch-toolchain.sh)}"

case "$(uname -s)" in
    Linux)  TOOLS="$OO_PS4_TOOLCHAIN/bin/linux" ;;
    Darwin) TOOLS="$OO_PS4_TOOLCHAIN/bin/macos" ;;
    *)      echo "unsupported host $(uname -s)" >&2; exit 1 ;;
esac
# PkgTool.Core is a .NET Core 3 app: without ICU on the host it needs invariant
# mode, and pkg_build needs OpenSSL 1.1 (libssl.so.1.1 / libcrypto.so.1.1).
# Hosts with only OpenSSL 3 can point WOWEE_PS4_LIBSSL11_DIR at a directory
# holding the 1.1 libraries (e.g. built from the OpenSSL_1_1_1-stable branch).
export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
if [ -n "${WOWEE_PS4_LIBSSL11_DIR:-}" ]; then
    export LD_LIBRARY_PATH="${WOWEE_PS4_LIBSSL11_DIR}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
elif [ "$(uname -s)" = "Linux" ] && ! ldconfig -p 2>/dev/null | grep -q "libssl\.so\.1\.1"; then
    # Every current distribution ships OpenSSL 3 and nothing else, so on a
    # normal machine this branch is the one that runs. Rather than telling the
    # reader to go and build a decade-old OpenSSL, shim it.
    #
    # .NET Core 3 dlopens libssl by SONAME - it tries 1.0.x and 1.1 and stops -
    # then resolves a fixed symbol list from the handle. Every one of those
    # symbols still exists in OpenSSL 3 except three that 3.0 removed or
    # renamed. So the shim is an empty library that *depends* on the real
    # libssl.so.3 and libcrypto.so.3 - dlsym searches an object's dependencies,
    # so everything else resolves to the genuine implementation - plus a jump
    # for each of the three. They are bare `jmp` trampolines rather than C
    # wrappers because a jump preserves registers and the return slot exactly,
    # so one line is correct for any signature.
    _shim="${TMPDIR:-/tmp}/wowee-openssl11-shim"
    if [ ! -f "$_shim/libssl.so.1.1" ]; then
        mkdir -p "$_shim"
        cat > "$_shim/shim.c" <<'SHIM_C'
extern void ERR_new(void);
extern void ERR_set_debug(const char *file, int line, const char *func);
extern void ERR_set_error(int lib, int reason, const char *fmt, ...);
/* Removed in OpenSSL 3.0, which replaced it with the triple above. `func` is
   dropped because 3.x records the function as a string on the debug entry. */
void ERR_put_error(int lib, int func, int reason, const char *file, int line);
void ERR_put_error(int lib, int func, int reason, const char *file, int line) {
    (void)func; ERR_new(); ERR_set_debug(file, line, 0); ERR_set_error(lib, reason, 0);
}
SHIM_C
        cat > "$_shim/tramp.s" <<'SHIM_S'
.text
/* Renamed in OpenSSL 3.0; the old names are gone from the exports. */
.globl EVP_MD_size
.type EVP_MD_size, @function
EVP_MD_size:
    jmp EVP_MD_get_size@PLT
.size EVP_MD_size, .-EVP_MD_size
.globl SSL_get_peer_certificate
.type SSL_get_peer_certificate, @function
SSL_get_peer_certificate:
    jmp SSL_get1_peer_certificate@PLT
.size SSL_get_peer_certificate, .-SSL_get_peer_certificate
.section .note.GNU-stack,"",@progbits
SHIM_S
        # --no-as-needed is load-bearing: the shim references none of libssl's
        # own symbols, so without it the linker drops the DT_NEEDED entry and
        # dlsym has no dependency to search - every unrenamed symbol then reads
        # as missing and PkgTool reports "No usable version of libssl".
        if ! ( cc -shared -fPIC -O2 -o "$_shim/libssl.so.1.1" \
                  "$_shim/shim.c" "$_shim/tramp.s" -Wl,-soname,libssl.so.1.1 \
                  -Wl,--no-as-needed -l:libssl.so.3 -l:libcrypto.so.3 && \
               cc -shared -fPIC -O2 -o "$_shim/libcrypto.so.1.1" \
                  "$_shim/shim.c" "$_shim/tramp.s" -Wl,-soname,libcrypto.so.1.1 \
                  -Wl,--no-as-needed -l:libcrypto.so.3 ) 2>/dev/null; then
            echo "note: could not build the OpenSSL 1.1 shim; set WOWEE_PS4_LIBSSL11_DIR" >&2
        fi
    fi
    [ -f "$_shim/libssl.so.1.1" ] && export LD_LIBRARY_PATH="$_shim${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

TITLE="${WOWEE_PS4_TITLE:-WoWPS}"
VERSION="${WOWEE_PS4_VERSION:-01.90}"
TITLE_ID="${WOWEE_PS4_TITLE_ID:-WOWE00001}"
CONTENT_ID="${WOWEE_PS4_CONTENT_ID:-IV0000-${TITLE_ID}_00-WOWEEPS4CLIENT00}"

ELF="$BUILD_DIR/wowee.elf"
[ -f "$ELF" ] || { echo "error: $ELF not found (build with cmake/ps4.toolchain.cmake first)" >&2; exit 1; }
[ -s "$REPO_DIR/assets/local_realm/world.json" ] || {
    echo "error: assets/local_realm/world.json is missing or empty; the local realm content must be bundled" >&2
    exit 1
}
for f in manifest.json cells.idx contacts.pack items.pack npcs.pack quests.pack spawns.pack; do
    [ -s "$REPO_DIR/assets/local_realm/catalog/$f" ] || {
        echo "error: assets/local_realm/catalog/$f is missing or empty; the local realm requires its world catalog" >&2
        exit 1
    }
done

python3 "$REPO_DIR/tools/ps4/verify_shaders.py"

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR/sce_sys/about" "$OUT_DIR/sce_module"

echo "==> eboot.bin"
"$TOOLS/create-fself" -in="$ELF" -out="$OUT_DIR/wowee.oelf" --eboot "$OUT_DIR/eboot.bin" --paid 0x3800000000000011
rm -f "$OUT_DIR/wowee.oelf"

echo "==> sce_sys"
cp "$REPO_DIR/ps4/sce_sys/icon0.png" "$OUT_DIR/sce_sys/icon0.png"
cp "$REPO_DIR/ps4/sce_sys/about/right.sprx" "$OUT_DIR/sce_sys/about/right.sprx"
# pic1.png is the background the console shows behind the title on the home
# screen. Optional, and the repository does not ship one: staged when it is
# there so adding the artwork is a matter of dropping the file in, rather than
# also having to find and edit this script.
[ -f "$REPO_DIR/ps4/sce_sys/pic1.png" ] && cp "$REPO_DIR/ps4/sce_sys/pic1.png" "$OUT_DIR/sce_sys/pic1.png"
# Runtime support modules come from the OpenOrbis samples; rendering uses
# the bundled Vulkan/GNM implementation.
for m in libc.prx libSceFios2.prx; do
    if [ -f "$OO_PS4_TOOLCHAIN/samples/hello_world/sce_module/$m" ]; then
        cp "$OO_PS4_TOOLCHAIN/samples/hello_world/sce_module/$m" "$OUT_DIR/sce_module/$m"
    fi
done

SFO="$OUT_DIR/sce_sys/param.sfo"
"$TOOLS/PkgTool.Core" sfo_new "$SFO"
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" APP_TYPE --type Integer --maxsize 4 --value 1
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" APP_VER --type Utf8 --maxsize 8 --value "$VERSION"
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" ATTRIBUTE --type Integer --maxsize 4 --value 0
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" CATEGORY --type Utf8 --maxsize 4 --value 'gd'
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" CONTENT_ID --type Utf8 --maxsize 48 --value "$CONTENT_ID"
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" SYSTEM_VER --type Integer --maxsize 4 --value 0
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" TITLE --type Utf8 --maxsize 128 --value "$TITLE"
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" TITLE_ID --type Utf8 --maxsize 12 --value "$TITLE_ID"
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" VERSION --type Utf8 --maxsize 8 --value "$VERSION"

echo "==> runtime files"
# Reusing an output directory must not nest Data/Data, addons/addons or
# local_realm/local_realm, or retain files removed from the current source.
# These are generated staging directories. Never clean an output that overlaps
# the source runtime directories, including symlink aliases.
python3 - "$REPO_DIR" "$OUT_DIR" <<'PY'
from pathlib import Path
import shutil, sys
source = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve()
targets = [output / name for name in ('assets', 'Data', 'addons')]
protected = [source / name for name in ('assets', 'Data', 'addons')]
for target in targets:
    resolved = target.resolve()
    if target.is_symlink() or any(resolved == p or resolved in p.parents or p in resolved.parents for p in protected):
        raise SystemExit('Refusing runtime staging cleanup inside or over source runtime files')
for target in targets:
    if target.exists():
        shutil.rmtree(target)
PY
# Shaders: the same committed SPIR-V desktop uses (ps4_vulkan is a real
# Vulkan ICD; opengnm-psbc compiles SPIR-V to PS4 GCN at shader-module
# creation time, no offline GLSL ES conversion step), the client's own
# textures, protocol tables and addons.
mkdir -p "$OUT_DIR/assets/shaders"
find "$REPO_DIR/assets/shaders" -maxdepth 1 -name '*.spv' -exec cp {} "$OUT_DIR/assets/shaders/" \;
for d in textures local_realm; do
    [ -d "$REPO_DIR/assets/$d" ] && cp -r "$REPO_DIR/assets/$d" "$OUT_DIR/assets/$d"
done
for f in grass_biomes.json krayonload.png krayonsignin.png WoWPS.png; do
    [ -f "$REPO_DIR/assets/$f" ] && cp "$REPO_DIR/assets/$f" "$OUT_DIR/assets/$f"
done
cp -r "$REPO_DIR/Data" "$OUT_DIR/Data"
if [ -d "$REPO_DIR/addons" ]; then
    cp -r "$REPO_DIR/addons" "$OUT_DIR/addons"
    cmake -DDIR="$OUT_DIR/addons" -P "$REPO_DIR/cmake/strip_saved_vars.cmake"
fi

echo "==> pkg"
( cd "$OUT_DIR" && \
  FILES="$(find . -type f ! -name '*.gp4' ! -name '*.pkg' | sed 's#^\./##' | tr '\n' ' ')" && \
  "$TOOLS/create-gp4" -out pkg.gp4 --content-id="$CONTENT_ID" --files "$FILES" && \
  python3 "$REPO_DIR/tools/ps4/gp4_rootdir.py" pkg.gp4 . && \
  "$TOOLS/PkgTool.Core" pkg_build pkg.gp4 . )

echo
echo "Package: $OUT_DIR/$CONTENT_ID.pkg"
