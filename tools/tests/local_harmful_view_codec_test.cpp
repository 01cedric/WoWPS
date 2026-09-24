// LAN108 owner-progress codec for creature aura views: the resolved armor
// modifier, slow percentage, armor percentage and control kind (LAN104-106)
// travel with every harmful row, LAN107 appended the remaining resolved
// amounts (attack power, flat and percentage damage done and taken with their
// school, healing taken, haste) and the break-on-damage flag, LAN108 the cast
// speed, hit chance, dodge/parry/block, a scoped resistance and the disarm
// flag; then the knockback block and the school lockouts; malformed amounts
// are rejected by the reader. LAN109 (2.40) appends the owner's gossip page
// (ids only) to the progress snapshot; this test covers that block too.
#include "game/local_bots.hpp"
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include <cassert>
#include <iostream>

using namespace wowee::game;

int main() {
    static_assert(lan::GameplayVersion==109);
    static_assert(HarmfulViewWireBytes==62&&KnockbackWireBytes==20&&kLocalMaxSchoolLockouts==8);
    LocalRealmPlayer p;p.guid=7;
    p.healingAuras.push_back({48441,9000,15000,7,1});
    p.harmfulAuras.push_back({20822,3000,4000,0xf130000000000014ULL,1,0,50});
    p.harmfulAuras.push_back({3396,29000,30000,0xf130000000000015ULL,1,-67,0});
    p.harmfulAuras.push_back({6016,19000,20000,0xf130000000000016ULL,1,0,0,-50});
    p.harmfulAuras.push_back({6253,1000,1000,0xf130000000000017ULL,1,0,0,0,1});
    // LAN107 rows: a fear that breaks on damage, a silence, a curse of weakness
    // style attack-power and damage-done reduction, a haste/healing debuff.
    {
        LocalHealingAuraView fear;fear.spellId=12542;fear.remainingMs=2500;fear.durationMs=4000;fear.casterGuid=0xf130000000000018ULL;fear.controlKind=3;fear.breakOnDamage=true;p.harmfulAuras.push_back(fear);
        LocalHealingAuraView silence;silence.spellId=15487;silence.remainingMs=5000;silence.durationMs=5000;silence.casterGuid=0xf130000000000019ULL;silence.controlKind=5;p.harmfulAuras.push_back(silence);
        // LAN108 amounts ride the last two rows (the budget is eight rows): a
        // disarm that lowers hit and avoidance on the weakness, a cast slow
        // with a frost resistance debuff on the wound.
        LocalHealingAuraView weakness;weakness.spellId=7646;weakness.remainingMs=100000;weakness.durationMs=120000;weakness.casterGuid=0xf13000000000001aULL;weakness.attackPower=-21;weakness.damageDoneFlat=-5;weakness.damageDonePct=-10;weakness.schoolMask=1;
        weakness.disarmed=true;weakness.hitChancePct=-5;weakness.dodgePct=-10;weakness.parryPct=-15;weakness.blockPct=-20;p.harmfulAuras.push_back(weakness);
        LocalHealingAuraView wound;wound.spellId=13218;wound.remainingMs=10000;wound.durationMs=15000;wound.casterGuid=0xf13000000000001bULL;wound.healingPct=-50;wound.hastePct=-20;wound.damageTakenPct=25;wound.damageTakenFlat=10;wound.schoolMask=127;
        wound.castSpeedPct=-50;wound.resistance=-30;wound.resistanceSchool=16;p.harmfulAuras.push_back(wound);
    }
    p.knockbackSequence=3;p.knockbackCos=0.6f;p.knockbackSin=-0.8f;p.knockbackSpeedXY=15.f;p.knockbackSpeedZ=9.f;
    p.schoolLockouts.push_back({4,4000});p.schoolLockouts.push_back({16,2500});
    Writer w;writeHealingViews(w,p);
    // Healing rows keep their 21-byte shape; harmful rows are 62 bytes at
    // LAN108, then the 20-byte knockback block and 5 bytes per lockout.
    assert(w.bytes.size()==1+21+1+8*HarmfulViewWireBytes+KnockbackWireBytes+1+2*5);
    LocalRealmPlayer q;q.guid=7;Reader r(w.bytes.data(),w.bytes.size());
    assert(readHealingViews(r,q)&&r.done()&&q.healingAuras==p.healingAuras&&q.harmfulAuras==p.harmfulAuras);
    assert(q.schoolLockouts==p.schoolLockouts&&q.knockbackSequence==3&&q.knockbackCos==0.6f&&q.knockbackSin==-0.8f&&q.knockbackSpeedXY==15.f&&q.knockbackSpeedZ==9.f);
    assert(localPlayerControl(q)==(1u|4u|16u));
    assert(localPlayerMovementHeld(q)&&localPlayerSchoolLocked(q,4)&&localPlayerSchoolLocked(q,20)&&!localPlayerSchoolLocked(q,2));
    const auto mods=localPlayerViewModifiers(q,1);
    assert(mods.attackPower==-21&&mods.damageDoneFlat==-5&&mods.damageDonePct==-10&&mods.damageTakenPct==25&&mods.damageTakenFlat==10&&mods.hastePct==-20);
    assert(localPlayerViewModifiers(q,2).damageDonePct==0&&localPlayerViewModifiers(q,2).damageTakenPct==25);
    assert(localPlayerHealingTaken(q,1000)==500);
    // LAN108 modifiers: the strongest cast slow, the frost-scoped resistance,
    // the disarm and its hit and avoidance terms.
    assert(localPlayerViewModifiers(q,0).castSpeedPct==-50&&localPlayerCastTimeModified(q,2000)==3000&&localPlayerCastTimeModified(q,0)==0);
    assert(localPlayerViewModifiers(q,16).resistance==-30&&localPlayerViewModifiers(q,4).resistance==0);
    assert(localPlayerDisarmed(q)&&mods.hitChancePct==-5&&mods.dodgePct==-10&&mods.parryPct==-15&&mods.blockPct==-20&&mods.disarmed);
    std::cout<<"PASS LAN108 harmful aura views carry armor "<<q.harmfulAuras[1].armorModifier<<", slow "<<unsigned(q.harmfulAuras[0].slowPercent)<<" %, armor "<<int(q.harmfulAuras[2].armorPercent)<<" %, a stun, a fear, a silence, attack power "<<mods.attackPower<<", damage done "<<mods.damageDonePct<<" %, healing taken 50 %, haste "<<mods.hastePct<<" %, a knockback and two school lockouts the guest obeys\n";
    const auto rejects=[&](auto mutate) {
        auto bad=p;mutate(bad);Writer bw;writeHealingViews(bw,bad);
        LocalRealmPlayer out;Reader br(bw.bytes.data(),bw.bytes.size());return !readHealingViews(br,out);
    };
    assert(rejects([](auto& v){v.harmfulAuras[0].slowPercent=100;}));
    // 2.37: an armor bonus is a legal creature amount (MOD_RESISTANCE of any sign).
    assert(!rejects([](auto& v){v.harmfulAuras[1].armorModifier=5;}));
    assert(rejects([](auto& v){v.harmfulAuras[1].armorModifier=100001;}));
    assert(rejects([](auto& v){v.harmfulAuras[1].armorModifier=-100001;}));
    assert(!rejects([](auto& v){v.harmfulAuras[2].armorPercent=1;}));
    assert(rejects([](auto& v){v.harmfulAuras[2].armorPercent=-100;}));
    assert(rejects([](auto& v){v.harmfulAuras[3].controlKind=6;}));
    assert(!rejects([](auto& v){v.harmfulAuras[3].controlKind=5;}));
    // LAN107 bounds: a break flag needs a control, scoped modifiers need a
    // school, percentages stay within the authority's own clamps, a lockout
    // needs a school and a timer, the knockback stays finite and bounded.
    assert(rejects([](auto& v){v.harmfulAuras[6].breakOnDamage=true;}));
    assert(rejects([](auto& v){v.harmfulAuras[6].schoolMask=0;}));
    assert(rejects([](auto& v){v.harmfulAuras[6].schoolMask=128;}));
    assert(rejects([](auto& v){v.harmfulAuras[6].damageDonePct=-100;}));
    assert(rejects([](auto& v){v.harmfulAuras[7].healingPct=1;}));
    assert(rejects([](auto& v){v.harmfulAuras[7].healingPct=-101;}));
    assert(rejects([](auto& v){v.harmfulAuras[7].hastePct=1001;}));
    assert(rejects([](auto& v){v.harmfulAuras[6].attackPower=-100001;}));
    assert(rejects([](auto& v){v.schoolLockouts[0].schoolMask=0;}));
    assert(rejects([](auto& v){v.schoolLockouts[0].remainingMs=0;}));
    assert(rejects([](auto& v){v.schoolLockouts[0].remainingMs=600001;}));
    assert(rejects([](auto& v){for(unsigned i=0;i<7;++i)v.schoolLockouts.push_back({uint8_t(1u<<(i%7)),1000});}));
    assert(rejects([](auto& v){v.knockbackSpeedXY=-1.f;}));
    assert(rejects([](auto& v){v.knockbackSpeedZ=1001.f;}));
    assert(rejects([](auto& v){v.knockbackCos=2.f;}));
    // LAN108 bounds: a resistance needs a magic school, the cast slow and the
    // hit / avoidance terms stay within the authority's clamps.
    assert(rejects([](auto& v){v.harmfulAuras[7].resistanceSchool=0;}));
    assert(rejects([](auto& v){v.harmfulAuras[7].resistanceSchool=1;}));
    assert(rejects([](auto& v){v.harmfulAuras[7].resistance=100001;}));
    assert(rejects([](auto& v){v.harmfulAuras[7].castSpeedPct=-100;}));
    assert(rejects([](auto& v){v.harmfulAuras[7].castSpeedPct=1001;}));
    assert(rejects([](auto& v){v.harmfulAuras[6].hitChancePct=-101;}));
    assert(rejects([](auto& v){v.harmfulAuras[6].dodgePct=101;}));
    assert(!rejects([](auto& v){v.harmfulAuras[7].castSpeedPct=-99;v.harmfulAuras[7].resistance=-100000;v.harmfulAuras[6].hitChancePct=-100;v.harmfulAuras[6].blockPct=100;}));
    assert(!rejects([](auto& v){v.harmfulAuras[0].slowPercent=99;v.harmfulAuras[1].armorModifier=-100000;v.harmfulAuras[2].armorPercent=-99;v.harmfulAuras[7].hastePct=1000;v.harmfulAuras[7].healingPct=-100;}));
    std::cout<<"PASS reader rejects a 100 % slow, out-of-range armor, flat or percentage amounts, unscoped or over-range LAN107/LAN108 amounts, a break flag without a control, empty or overlong lockouts and unbounded knockbacks\n";
    // The owner-progress budget covers eight full harmful rows, the knockback
    // and eight lockouts, and a full worst-case view block fits it exactly.
    static_assert(MaxOwnerProgressBytes<=16384);
    {
        LocalRealmPlayer full;full.guid=7;
        for(unsigned i=0;i<kLocalMaxHealingAuraViews;++i){full.healingAuras.push_back({48441+i,9000,15000,7,1});
            LocalHealingAuraView a;a.spellId=7646+i;a.remainingMs=1000;a.durationMs=2000;a.casterGuid=0xf130000000000020ULL+i;a.schoolMask=1;a.damageDoneFlat=-1;full.harmfulAuras.push_back(a);}
        for(unsigned i=0;i<kLocalMaxSchoolLockouts;++i)full.schoolLockouts.push_back({uint8_t(1u<<(i%7)),1000+i});
        Writer fw;writeHealingViews(fw,full);
        assert(fw.bytes.size()==1+kLocalMaxHealingAuraViews*21+1+kLocalMaxHealingAuraViews*HarmfulViewWireBytes+KnockbackWireBytes+1+kLocalMaxSchoolLockouts*5);
        LocalRealmPlayer out;Reader fr(fw.bytes.data(),fw.bytes.size());assert(readHealingViews(fr,out)&&fr.done()&&out.harmfulAuras==full.harmfulAuras&&out.schoolLockouts==full.schoolLockouts);
    }
    std::cout<<"PASS owner-progress bound "<<MaxOwnerProgressBytes<<" bytes\n";
    // LAN109: the gossip page - the creature, the menu, the text, the
    // revision, a script-offered quest, the quest-list flag and the option
    // ids with their icon, type, action menu and price; the texts stay on the
    // guest's own catalog. A page without a creature reads as closed.
    {
        LocalRealmPlayer owner;owner.guid=7;
        owner.gossip.npcGuid=0xf130000000000014ULL;owner.gossip.menuId=7178;owner.gossip.textId=8458;owner.gossip.revision=12;owner.gossip.offeredQuestId=9180;owner.gossip.questMenu=true;
        for(unsigned i=0;i<3;++i){LocalGossipShownOption o;o.id=uint16_t(i);o.icon=uint8_t(i);o.type=i?3:1;o.actionMenuId=8312+i;o.boxMoney=i*500;o.text="Option "+std::to_string(i);owner.gossip.options.push_back(o);}
        Writer gw;writeGossip(gw,owner);
        assert(gw.bytes.size()==8+4+4+4+4+1+1+3*12);
        LocalRealmPlayer out;Reader gr(gw.bytes.data(),gw.bytes.size());
        assert(readGossip(gr,out)&&gr.done()&&out.gossip.npcGuid==owner.gossip.npcGuid&&out.gossip.menuId==7178&&out.gossip.textId==8458&&out.gossip.revision==12&&
               out.gossip.offeredQuestId==9180&&out.gossip.questMenu&&out.gossip.options.size()==3&&out.gossip.options[2].boxMoney==1000&&out.gossip.options[1].type==3&&out.gossip.options[1].text.empty());
        // A page of the maximum size fits the progress budget; unsorted option
        // ids and a quest-list flag above one are refused.
        LocalRealmPlayer full=owner;full.gossip.options.clear();
        for(unsigned i=0;i<kLocalMaxGossipOptions;++i){LocalGossipShownOption o;o.id=uint16_t(i);full.gossip.options.push_back(o);}
        Writer fw;writeGossip(fw,full);assert(fw.bytes.size()==GossipWireBytes&&GossipWireBytes==8+4+4+4+4+1+1+kLocalMaxGossipOptions*12);
        LocalRealmPlayer fullOut;Reader fr(fw.bytes.data(),fw.bytes.size());assert(readGossip(fr,fullOut)&&fr.done()&&fullOut.gossip.options.size()==kLocalMaxGossipOptions);
        auto bad=owner;std::swap(bad.gossip.options[0],bad.gossip.options[1]);
        Writer bw;writeGossip(bw,bad);LocalRealmPlayer badOut;Reader br(bw.bytes.data(),bw.bytes.size());assert(!readGossip(br,badOut));
        Writer qw;writeGossip(qw,owner);qw.bytes[8+4+4+4+4]=2;LocalRealmPlayer qOut;Reader qr(qw.bytes.data(),qw.bytes.size());assert(!readGossip(qr,qOut));
        LocalRealmPlayer closed;closed.guid=7;closed.gossip.menuId=7178;closed.gossip.options=owner.gossip.options;
        Writer cw;writeGossip(cw,closed);LocalRealmPlayer cOut;Reader cr(cw.bytes.data(),cw.bytes.size());assert(readGossip(cr,cOut)&&!cOut.gossip.open()&&cOut.gossip.options.empty()&&cOut.gossip.menuId==0);
    }
    std::cout<<"PASS LAN109 gossip page codec: ids only, "<<GossipWireBytes<<" bytes at most\n";
}
