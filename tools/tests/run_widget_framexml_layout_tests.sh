#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
: "${WOWPS_RETAIL_FRAME_XML_DIR:?Set to the extracted 3.3.5 FrameXML directory containing MainMenuBar.xml and ContainerFrame.xml}"
TEST_DIR="$(mktemp -d)"
mapfile -t LUA_SOURCES < <(rg --files "$ROOT/extern/lua-5.1.5/src" -g '*.c' -g '!lua.c' -g '!luac.c' -g '!print.c')
SAN_FLAGS=()
if [ "${SANITIZE:-0}" = 1 ]; then SAN_FLAGS=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
(
    cd "$TEST_DIR"
    "${CC:-cc}" -O1 -g -w "${SAN_FLAGS[@]}" -c "${LUA_SOURCES[@]}"
    "${CXX:-c++}" -std=c++20 -O1 -g "${SAN_FLAGS[@]}" -DWOWEE_PS4 -I"$ROOT/include" \
        -c "$ROOT/src/ui/settings_schema.cpp" -o settings_schema.o
    "${CXX:-c++}" -std=c++20 -O1 -g "${SAN_FLAGS[@]}" \
        -I"$ROOT/include" -I"$ROOT/extern/lua-5.1.5/src" \
        "$ROOT/tools/tests/widget_framexml_layout_test.cpp" \
        "$ROOT/src/ui/widget_tree.cpp" "$ROOT/src/ui/xml_parser.cpp" "$ROOT/src/ui/framexml_emitter.cpp" \
        ./*.o -lm -ldl -o widget_framexml_layout_test
    ./widget_framexml_layout_test "$WOWPS_RETAIL_FRAME_XML_DIR"
)
printf 'Test executable: %s/widget_framexml_layout_test\n' "$TEST_DIR"
