#include "game/local_gameplay.hpp"

#include <cassert>
#include <iostream>

using namespace wowee::game;

int main() {
    LocalRealmPlayer player;
    assert(player.phaseMask == 1);
    assert(localScriptState(player, 100) == 0);

    assert(localSetScriptState(player, 100, 7));
    assert(localSetScriptState(player, 50, -3));
    assert(localSetScriptState(player, 75, 11));
    assert(validLocalScriptStates(player.scriptStates));
    assert(player.scriptStates.size() == 3);
    assert(player.scriptStates[0].scriptId == 50);
    assert(player.scriptStates[1].scriptId == 75);
    assert(player.scriptStates[2].scriptId == 100);
    assert(localScriptState(player, 50) == -3);
    assert(localScriptState(player, 75) == 11);
    assert(localScriptState(player, 100) == 7);

    assert(localSetScriptState(player, 75, 0));
    assert(localScriptState(player, 75) == 0);
    assert(player.scriptStates.size() == 2);

    assert(localPhaseVisible(0x1u, 0, 0));
    assert(localPhaseVisible(0x4u, 0x4u, 0));
    assert(localPhaseVisible(0x6u, 0x4u, 0));
    assert(!localPhaseVisible(0x2u, 0x4u, 0));
    assert(!localPhaseVisible(0x4u, 0, 0x4u));
    assert(!localPhaseVisible(0x6u, 0x4u, 0x2u));

    LocalRealmPlayer full;
    for (uint32_t id = 1; id <= kLocalMaxScriptStates; ++id)
        assert(localSetScriptState(full, id, int32_t(id)));
    assert(validLocalScriptStates(full.scriptStates));
    assert(!localSetScriptState(full, 1000, 1));

    std::cout << "PASS local persistent script state + phase visibility\n";
    return 0;
}
