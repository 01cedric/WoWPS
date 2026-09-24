#include "game/local_gameplay.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
using namespace wowee::game;

int main() {
    LocalRealmPlayer p;
    assert(p.phaseMask == 1);

    LocalScriptTrigger accept;
    accept.kind = LocalScriptTriggerKind::QuestAccept;
    accept.sourceId = 100;
    accept.scriptId = 500;
    accept.valueOp = LocalScriptValueOp::Set;
    accept.value = 1;
    accept.addPhaseMask = 2;
    assert(validLocalScriptTrigger(accept));
    assert(localApplyScriptTrigger(p, accept));
    assert(localScriptState(p, 500) == 1);
    assert(p.phaseMask == 3);

    LocalScriptTrigger complete;
    complete.kind = LocalScriptTriggerKind::QuestComplete;
    complete.sourceId = 100;
    complete.requiredScriptId = 500;
    complete.requiredValue = 1;
    complete.scriptId = 500;
    complete.valueOp = LocalScriptValueOp::Add;
    complete.value = 1;
    complete.addPhaseMask = 4;
    complete.removePhaseMask = 2;
    assert(localScriptTriggerMatches(p, complete));
    assert(localApplyScriptTrigger(p, complete));
    assert(localScriptState(p, 500) == 2);
    assert(p.phaseMask == 5);

    LocalScriptTrigger reward;
    reward.kind = LocalScriptTriggerKind::QuestReward;
    reward.sourceId = 100;
    reward.requiredScriptId = 500;
    reward.requiredValue = 2;
    reward.scriptId = 500;
    reward.valueOp = LocalScriptValueOp::Set;
    reward.value = 3;
    reward.addPhaseMask = 8;
    reward.removePhaseMask = 4;
    assert(localApplyScriptTrigger(p, reward));
    assert(localScriptState(p, 500) == 3);
    assert(p.phaseMask == 9);

    LocalScriptTrigger mismatch = reward;
    mismatch.requiredValue = 77;
    const auto before = p;
    assert(!localScriptTriggerMatches(p, mismatch));
    assert(!localApplyScriptTrigger(p, mismatch));
    assert(p.phaseMask == before.phaseMask && p.scriptStates == before.scriptStates);

    LocalScriptTrigger talk;
    talk.kind = LocalScriptTriggerKind::NpcTalk;
    talk.sourceId = 200;
    talk.scriptId = 600;
    talk.valueOp = LocalScriptValueOp::Add;
    talk.value = 1;
    assert(localApplyScriptTrigger(p, talk));
    assert(localApplyScriptTrigger(p, talk));
    assert(localScriptState(p, 600) == 2);

    LocalScriptTrigger kill = talk;
    kill.kind = LocalScriptTriggerKind::NpcKill;
    kill.sourceId = 201;
    kill.scriptId = 601;
    kill.value = -1;
    assert(localApplyScriptTrigger(p, kill));
    assert(localScriptState(p, 601) == -1);

    LocalScriptTrigger bad = talk;
    bad.addPhaseMask = 2;
    bad.removePhaseMask = 2;
    assert(!validLocalScriptTrigger(bad));

    LocalScriptTrigger zeroPhase;
    zeroPhase.kind = LocalScriptTriggerKind::NpcTalk;
    zeroPhase.sourceId = 202;
    zeroPhase.removePhaseMask = p.phaseMask;
    assert(validLocalScriptTrigger(zeroPhase));
    assert(!localApplyScriptTrigger(p, zeroPhase));
    assert(p.phaseMask == 9);

    LocalRealmPlayer maxed;
    LocalScriptTrigger setMax;
    setMax.kind = LocalScriptTriggerKind::NpcTalk;
    setMax.sourceId = 203;
    setMax.scriptId = 700;
    setMax.valueOp = LocalScriptValueOp::Set;
    setMax.value = std::numeric_limits<int32_t>::max();
    assert(localApplyScriptTrigger(maxed, setMax));
    LocalScriptTrigger overflow = setMax;
    overflow.valueOp = LocalScriptValueOp::Add;
    overflow.value = 1;
    assert(!localApplyScriptTrigger(maxed, overflow));
    assert(localScriptState(maxed, 700) == std::numeric_limits<int32_t>::max());

    std::cout << "PASS local quest/event script transitions + phase changes\n";
}
