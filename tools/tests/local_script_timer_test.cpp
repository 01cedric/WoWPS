#include "game/local_gameplay.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace wowee::game;

int main() {
    LocalRealmPlayer p;
    LocalScriptTrigger start;
    start.kind = LocalScriptTriggerKind::QuestAccept;
    start.sourceId = 100;
    start.scriptId = 500;
    start.valueOp = LocalScriptValueOp::Set;
    start.value = 1;
    start.addPhaseMask = 2;
    start.scheduleTimerId = 900;
    start.scheduleDelayMs = 5000;
    assert(validLocalScriptTrigger(start));
    assert(localApplyScriptTrigger(p, start));
    assert(localScriptState(p, 500) == 1);
    assert(p.phaseMask == 3);
    assert(p.scriptTimers.size() == 1);
    assert(p.scriptTimers[0] == (LocalScriptTimer{900, 5000}));

    // Scheduling the same timer restarts it instead of duplicating it.
    start.scriptId = 0;
    start.valueOp = LocalScriptValueOp::None;
    start.value = 0;
    start.addPhaseMask = 0;
    start.scheduleDelayMs = 1200;
    assert(localApplyScriptTrigger(p, start));
    assert(p.scriptTimers.size() == 1 && p.scriptTimers[0].remainingMs == 1200);

    LocalScriptTimerAction expire;
    expire.timerId = 900;
    expire.requiredScriptId = 500;
    expire.requiredValue = 1;
    expire.scriptId = 500;
    expire.valueOp = LocalScriptValueOp::Add;
    expire.value = 1;
    expire.addPhaseMask = 4;
    expire.removePhaseMask = 2;
    assert(validLocalScriptTimerAction(expire));
    assert(localApplyScriptTimerAction(p, expire));
    assert(localScriptState(p, 500) == 2);
    assert(p.phaseMask == 5);

    // Cancel is idempotent and a trigger may exist purely to cancel a timer.
    LocalScriptTrigger cancel;
    cancel.kind = LocalScriptTriggerKind::NpcTalk;
    cancel.sourceId = 200;
    cancel.cancelTimerId = 900;
    assert(validLocalScriptTrigger(cancel));
    assert(localApplyScriptTrigger(p, cancel));
    assert(p.scriptTimers.empty());
    assert(localApplyScriptTrigger(p, cancel));

    // Timer rows are strictly sorted and bounded.
    LocalRealmPlayer sorted;
    assert(localSetScriptTimer(sorted, 30, 300));
    assert(localSetScriptTimer(sorted, 10, 100));
    assert(localSetScriptTimer(sorted, 20, 200));
    assert(validLocalScriptTimers(sorted.scriptTimers));
    assert(sorted.scriptTimers[0].timerId == 10 && sorted.scriptTimers[2].timerId == 30);
    for (uint32_t id = 1; id <= kLocalMaxScriptTimers; ++id) {
        LocalRealmPlayer cap;
        for (uint32_t j=1;j<=kLocalMaxScriptTimers;++j) assert(localSetScriptTimer(cap,j,1000));
        assert(!localSetScriptTimer(cap,1000,1000));
        break;
    }

    // Invalid ambiguous timer rows are rejected atomically.
    const auto before = p;
    LocalScriptTrigger bad = cancel;
    bad.scheduleTimerId = 900;
    bad.scheduleDelayMs = 1000;
    bad.cancelTimerId = 900;
    assert(!validLocalScriptTrigger(bad));
    assert(!localApplyScriptTrigger(p, bad));
    assert(p.phaseMask == before.phaseMask && p.scriptStates == before.scriptStates && p.scriptTimers == before.scriptTimers);

    std::cout << "PASS persistent script timer schedule/restart/cancel + timed state action\n";
}
