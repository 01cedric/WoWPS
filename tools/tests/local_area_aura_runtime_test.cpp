#include "local_group_rewards_fixture.hpp"
#include "game/local_area_aura.hpp"
#include "game/local_party.hpp"
#include "game/local_proc_rules.hpp"
#include <iostream>

// P03/D2 authority runtime for raid area auras. The seven Retribution Aura
// ranks' source columns are pinned against the real client records by the
// import audit; this fixture exercises the emitter/recipient pair itself:
// activation, per-caster exclusivity, the reconciliation pass, membership and
// radius eligibility, multi-source dominance and the damage-shield retaliation.
namespace {
using namespace wowee::game;

constexpr uint32_t kRankOne=7294,kRankTwo=10298,kOtherAura=465;
constexpr uint32_t kAmountOne=10,kAmountTwo=18,kAmountOther=5;
constexpr float kRadius=40.0f;

LocalSpellDefinition areaAura(uint32_t id,uint32_t amount,uint32_t familyFlag2) {
    LocalSpellDefinition d;d.id=id;d.name="Area aura "+std::to_string(id);d.clientSpell=true;
    d.allowableClasses=0x5ff;d.schoolMask=2;d.range=0;d.resourceType=255;
    d.spellFamily=10;d.spellFamilyFlags={8,0,familyFlag2};
    d.areaAuraProfile=1;d.areaAuraEffectMask=0x7;d.areaAuraTypes={15,79,193};
    d.areaAuraAmounts={int32_t(amount),0,0};d.areaAuraMiscValues={0,127,0};
    d.areaAuraRadius=kRadius;d.indefiniteDuration=true;d.buffSelfOnly=true;
    return d;
}
std::shared_ptr<LocalWorldContent> areaContent() {
    auto c=rewardContent();
    c->spells.push_back(areaAura(kRankOne,kAmountOne,0x20));
    c->spells.push_back(areaAura(kRankTwo,kAmountTwo,0x20));
    // A different exclusivity group: same family, different family flags.
    c->spells.push_back(areaAura(kOtherAura,kAmountOther,0x40));
    std::sort(c->spells.begin(),c->spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    return c;
}
LocalRealmPlayer auraPlayer(uint64_t guid) {
    auto p=rewardPlayer(guid);p.level=80;p.health=p.maxHealth=10000;
    p.knownSpells={1,kOtherAura,kRankOne,kRankTwo};return p;
}
bool cast(LocalGameplay& game,LocalRealmPlayer& p,const std::vector<LocalRealmPlayer*>& roster,uint32_t spellId) {
    p.globalCooldownMs=0;p.cooldowns.clear();std::string result;
    const bool ok=game.execute(p,{LocalAction::CastSpell,0,spellId},roster,result);
    if(!ok)std::cerr<<"cast "<<spellId<<": "<<result<<'\n';
    return ok;
}
const LocalAreaAuraApplication* application(const LocalRealmPlayer& p,uint32_t spellId,uint64_t emitter) {
    for(const auto& a:p.areaAuras)if(a.spellId==spellId&&a.emitterGuid==emitter)return &a;
    return nullptr;
}
// The authority advances at most a quarter second per tick and reconciles on
// the source-independent 500 ms cadence, so one reconciliation is two ticks.
void reconcile(LocalGameplay& game,const std::vector<LocalRealmPlayer*>& roster) {
    game.tick(0.25f,roster);game.tick(0.25f,roster);
}
LocalParty party(uint32_t id,std::vector<uint64_t> members) {
    LocalParty out;out.id=id;out.members=std::move(members);return out;
}
}

int main() {
    // --- Activation, and the emitter's own unconditional application --------
    {
        LocalGameplay game;game.useContent(areaContent());
        auto p=auraPlayer(1);std::vector<LocalRealmPlayer*> roster{&p};
        assert(p.areaEmitters.empty()&&p.areaAuras.empty());
        assert(cast(game,p,roster,kRankOne));
        assert(p.areaEmitters.size()==1);
        const auto& emitter=p.areaEmitters[0];
        assert(emitter.spellId==kRankOne&&emitter.amount==kAmountOne&&emitter.effectMask==0x7);
        assert(emitter.mapId==p.mapId&&emitter.instanceId==p.instanceId&&emitter.generation);
        assert(validLocalAreaAuraEmitters(p.areaEmitters));
        // UnitAura::FillTargetMap applies the aura to its own caster with no
        // membership or radius test. A solo caster still carries it.
        assert(p.areaAuras.size()==1);
        const auto* self=application(p,kRankOne,p.guid);
        assert(self&&self->effective&&self->amount==kAmountOne&&self->effectMask==0x7);
        assert(self->emitterGeneration==emitter.generation);
        assert(validLocalAreaAuraApplications(p.areaAuras));
        // An indefinite emitter is not a lease: it carries no stat aura and no
        // timer, and surviving many ticks must not expire it.
        assert(p.statAuras.empty());
        for(int i=0;i<64;++i)game.tick(0.25f,roster);
        assert(p.areaEmitters.size()==1&&p.areaAuras.size()==1&&p.areaAuras[0].effective);
    }
    // --- One aura of a group per caster; a different group coexists ---------
    {
        LocalGameplay game;game.useContent(areaContent());
        auto p=auraPlayer(1);std::vector<LocalRealmPlayer*> roster{&p};
        assert(cast(game,p,roster,kRankOne));
        assert(cast(game,p,roster,kRankTwo));
        // IsAuraExclusiveBySpecificPerCasterWith: the later rank replaces the
        // earlier one rather than adding a second emitter.
        assert(p.areaEmitters.size()==1&&p.areaEmitters[0].spellId==kRankTwo);
        assert(p.areaAuras.size()==1&&p.areaAuras[0].spellId==kRankTwo&&p.areaAuras[0].amount==kAmountTwo);
        // A different exclusivity group is not replaced.
        assert(cast(game,p,roster,kOtherAura));
        assert(p.areaEmitters.size()==2&&validLocalAreaAuraEmitters(p.areaEmitters));
        assert(p.areaAuras.size()==2);
        assert(application(p,kRankTwo,1)&&application(p,kOtherAura,1));
        // Cancellation removes the emitter and every derived application it
        // produced, in one step.
        std::string result;
        assert(game.execute(p,{LocalAction::CancelStatAura,0,kRankTwo},roster,result));
        assert(p.areaEmitters.size()==1&&p.areaEmitters[0].spellId==kOtherAura);
        assert(p.areaAuras.size()==1&&p.areaAuras[0].spellId==kOtherAura);
        assert(!game.execute(p,{LocalAction::CancelStatAura,0,kRankTwo},roster,result));
    }
    // --- Membership and radius eligibility ----------------------------------
    {
        LocalGameplay game;game.useContent(areaContent());
        auto caster=auraPlayer(1),ally=auraPlayer(2),stranger=auraPlayer(3);
        ally.x=5;stranger.x=5;
        std::vector<LocalRealmPlayer*> roster{&caster,&ally,&stranger};
        assert(game.setPartyMembership({party(1,{1,2})}));
        assert(cast(game,caster,roster,kRankOne));
        assert(application(caster,kRankOne,1));
        assert(application(ally,kRankOne,1));          // in party, in range
        assert(!application(stranger,kRankOne,1));     // in range, not in the party
        // Out of the source radius: removed on the next pass, immediately.
        ally.x=kRadius+1;
        reconcile(game,roster);
        assert(!application(ally,kRankOne,1));
        assert(application(caster,kRankOne,1));        // the caster is unconditional
        ally.x=5;reconcile(game,roster);
        assert(application(ally,kRankOne,1));
        // A different instance is never in range.
        ally.instanceId=4;reconcile(game,roster);
        assert(!application(ally,kRankOne,1));
        ally.instanceId=0;
        // A dead recipient holds nothing; a dead emitter projects nothing.
        ally.dead=true;reconcile(game,roster);
        assert(ally.areaAuras.empty());
        ally.dead=false;reconcile(game,roster);
        assert(application(ally,kRankOne,1));
        caster.dead=true;reconcile(game,roster);
        assert(caster.areaEmitters.empty()&&ally.areaAuras.empty());
    }
    // --- Multi-source dominance --------------------------------------------
    {
        LocalGameplay game;game.useContent(areaContent());
        auto weak=auraPlayer(1),strong=auraPlayer(2),recipient=auraPlayer(3);
        strong.x=2;recipient.x=4;
        std::vector<LocalRealmPlayer*> roster{&weak,&strong,&recipient};
        assert(game.setPartyMembership({party(1,{1,2,3})}));
        assert(cast(game,weak,roster,kRankOne));
        assert(cast(game,strong,roster,kRankTwo));
        reconcile(game,roster);
        const auto* fromWeak=application(recipient,kRankOne,1);
        const auto* fromStrong=application(recipient,kRankTwo,2);
        assert(fromWeak&&fromStrong);
        // Both applications coexist; only the larger amount keeps its effects,
        // and the loser is kept effect-free rather than removed.
        assert(!fromWeak->effective&&fromStrong->effective);
        assert(fromWeak->effectMask==0x7); // retained, so a dominant neighbour is still findable
        assert(localAreaAuraShieldAmount(recipient.areaAuras,recipient.mapId,recipient.instanceId)==kAmountTwo);
        // A recipient's own aura wins an exact tie against a neighbour's.
        auto selfCaster=auraPlayer(4);selfCaster.x=6;
        std::vector<LocalRealmPlayer*> tie{&weak,&selfCaster};
        assert(game.setPartyMembership({party(2,{1,4})}));
        LocalGameplay second;second.useContent(areaContent());
        assert(second.setPartyMembership({party(2,{1,4})}));
        assert(cast(second,weak,tie,kRankOne));
        assert(cast(second,selfCaster,tie,kRankOne));
        reconcile(second,tie);
        const auto* own=application(selfCaster,kRankOne,4);
        const auto* neighbour=application(selfCaster,kRankOne,1);
        assert(own&&neighbour&&own->effective&&!neighbour->effective);
    }
    // --- The damage shield retaliates from the melee path -------------------
    {
        LocalGameplay game;game.useContent(areaContent());
        auto p=auraPlayer(1);std::vector<LocalRealmPlayer*> roster{&p};
        assert(cast(game,p,roster,kRankOne));
        auto enemy=rewardNpc();enemy.level=80;enemy.health=enemy.maxHealth=1000000;
        enemy.aggressive=true;enemy.targetGuid=p.guid;enemy.threat[0]={p.guid,1000};
        enemy.attackTimer=0;enemy.homeX=enemy.homeY=enemy.homeZ=0;
        game.setRemoteNpcs({enemy});
        uint64_t mark=game.combatEvents().empty()?0:game.combatEvents().back().sequence;
        bool swung=false,retaliated=false;
        for(int i=0;i<64&&!retaliated;++i) {
            if(game.npcs().empty())game.setRemoteNpcs({enemy});
            game.tick(0.5f,roster);
            for(const auto& e:game.combatEvents()) {
                if(e.sequence<=mark)continue;
                if(e.kind==LocalCombatEventKind::NpcMelee&&e.target==p.guid&&e.effective)swung=true;
                if(e.kind==LocalCombatEventKind::ProcDamage&&e.auraSpell==kRankOne) {
                    retaliated=true;
                    // Unit.cpp:2181 credits the RECIPIENT as the damage source.
                    assert(e.source==p.guid&&e.target==enemy.guid);
                    assert(e.attempted>=kAmountOne&&e.schoolMask==2);
                }
            }
        }
        assert(swung&&retaliated);
        // An application whose effects were stripped retaliates for nothing.
        auto stripped=p.areaAuras;assert(stripped.size()==1);
        stripped[0].effective=false;
        assert(localAreaAuraShieldAmount(stripped,p.mapId,p.instanceId)==0);
    }
    std::cout<<"PASS P03/D2 raid area aura runtime: activation with the caster's unconditional application, "
               "one aura per caster per exclusivity group with rank replacement, cancellation at the emitter, "
               "party and radius eligibility with immediate removal, dead emitter and dead recipient, "
               "multi-source dominance keeping the loser effect-free, self-cast tie-break, and the damage "
               "shield retaliating from the melee path with the recipient credited as its source\n";
    return 0;
}
