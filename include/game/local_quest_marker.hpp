#pragma once

#include "game/local_gameplay.hpp"
#include "game/quest_giver_status.hpp"
#include <algorithm>

namespace wowee::game {

// World markers must remain visible beyond talking range. Read only the
// authoritative quest snapshot; opening/closing a dialogue cannot change it.
inline QuestGiverStatus localQuestMarkerStatus(const LocalRealmPlayer& player,
                                               const LocalRealmNpc& npc,
                                               const std::vector<LocalQuestDefinition>& quests) {
    if (!npc.questGiver || npc.dead || npc.health == 0 || npc.aggressive ||
        npc.mapId != player.mapId || npc.instanceId != player.instanceId ||
        player.race < 1 || player.race > 32 || player.classId < 1 || player.classId > 32) {
        return QuestGiverStatus::NONE;
    }

    const auto rewarded = [&](uint32_t id) {
        return std::binary_search(player.completedQuestIds.begin(), player.completedQuestIds.end(), id);
    };
    bool available = false;
    bool incomplete = false;
    for (const auto& quest : quests) {
        if (rewarded(quest.id)) continue;
        const auto progress = std::find_if(player.quests.begin(), player.quests.end(),
            [&](const LocalQuestProgress& entry) { return entry.id == quest.id; });
        if (progress != player.quests.end()) {
            if (quest.turnInEntry != npc.entry) continue;
            if (progress->status == LocalQuestStatus::Complete) return QuestGiverStatus::REWARD;
            if (progress->status == LocalQuestStatus::Active) incomplete = true;
            continue;
        }

        if (quest.giverEntry != npc.entry || player.level < quest.minLevel || quest.requiredSkill ||
            (quest.allowableRaces && !(quest.allowableRaces & (1u << (player.race - 1)))) ||
            (quest.allowableClasses && !(quest.allowableClasses & (1u << (player.classId - 1)))) ||
            (quest.prerequisite && !rewarded(quest.prerequisite))) continue;
        available = true;
    }

    // A completed hand-in wins over a new offer; a new offer wins over a
    // pending objective. The catalog stores minimum acceptance level, not
    // authored quest difficulty, so it cannot truthfully classify trivial
    // quests. Online statuses still carry and render that distinction.
    if (available) return QuestGiverStatus::AVAILABLE;
    return incomplete ? QuestGiverStatus::INCOMPLETE : QuestGiverStatus::NONE;
}

} // namespace wowee::game
