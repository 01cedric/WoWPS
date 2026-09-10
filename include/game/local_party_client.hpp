#pragma once
#include "game/local_party.hpp"
#include "game/group_defines.hpp"
#include <algorithm>

namespace wowee::game {
// Adapt only public party metadata for native unit/portrait resolution. It
// confers no group loot, encounter, difficulty, XP or quest-credit authority.
inline void updateLocalPartyClientData(GroupListData& out,const LocalPartyView& view,uint64_t self) {
    out.groupType=out.subGroup=out.flags=out.roles=0;
    out.lootMethod=view.members.empty()?0:1;out.lootThreshold=2;
    out.difficultyId=out.raidDifficultyId=0;out.looterGuid=0;
    out.leaderGuid=view.members.empty()?0:view.members.front().guid;
    size_t count=0;
    for(const auto& m:view.members)if(m.guid!=self){
        if(count==out.members.size())out.members.emplace_back();
        auto& member=out.members[count++];member.name=m.name;member.guid=m.guid;
        member.isOnline=1;member.subGroup=member.flags=member.roles=0;
        member.curHealth=m.health;member.maxHealth=m.maxHealth;member.powerType=m.powerType;
        member.curPower=uint16_t(std::min(m.power,65535u));member.maxPower=uint16_t(std::min(m.maxPower,65535u));
        member.level=m.level;member.zoneId=0;
        member.posX=int16_t(std::clamp(m.x,-32768.0f,32767.0f));member.posY=int16_t(std::clamp(m.y,-32768.0f,32767.0f));
        member.onlineStatus=m.dead?3:1;member.hasPartyStats=true;
    }
    out.members.resize(count);out.memberCount=uint32_t(count);
}
}
