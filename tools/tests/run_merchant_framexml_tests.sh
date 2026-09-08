#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
: "${WOWPS_RETAIL_FRAME_XML_DIR:?Set to the extracted 3.3.5 FrameXML directory containing MerchantFrame.lua}"
TEST_DIR="$(mktemp -d)"
mapfile -t LUA_SOURCES < <(rg --files "$ROOT/extern/lua-5.1.5/src" -g '*.c' -g '!lua.c' -g '!luac.c' -g '!print.c')
SAN_FLAGS=()
if [ "${SANITIZE:-0}" = 1 ]; then SAN_FLAGS=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
(
 cd "$TEST_DIR"
 "${CC:-cc}" -O1 -g -w -c "${LUA_SOURCES[@]}"
 "${CXX:-c++}" -std=c++20 -O1 -g "${SAN_FLAGS[@]}" -I"$ROOT/include" -I"$ROOT/extern/lua-5.1.5/src" \
    "$ROOT/tools/tests/merchant_framexml_test.cpp" ./*.o -lm -ldl -o merchant_framexml_test
 ./merchant_framexml_test "$ROOT/tools/tests/merchant_framexml_retail_test.lua" "$WOWPS_RETAIL_FRAME_XML_DIR"
)
