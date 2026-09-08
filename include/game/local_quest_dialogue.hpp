#pragma once
#include "game/local_gameplay.hpp"
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
    return q.giverEntry==n.entry && p.level>=q.minLevel &&
        (!q.allowableRaces || (q.allowableRaces&(1u<<(p.race-1)))) &&
        (!q.allowableClasses || (q.allowableClasses&(1u<<(p.classId-1)))) &&
        (!q.prerequisite || std::binary_search(p.completedQuestIds.begin(),p.completedQuestIds.end(),q.prerequisite));
}
}
