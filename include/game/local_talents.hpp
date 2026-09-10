#pragma once
#include "game/local_gameplay.hpp"
namespace wowee::game {
inline unsigned localTalentPointsSpent(const LocalRealmPlayer& p){unsigned n=0;for(auto [id,rank]:p.talents)n+=rank;return n;}
inline unsigned localTalentPointsAvailable(const LocalRealmPlayer& p){const unsigned earned=p.level>9?p.level-9:0,spent=localTalentPointsSpent(p);return earned>spent?earned-spent:0;}
inline const LocalSpellDefinition* localTalentSpell(const LocalWorldContent& c,uint32_t id,uint8_t rank){
    for(const auto& s:c.spells)if(s.talentId==id && s.talentRank==rank)return &s;return nullptr;
}
inline bool validLocalTalents(const LocalRealmPlayer& p){
    if(p.talents.size()>71 || localTalentPointsSpent(p)>unsigned(p.level>9?p.level-9:0))return false;
    uint32_t previous=0;for(auto [id,rank]:p.talents){if(!id || id<=previous || !rank || rank>5)return false;previous=id;}return true;
}
inline uint32_t localTalentBonus(const LocalRealmPlayer& p,const LocalWorldContent& c,bool armor){
    uint64_t value=0;for(auto [id,rank]:p.talents)if(const auto* s=localTalentSpell(c,id,rank);s && s->unsupportedReason.empty())value+=armor?s->passiveArmor:s->passiveHealth;
    return uint32_t(std::min(uint64_t(100000),value));
}
inline bool learnLocalTalent(LocalRealmPlayer& p,const LocalWorldContent& c,uint32_t id,uint32_t requestedRank,std::string& error){
    auto reject=[&](const std::string& why){error=why;return false;};
    if(requestedRank>4 || !localTalentPointsAvailable(p))return reject("No available talent points or invalid rank");
    const auto* s=localTalentSpell(c,id,uint8_t(requestedRank+1));
    if(!s || p.classId<1 || p.classId>11 || !(s->allowableClasses&(1u<<(p.classId-1))))return reject("Talent does not belong to this class");
    if(!s->unsupportedReason.empty())return reject("Talent unavailable: "+s->unsupportedReason);
    unsigned rank=0,treePoints=0;
    for(auto [other,n]:p.talents){if(other==id)rank=n;if(const auto* v=localTalentSpell(c,other,n);v && v->talentTab==s->talentTab)treePoints+=n;}
    if(rank!=requestedRank || treePoints<unsigned(s->talentRow)*5)return reject("Spend points in earlier tiers first");
    for(size_t i=0;i<3;++i)if(s->talentPrerequisites[i]){
        auto found=std::find_if(p.talents.begin(),p.talents.end(),[&](const auto& t){return t.first==s->talentPrerequisites[i];});
        if(found==p.talents.end() || found->second<s->talentPrerequisiteRanks[i]+1)return reject("Required talent rank is missing");
    }
    auto candidate=p;
    if(const auto* prior=localTalentSpell(c,id,uint8_t(rank)))std::erase(candidate.knownSpells,prior->id);
    if(!s->passive){if(candidate.knownSpells.size()>=LocalGameplay::MaxSpells)return reject("Spellbook is full");candidate.knownSpells.push_back(s->id);}
    auto found=std::find_if(candidate.talents.begin(),candidate.talents.end(),[&](const auto& t){return t.first==id;});
    if(found==candidate.talents.end())candidate.talents.emplace_back(id,uint8_t(rank+1));else found->second=uint8_t(rank+1);
    std::sort(candidate.talents.begin(),candidate.talents.end());p=std::move(candidate);error="Talent learned: "+s->name;return true;
}
}
