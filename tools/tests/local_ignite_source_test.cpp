#include "local_ignite_fixture.hpp"
// Source/progression harness resolves immutable definitions linearly; it does
// not provide an alternate proc dispatcher or talent transaction.
const LocalSpellDefinition* LocalWorldContent::spell(uint32_t id) const {
    for(const auto& spell:spells)if(spell.id==id)return &spell;return nullptr;
}
int main(int argc,char** argv) {
    assert(argc==2);IgniteSource0237 input(argv[1]);unsigned mutations=0;
    for(uint8_t rank=1;rank<=5;++rank) {
        LocalSpellDefinition d,child;assert(input.decode(rank,d,&child));
        assert(validLocalIgniteProc(d.proc)&&validLocalProc(d)&&d.proc.amount==8u*rank&&d.proc.allowTriggered);
        assert(child.id==12654&&child.triggeredOnly&&child.procParentTalentId==34&&child.periodicEffectSlot==0&&
            child.durationMs==4000&&child.periodicIntervalMs==2000&&!child.periodicDamage);
        for(uint32_t spell:{d.id,12654u}) {
            auto row=detail::ClientSpellTables::lookup(input.source.spellIndex,spell);assert(row>=0);
            for(uint32_t col=1;col<234;++col) {
                if(col>=131&&col<=203)continue;
                auto changed=input.bytes.at("Spell");changed[20+size_t(row)*234*4+col*4]^=1;
                assert(input.tables.at("Spell").load(changed));LocalSpellDefinition bad;
                if(input.decode(rank,bad)){std::cerr<<"unexpected source acceptance "<<spell<<" column "<<col<<'\n';assert(false);}++mutations;
            }
            assert(input.tables.at("Spell").load(input.bytes.at("Spell")));assert(input.decode(rank,d));
        }
        for(auto [table,id,maxColumn]:{std::tuple{"SpellDuration",35u,3u},std::tuple{"SpellCastTimes",1u,3u},
                                      std::tuple{"SpellRange",1u,5u},std::tuple{"SpellRange",13u,5u}}) {
            const auto& index=std::string(table)=="SpellDuration"?input.source.durationIndex:
                std::string(table)=="SpellRange"?input.source.rangeIndex:input.source.castIndex;
            const auto row=detail::ClientSpellTables::lookup(index,id);assert(row>=0);
            for(uint32_t col=1;col<=maxColumn;++col) {
                auto changed=input.bytes.at(table);changed[20+size_t(row)*input.tables.at(table).getRecordSize()+col*4]^=1;
                assert(input.tables.at(table).load(changed));LocalSpellDefinition bad;assert(!input.decode(rank,bad));++mutations;
            }
            assert(input.tables.at(table).load(input.bytes.at(table)));assert(input.decode(rank,d));
        }
    }
    // Retail allocations through the production importer and talent transaction.
    auto c=input.content();assert(c->spell(12654)&&c->spell(12654)->triggeredOnly);
    auto p=rewardPlayer(1);p.classId=8;p.race=1;p.level=80;p.maxMana=p.mana=10000;std::string error;
    auto learn=[&](uint32_t id,unsigned count){for(unsigned rank=0;rank<count;++rank){
        if(!learnLocalTalent(p,*c,id,rank,error)){std::cerr<<id<<'/'<<rank<<' '<<error<<'\n';assert(false);}}};
    assert(!learnLocalTalent(p,*c,34,0,error));learn(26,5);learn(34,5);learn(27,2);learn(28,2);learn(29,1);
    assert(localTalentPointsSpent(p)==15);
    auto low=p;low.level=24;assert(!learnLocalTalent(low,*c,24,0,error));
    p.level=25;assert(learnLocalTalent(p,*c,24,0,error));assert(!learnLocalTalent(p,*c,24,1,error));
    p.level=26;assert(learnLocalTalent(p,*c,24,1,error));
    // Integer truncation, pre-overkill damage, partial absorption and carryover.
    for(uint32_t rank=1;rank<=5;++rank)assert(localIgniteTickAmount(1501,8*rank)==1501*8*rank/200);
    assert(localIgniteTickAmount(1500-500,40)==200);
    assert(localIgniteTickAmount(1500,40,301,2)==601);
    assert(localIgniteTickAmount(1500,40,301,1)==450);
    assert(localIgniteTickAmount(1500,40,301,0)==300);
    assert(localIgniteTickAmount(0,40,301,1)==150);
    assert(localIgniteTickAmount(1500,41)==0);
    LocalCombatEvent event;event.source=1;event.target=2;event.spell=2136;
    event.kind=LocalCombatEventKind::SpellDamage;assert(localIgniteScriptMatches(event));
    event.spellFamilyFlags[1]=8;assert(!localIgniteScriptMatches(event));event.spellFamilyFlags[1]=0;
    event.spell=0;assert(!localIgniteScriptMatches(event));event.spell=2136;
    event.source=0;assert(!localIgniteScriptMatches(event));event.source=1;
    event.kind=LocalCombatEventKind::DirectHeal;assert(!localIgniteScriptMatches(event));
    LocalSpellDefinition ignite;assert(input.decode(5,ignite));
    event.kind=LocalCombatEventKind::ProcDamage;event.auraSpell=999;event.schoolMask=4;event.spellFamily=3;
    event.attempted=event.effective=1500;event.outcome=LocalMeleeOutcome::Critical;
    assert(localProcMatches(ignite.proc,event,1)&&localIgniteScriptMatches(event));
    event.spellFamilyFlags[1]=8;assert(localProcMatches(ignite.proc,event,1)&&!localIgniteScriptMatches(event));
    std::cout<<"PASS Ignite source:5 ranks+internal two-effect12654; "<<mutations<<" full profile mutations; real Fire15point route, Molten Shields level25/26, rolling integer carry, after-absorb input, script exclusions\n";
}
