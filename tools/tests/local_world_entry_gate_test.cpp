#include "core/local_world_entry_gate.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
using Gate = wowee::core::LocalWorldEntryGate;
using R = Gate::Result;
int main() {
    const Gate::Clock::time_point t{};
    using namespace std::chrono_literals;
    Gate gate;
    assert(gate.update(false, false, false, t) == R::Failed); // actual failed login
    gate.reset();
    assert(gate.update(true, false, true, t) == R::Waiting); // initial finalization
    assert(gate.update(true, true, false, t + 1s) == R::Ready);
    gate.beginRelocation();
    assert(gate.update(true, false, false, t + 2s) == R::Waiting); // before jobs enqueue
    assert(gate.update(true, false, true, t + 7s) == R::Waiting);
    assert(gate.update(true, true, true, t + 8s) == R::Ready); // destination done, neighbors pending
    assert(!gate.waiting());
    assert(gate.update(true, false, true, t + 9s) == R::Waiting); // old tiles retired
    assert(gate.update(true, false, true, t + 68s) == R::Waiting);
    assert(gate.update(true, false, true, t + 69s) == R::Failed); // stuck worker cannot wait forever
    gate.reset();
    assert(gate.update(true, true, false, t) == R::Ready);
    assert(gate.update(true, false, false, t + 1s) == R::Waiting);
    assert(gate.update(true, false, false, t + 6s) == R::Failed); // absent destination, no work
    gate.reset();
    assert(gate.update(true, false, false, t) == R::Failed); // new session gets no stale grace
    gate.beginRelocation();
    assert(gate.update(false, false, true, t) == R::Waiting); // cross-map avatar respawn
    assert(gate.update(false, true, false, t + 5s) == R::Failed); // missing avatar still fails
    std::cout << "PASS world-entry streaming gaps, relocation, recovery, missing data/avatar, bounded stuck worker and session reset\n";
}
