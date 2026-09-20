#pragma once
#include "game/local_gameplay.hpp"
#include <atomic>
#include <cstdlib>

namespace wowee::game {
// Authority-session identity, deliberately absent from save and LAN payloads.
// A process counter also survives replacement of a LocalRealmPlayer value.
inline std::atomic<uint64_t> localAuraApplicationCounter{1};
inline uint64_t nextLocalAuraApplication() {
    const auto generation=localAuraApplicationCounter.fetch_add(1,std::memory_order_relaxed);
    // Never wrap into an identity already issued during this session.
    if(!generation)std::abort();
    return generation;
}
inline uint64_t ensureLocalAuraApplication(LocalStatAura& aura) {
    if(!aura.applicationGeneration)aura.applicationGeneration=nextLocalAuraApplication();
    return aura.applicationGeneration;
}
inline void localPrepareAuraApplication(const LocalRealmPlayer& owner,LocalStatAura& fresh,
                                        LocalStatAura* previous=nullptr) {
    // Refresh/stack of the same live application retains its identity. An
    // expired slot, another rank, caster, map or instance creates a new one.
    const bool refresh=previous&&previous->remainingMs&&previous->spellId==fresh.spellId&&
        previous->mapId==fresh.mapId&&previous->instanceId==fresh.instanceId&&
        (previous->casterGuid?previous->casterGuid:owner.guid)==(fresh.casterGuid?fresh.casterGuid:owner.guid);
    fresh.applicationGeneration=refresh?ensureLocalAuraApplication(*previous):nextLocalAuraApplication();
}
inline bool localSameAuraApplication(const LocalStatAura& current,const LocalStatAura& prepared) {
    return prepared.applicationGeneration&&current.applicationGeneration==prepared.applicationGeneration&&
        current.spellId==prepared.spellId&&current.mapId==prepared.mapId&&current.instanceId==prepared.instanceId;
}
inline LocalStatAura* localFindAuraApplication(LocalRealmPlayer& owner,const LocalStatAura& prepared) {
    // Erases can compact the vector during a nested callback. Slots and value
    // equality are neither necessary nor sufficient application identities.
    for(auto& aura:owner.statAuras)if(localSameAuraApplication(aura,prepared))return &aura;
    return nullptr;
}
}
