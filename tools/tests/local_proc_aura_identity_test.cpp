#include "local_group_rewards_fixture.hpp"
#include "game/local_aura_identity.hpp"
#include <iostream>

static LocalStatAura baseAura(uint32_t spell=1126,uint64_t caster=1) {
    LocalStatAura aura{spell,60000,0,0,caster};aura.procCharges=2;return aura;
}
static void preparedIdentity() {
    auto owner=rewardPlayer(1);
    auto original=baseAura();localPrepareAuraApplication(owner,original);
    owner.statAuras.push_back(original);
    const auto prepared=owner.statAuras.front();
    assert(prepared.applicationGeneration&&localFindAuraApplication(owner,prepared));
    // An earlier callback erases and recreates exactly the same stored values
    // at exactly the same slot. The production prepared-dispatch selector must
    // reject it even though all persistent fields match the old application.
    owner.statAuras.clear();auto recreated=baseAura();
    localPrepareAuraApplication(owner,recreated);owner.statAuras.push_back(recreated);
    auto withoutGeneration=recreated;withoutGeneration.applicationGeneration=prepared.applicationGeneration;
    assert(withoutGeneration==prepared);
    assert(!localFindAuraApplication(owner,prepared));
    assert(localFindAuraApplication(owner,recreated));
    // A live refresh is still the original application, despite changed
    // duration, stacks, charge or cooldown values.
    auto refreshed=baseAura();refreshed.remainingMs=30000;refreshed.stacks=2;
    refreshed.procCharges=3;refreshed.procCooldownMs=500;
    localPrepareAuraApplication(owner,refreshed,&owner.statAuras.front());
    owner.statAuras.front()=refreshed;
    assert(localFindAuraApplication(owner,recreated)==&owner.statAuras.front());
    assert(refreshed.applicationGeneration==recreated.applicationGeneration);
    // Unrelated vector compaction does not discard a prepared application.
    owner.statAuras.insert(owner.statAuras.begin(),baseAura(1127));
    ensureLocalAuraApplication(owner.statAuras.front());
    assert(localFindAuraApplication(owner,recreated)==&owner.statAuras[1]);
    owner.statAuras.erase(owner.statAuras.begin());
    assert(localFindAuraApplication(owner,recreated)==&owner.statAuras.front());
    // Each distinct replacement requires a fresh token; legacy self-caster
    // zero and the explicit owner GUID describe the same caster on refresh.
    for(unsigned kind=0;kind<5;++kind) {
        auto prior=refreshed;auto fresh=baseAura();
        if(kind==0)prior.remainingMs=0;
        if(kind==1)++fresh.spellId;
        if(kind==2)++fresh.casterGuid;
        if(kind==3)++fresh.mapId;
        if(kind==4)++fresh.instanceId;
        localPrepareAuraApplication(owner,fresh,&prior);
        assert(fresh.applicationGeneration!=prior.applicationGeneration);
    }
    auto legacy=baseAura(1126,0),explicitSelf=baseAura();
    assert(!legacy.applicationGeneration);localPrepareAuraApplication(owner,explicitSelf,&legacy);
    assert(legacy.applicationGeneration==explicitSelf.applicationGeneration);
    auto secondOwner=rewardPlayer(2);auto secondAura=baseAura();
    localPrepareAuraApplication(secondOwner,secondAura);
    assert(secondAura.applicationGeneration!=explicitSelf.applicationGeneration);
}
static void publicCastLifecycle() {
    auto content=rewardContent();auto& spell=content->spells.front();
    spell.damage=0;spell.buffArmor=10;spell.durationMs=60000;
    auto owner=rewardPlayer(1);LocalGameplay game;game.useContent(content);std::string message;
    const auto cast=[&] {
        owner.globalCooldownMs=0;owner.cooldowns.clear();owner.categoryCooldowns.clear();
        assert(game.execute(owner,{LocalAction::CastSpell,owner.guid,spell.id},{&owner},message));
        assert(owner.statAuras.size()==1&&owner.statAuras.front().applicationGeneration);
        return owner.statAuras.front();
    };
    const auto first=cast();const auto refreshed=cast();
    assert(first.applicationGeneration==refreshed.applicationGeneration);
    // The real cancellation API removes the aura, and casting the same buff
    // with the same values recreates it. Prepared callbacks cannot see it as
    // the removed application.
    assert(game.execute(owner,{LocalAction::CancelStatAura,0,spell.id},{&owner},message));
    assert(owner.statAuras.empty());const auto replacement=cast();
    assert(replacement.applicationGeneration!=first.applicationGeneration);
    assert(!localFindAuraApplication(owner,first));
    owner.statAuras.front().remainingMs=0;const auto expiredReplacement=cast();
    assert(expiredReplacement.applicationGeneration!=replacement.applicationGeneration);
}
int main() {
    preparedIdentity();publicCastLifecycle();
    std::cout<<"PASS aura application identity: byte-identical remove/recreate rejects stale prepared selection; refresh and vector compaction retain selection; caster/rank/map/instance/expiry create new identities; real CastSpell/CancelStatAura lifecycle; runtime-only lazy legacy identity\n";
}
