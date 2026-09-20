#include "game/local_proc_talents.hpp"
#include <cassert>
#include <iostream>
using namespace wowee::game;
int main() {
    LocalWorldContent content;LocalRealmPlayer owner;owner.classId=11;owner.level=80;owner.talents={{801,2}};
    LocalSpellDefinition fury;fury.talentPrerequisites[0]=798;fury.talentPrerequisiteRanks[0]=2;
    assert(!localPassiveProcPrerequisites(owner,content,fury));
    owner.talents.insert(owner.talents.begin(),{798,2});
    LocalSpellDefinition claws;claws.talentId=798;claws.talentRank=2;claws.allowableClasses=1024;claws.id=16943;
    content.spells.push_back(claws);assert(!localPassiveProcPrerequisites(owner,content,fury));
    owner.talents[0].second=3;assert(!localPassiveProcPrerequisites(owner,content,fury));
    content.spells[0].talentRank=3;content.spells[0].unsupportedReason="Unimplemented complete aura";
    assert(!localPassiveProcPrerequisites(owner,content,fury));
    content.spells[0].unsupportedReason.clear();content.spells[0].allowableClasses=1;
    assert(!localPassiveProcPrerequisites(owner,content,fury));
    content.spells[0].allowableClasses=1024;assert(localPassiveProcPrerequisites(owner,content,fury));
    owner.talents.erase(owner.talents.begin());assert(!localPassiveProcPrerequisites(owner,content,fury));
    std::cout<<"PASS passive proc direct prerequisite guard: missing, insufficient, unresolved, unsupported, wrong-class prerequisite rejected; current complete rank accepted; reset immediately blocks; allocations retained\n";
}
