#include "game/local_melee.hpp"
#include "game/local_spell_amount.hpp"
#include "game/local_spell_import.hpp"
#include "pipeline/dbc_loader.hpp"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
using namespace wowee;
using namespace wowee::game;

int main(int argc,char** argv) {
    assert(argc==2);
    std::ifstream f(std::filesystem::path(argv[1])/"Spell.dbc",std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(f),{}};
    pipeline::DBCFile spells;assert(spells.load(bytes));
    uint32_t row=spells.getRecordCount();
    for(uint32_t i=0;i<spells.getRecordCount();++i)if(spells.getUInt32(i,0)==23881){row=i;break;}
    assert(row<spells.getRecordCount());
    assert(spells.getUInt32(row,71)==2&&spells.getUInt32(row,74)==1);
    const auto coefficient=uint32_t(spells.getInt32(row,80)+1);
    assert(coefficient==50&&spells.getUInt32(row,213)==2&&spells.getUInt32(row,225)==1);
    assert(spells.getInt32(row,68)==2&&spells.getUInt32(row,69)==173555);

    // Bloodthirst's scripted source formula is effect percentage of BASE_ATTACK
    // AP. Neither a weapon damage roll nor normalized weapon speed is added.
    assert(localMeleeSpecialAmount(201,coefficient)==100);
    assert(localMeleeSpecialAmount(1234.5f,coefficient)==617);
    assert(localMeleeSpecialAmount(0,coefficient)==0);
    assert(localMeleeSpecialAmount(-100,coefficient)==0);
    assert(localMeleeSpecialAmount(200,0)==0);
    assert(localMeleeSpecialAmount(std::numeric_limits<float>::quiet_NaN(),coefficient)==0);
    assert(localMeleeSpecialAmount(std::numeric_limits<float>::infinity(),coefficient)==0);
    assert(localMeleeSpecialAmount(std::numeric_limits<float>::max(),UINT32_MAX)==1000000);

    std::map<std::string,pipeline::DBCFile> tables;
    for(const auto* name:{"SpellRange","SpellCastTimes","SpellDuration","SpellIcon","Talent","TalentTab","SpellRuneCost","SpellRadius"}) {
        std::ifstream input(std::filesystem::path(argv[1])/(std::string(name)+".dbc"),std::ios::binary);
        std::vector<uint8_t> data{std::istreambuf_iterator<char>(input),{}};assert(tables[name].load(data));
    }
    auto get=[&](const char* name){return &tables.at(name);};
    LocalSpellImport imported;
    detail::importClientTalents(imported,get("Talent"),get("TalentTab"),&spells,get("SpellRange"),
        get("SpellCastTimes"),get("SpellDuration"),get("SpellIcon"),get("SpellRuneCost"),get("SpellRadius"));
    LocalWorldContent c;c.spells=std::move(imported.spells);
    std::sort(c.spells.begin(),c.spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    const auto* bloodthirst=c.spell(23881);
    assert(bloodthirst&&bloodthirst->unsupportedReason.empty()&&bloodthirst->damage==coefficient);
    LocalRealmPlayer allocation;allocation.classId=1;allocation.level=80;allocation.dead=false;
    std::string error;
    assert(!learnLocalTalent(allocation,c,2234,0,error)); // Still requires earlier Fury tiers.
    const uint32_t furySpells[]={56927,56929,56930,56931,56932};
    const auto apBase=localMeleeSpecialAmount(2000,coefficient);assert(apBase==1000);
    for(uint8_t rank=1;rank<=5;++rank) {
        const auto* fury=localTalentSpell(c,2234,rank);
        assert(fury&&fury->id==furySpells[rank-1]&&fury->unsupportedReason.empty()&&fury->passive);
        assert(fury->spellFamily==4&&fury->talentTab==164);
        unsigned activeModifiers=0;
        for(const auto& modifier:fury->passiveCastModifiers)if(modifier.active) {
            ++activeModifiers;
            assert(modifier.operation==0&&modifier.percentage&&modifier.amount==2*rank);
            assert((modifier.mask==std::array<uint32_t,3>{2097152,1028,0}));
        }
        assert(activeModifiers==1);
        // Component fixture: select the stored learned rank only. This does
        // not claim that the currently blocked talent tree is reachable.
        allocation.talents={{2234,rank}};
        assert(localSpellAmountAfterTalents(allocation,c,*bloodthirst,apBase,false)==1000+20*rank);
        assert(localSpellAmountAfterTalents(allocation,c,*bloodthirst,apBase,true)==apBase);
        auto wrongFamily=*bloodthirst;wrongFamily.spellFamily=8;
        assert(localSpellAmountAfterTalents(allocation,c,wrongFamily,apBase,false)==apBase);
        auto wrongMask=*bloodthirst;wrongMask.spellFamilyFlags={0,0,1};
        assert(localSpellAmountAfterTalents(allocation,c,wrongMask,apBase,false)==apBase);
    }
    // Rank replacement applies rank1's 2%, rather than adding any older ranks.
    allocation.talents={{2234,1}};
    assert(localSpellAmountAfterTalents(allocation,c,*bloodthirst,apBase,false)==1020);
    allocation.talents.clear();
    assert(localSpellAmountAfterTalents(allocation,c,*bloodthirst,apBase,false)==apBase);
    std::cout<<"PASS: real DBC Unending Fury ranks 1..5 apply 2/4/6/8/10 percent after Bloodthirst 50% AP; only selected learned rank contributes; family/mask/periodic filtering and reset; natural learning remains tier-gated\n";

    LocalRealmPlayer p;p.classId=1;p.level=40;p.x=0;p.y=0;
    LocalRealmNpc n;n.level=p.level;n.entry=0;n.x=1;n.y=0;n.orientation=0;
    LocalMeleeStats stats;stats.crit=20;stats.offHand=true;
    // Special attacks do not inherit the dual-wield white-swing miss penalty.
    assert(localRollPlayerMelee(p,n,stats,true,499,9999)==LocalMeleeOutcome::Miss);
    assert(localRollPlayerMelee(p,n,stats,true,500,9999)==LocalMeleeOutcome::Dodge);
    assert(localRollPlayerMelee(p,n,stats,true,999,9999)==LocalMeleeOutcome::Dodge);
    assert(localRollPlayerMelee(p,n,stats,true,1000,1999)==LocalMeleeOutcome::Critical);
    assert(localRollPlayerMelee(p,n,stats,true,1000,2000)==LocalMeleeOutcome::Hit);
    assert(localRollPlayerMelee(p,n,stats,false,1000,9999)==LocalMeleeOutcome::Miss);
    for(uint32_t roll=0;roll<10000;++roll) {
        const auto outcome=localRollPlayerMelee(p,n,stats,true,roll,9999);
        assert(outcome!=LocalMeleeOutcome::Glancing&&outcome!=LocalMeleeOutcome::Crushing);
    }
    // Production NPC metadata supplies the parry-capable humanoid; a rear
    // attack removes parry while preserving dodge and independent critical roll.
    uint32_t humanoid=0;
    n.orientation=3.14159265358979323846f;
    for(uint32_t entry=1;entry<=40000&&!humanoid;++entry) {
        n.entry=entry;
        if(localRollPlayerMelee(p,n,stats,true,1000,9999)==LocalMeleeOutcome::Parry)humanoid=entry;
    }
    assert(humanoid);n.entry=humanoid;
    assert(localMeleeAvoided(localRollPlayerMelee(p,n,stats,true,1000,9999)));
    n.orientation=0;
    assert(localRollPlayerMelee(p,n,stats,true,1000,9999)==LocalMeleeOutcome::Hit);
    stats.hit=100;stats.expertise=100;
    assert(localRollPlayerMelee(p,n,stats,true,0,1999)==LocalMeleeOutcome::Critical);
    n.entry=0;n.orientation=3.14159265358979323846f;
    assert(localRollMeleeSpecialBlock(p,n,499)&&!localRollMeleeSpecialBlock(p,n,500));
    assert(localRollPlayerMelee(p,n,stats,true,9999,0)==LocalMeleeOutcome::Critical);
    n.level=43;
    assert(localRollMeleeSpecialBlock(p,n,439)&&!localRollMeleeSpecialBlock(p,n,440));
    n.orientation=0;assert(!localRollMeleeSpecialBlock(p,n,0));
    n.orientation=3.14159265358979323846f;
    uint32_t noBlock=0;
    for(uint32_t entry=1;entry<=40000&&!noBlock;++entry)if(localNpcMeleeFlags(entry)&16)noBlock=entry;
    assert(noBlock);n.entry=noBlock;assert(!localRollMeleeSpecialBlock(p,n,0));
    std::cout<<"PASS: Bloodthirst real DBC 50% AP and physical melee metadata; percentage/truncation/finite bounds; special miss/dodge/parry, rear-facing, independent crit and block, source block level difference, no-block NPC flag, dual-wield exclusion and no glancing/crushing\n";
}
