#include "game/local_spell_amount.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
// This fixture intentionally exercises the real unindexed talent lookup.
// Indexed spell lookup must never be reached; no lookup behavior is mocked.
namespace wowee::game {
const LocalSpellDefinition* LocalWorldContent::spell(uint32_t) const {std::abort();}
}
using namespace wowee::game;
int main() {
    LocalWorldContent c;assert(!c.talentIndexReady);
    LocalSpellDefinition cast;cast.spellFamily=4;cast.spellFamilyFlags={0,0x400,0};
    LocalSpellDefinition prerequisite;prerequisite.id=1;prerequisite.talentId=165;prerequisite.talentRank=1;prerequisite.allowableClasses=1;
    LocalSpellDefinition modifier;modifier.id=56927;modifier.talentId=2234;modifier.talentRank=1;modifier.passive=true;modifier.allowableClasses=1;modifier.spellFamily=4;
    modifier.passiveCastModifiers[0]={0,true,true,2,{0,0x400,0}};
    c.spells={prerequisite,modifier};
    LocalRealmPlayer p;p.classId=1;p.level=80;p.talents={{2234,1}};
    const auto amount=[&]{return localSpellAmountAfterTalents(p,c,cast,1000,false);};
    assert(amount()==1020);
    p.talents={{2234,1},{2234,1}};assert(amount()==1000); // Duplicate allocations cannot multiply a modifier.
    p.level=10;p.talents={{165,1},{2234,1}};assert(amount()==1000); // Two points exceed the single earned point.
    p.level=80;c.spells[1].talentPrerequisites[0]=165;c.spells[1].talentPrerequisiteRanks[0]=0;
    p.talents={{2234,1}};assert(amount()==1000); // Missing direct dependency.
    p.talents={{165,1},{2234,1}};assert(amount()==1020);
    c.spells[0].unsupportedReason="Required effect unavailable";assert(amount()==1000);
    c.spells[0].unsupportedReason.clear();c.spells[0].allowableClasses=2;assert(amount()==1000);
    c.spells[0].allowableClasses=1;c.spells[1].talentPrerequisiteRanks[0]=1;assert(amount()==1000);
    c.spells[1].talentPrerequisiteRanks[0]=0;assert(amount()==1020);
    p.talents.clear();assert(amount()==1000);
    std::cout<<"PASS P03 modifier guards: normal modifier, duplicate/overbudget allocation, missing/unsupported/wrong-class/insufficient direct prerequisite, restored dependency, reset\n";
}
