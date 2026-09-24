#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_quest_eligibility.hpp"
#include <algorithm>
#include <cmath>

namespace wowee::game {
// Expand the bounded server greeting without interpreting it as Lua or a format
// string. Unknown escapes are retained for a future locale-specific renderer.
inline std::string localNpcGreeting(const LocalRealmPlayer& p, const LocalRealmNpc& n,
                                    const LocalWorldContent& c) {
    const auto* def=c.npc(n.entry);
    std::string text=def?def->gossipText:std::string{};
    if(text.empty()) text="Greetings, $N."; // explicit fallback, not fabricated retail gossip
    std::string out;out.reserve(std::min<size_t>(8192,text.size()+p.name.size()));
    for(size_t i=0;i<text.size() && out.size()<8192;++i) {
        if(text[i]=='$' && i+1<text.size()) {
            if(text[i+1]=='N' || text[i+1]=='n'){out+=p.name;++i;continue;}
            if(text[i+1]=='B' || text[i+1]=='b'){out+='\n';++i;continue;}
        }
        out+=text[i];
    }
    if(out.size()>8192)out.resize(8192);
    return out;
}
/// 2.40: the npc_text variant a gossip page shows. The variants' probabilities
/// weigh a roll seeded by the page's revision, so a page keeps its text while
/// it is open and a reopened one may pick another (the client rolls per
/// SMSG_NPC_TEXT_UPDATE).
inline const LocalGossipTextVariant* localGossipVariant(const LocalGossipText& t, uint32_t seed) {
    if(t.variants.empty())return nullptr;
    float total=0;for(const auto& v:t.variants)total+=std::max(0.f,v.probability);
    if(!(total>0))return &t.variants.front();
    uint32_t x=seed*2654435761u+0x9e3779b9u;x^=x>>13;x*=0x5bd1e995u;x^=x>>15;
    float roll=float(x%10000)/10000.f*total;
    for(const auto& v:t.variants){roll-=std::max(0.f,v.probability);if(roll<0)return &v;}
    return &t.variants.back();
}
/// The page's text for this character: the gender's variant text with the
/// $N / $B / $C / $R / $G tokens expanded (the client's own substitutions).
inline std::string localGossipPageText(const LocalRealmPlayer& p, const LocalGossipText& t, uint32_t seed) {
    static const char* races[]={"","Human","Orc","Dwarf","Night Elf","Undead","Tauren","Gnome","Troll","","Blood Elf","Draenei"};
    static const char* classes[]={"","Warrior","Paladin","Hunter","Rogue","Priest","Death Knight","Shaman","Mage","Warlock","","Druid"};
    const auto* v=localGossipVariant(t,seed);if(!v)return {};
    const std::string& text=p.gender&&!v->femaleText.empty()?v->femaleText:(!v->maleText.empty()?v->maleText:v->femaleText);
    std::string out;out.reserve(std::min<size_t>(8192,text.size()+p.name.size()));
    for(size_t i=0;i<text.size()&&out.size()<8192;++i) {
        if(text[i]!='$'||i+1>=text.size()){out+=text[i];continue;}
        const char token=text[i+1];
        if(token=='g'||token=='G') {
            const auto colon=text.find(':',i+2),end=text.find(';',i+2);
            if(colon!=std::string::npos&&end!=std::string::npos&&colon<end) {
                auto trim=[](std::string s){while(!s.empty()&&s.front()==' ')s.erase(s.begin());while(!s.empty()&&s.back()==' ')s.pop_back();return s;};
                out+=trim(p.gender?text.substr(colon+1,end-colon-1):text.substr(i+2,colon-i-2));i=end;continue;
            }
        }
        if(token=='N'||token=='n'){out+=p.name;++i;continue;}
        if(token=='B'||token=='b'){out+='\n';++i;continue;}
        if(token=='R'||token=='r'){out+=p.race<12?races[p.race]:"";++i;continue;}
        if(token=='C'||token=='c'){out+=p.classId<12?classes[p.classId]:"";++i;continue;}
        out+=text[i];
    }
    if(out.size()>8192)out.resize(8192);
    return out;
}
inline bool localNpcInTalkRange(const LocalRealmPlayer& p, const LocalRealmNpc& n) {
    const float x=p.x-n.x, y=p.y-n.y, z=p.z-n.z;
    const float d=x*x+y*y+z*z;
    return !p.dead && !n.dead && !n.hostile && p.mapId==n.mapId &&
        p.instanceId==n.instanceId && std::isfinite(d) && d<=64.0f;
}
inline const LocalQuestProgress* localQuestProgress(const LocalRealmPlayer& p, uint32_t id) {
    for(const auto& q:p.quests) if(q.id==id) return &q;
    return nullptr;
}
inline bool localQuestOffered(const LocalRealmPlayer& p, const LocalRealmNpc& n,
                             const LocalQuestDefinition& q) {
    if(!localNpcInTalkRange(p,n) || !n.questGiver || p.race<1 || p.race>32 ||
       p.classId<1 || p.classId>32 ||
       std::binary_search(p.completedQuestIds.begin(),p.completedQuestIds.end(),q.id)) return false;
    if(const auto* progress=localQuestProgress(p,q.id))
        return q.turnInEntry==n.entry && progress->status!=LocalQuestStatus::Rewarded;
    return q.giverEntry==n.entry && !localQuestAcceptanceError(p,q);
}
}
