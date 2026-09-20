#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
bin=$(mktemp /tmp/wowps-session-cache-XXXXXX)
trap 'rm -f "$bin"' EXIT
${CXX:-c++} -std=c++17 -Wall -Wextra -fsanitize=address,undefined -I"$root/include" "$root/tools/tests/session_cache_test.cpp" -o "$bin"
# LeakSanitizer cannot run under this workspace's ptrace monitor. The allocator
# fixture independently counts all live blocks after each session release.
ASAN_OPTIONS=detect_leaks=0 "$bin"
python3 - "$root" <<'PY'
from pathlib import Path
import sys
r=Path(sys.argv[1]);s=(r/'src/core/application.cpp').read_text()
b=s[s.index('bool Application::rebuildSessionRenderer()'):s.index('void Application::performLogoutToLogin()')]
assert b.index('entitySpawner_.reset()') < b.index('gameHandler->resetDbcCaches()')
assert b.index('renderer.reset()') < b.index('gameHandler->resetDbcCaches()') < b.index('renderer = std::make_unique')
assert 'blockBytes=' in b and 'arenaMappedCumulative=' in b
s=(r/'src/rendering/renderer.cpp').read_text();b=s[s.index('bool Renderer::initialize('):s.index('void Renderer::shutdown()')]
assert b.index('checkpoint("charge")') < b.index('chargeEffect = std::make_unique')
for stage in ['per-frame resources','sky','weather','lightning','swim','mount dust','charge','level up','grass','quest markers','footprints','lighting','zone database','post process','render graph','overlay','complete']:
    assert 'checkpoint("'+stage+'")' in b
assert 'setVolumetricDebug' in b
print('PASS teardown-before-cache-release-before-rebuild order; all init checkpoints present; debug sink preserved')
PY
