#include "core/presentation_recovery.hpp"
#include <cassert>
#include <cstdio>
#include <stdexcept>
using namespace wowee::core;
int main() {
    unsigned failures=0,reclaimed=0,authorityTicks=0,portalCommands=0;
    for(unsigned tick=0;tick<35;++tick) {
        ++authorityTicks; // Kept outside retryable work, as in Application.
        const auto result=updatePresentationWithRecovery(failures,[]{throw std::bad_alloc();},[&]{++reclaimed;});
        assert(result==(tick<29?PresentationUpdateResult::Retry:PresentationUpdateResult::EndSession));
        if(result==PresentationUpdateResult::Complete)++portalCommands;
    }
    assert(failures==30&&reclaimed==35&&authorityTicks==35&&!portalCommands);
    assert(updatePresentationWithRecovery(failures,[]{},[&]{++reclaimed;})==PresentationUpdateResult::Complete);
    assert(failures==0&&reclaimed==35);
    try{(void)updatePresentationWithRecovery(failures,[]{throw std::runtime_error("not OOM");},[&]{++reclaimed;});assert(false);}catch(const std::runtime_error&){}
    assert(!failures&&reclaimed==35);
    std::puts("PASS: transient OOM recovers, 30 consecutive failures schedule bounded session exit, successes alone reset count, other exceptions propagate");
}
