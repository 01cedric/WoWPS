#pragma once

#include "game/local_gameplay.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace wowee::game {

struct LocalUnitPresentationState {
    uint32_t health = 0, level = 0;
    uint64_t target = 0;
    bool dead = false;
};
struct LocalUnitPresentationEvents {
    bool deathPose = false, deathSound = false, respawn = false;
    bool wound = false, aggro = false, levelUp = false;
};
inline LocalUnitPresentationEvents localUnitPresentationEvents(
    const LocalUnitPresentationState* previous, const LocalUnitPresentationState& current) {
    LocalUnitPresentationEvents event;
    // An already-dead unit still needs a corpse when its asynchronous model
    // arrives, but entering range/login must not replay its death vocalization.
    event.deathPose = current.dead && (!previous || !previous->dead);
    if (!previous) return event;
    event.deathSound = current.dead && !previous->dead;
    event.respawn = previous->dead && !current.dead;
    // A lethal hit must not overwrite DEATH with COMBAT_WOUND and then STAND.
    event.wound = !previous->dead && !current.dead && current.health < previous->health;
    event.aggro = !previous->dead && !current.dead && !previous->target && current.target;
    event.levelUp = current.level > previous->level;
    return event;
}

// The authority clears attackTarget on death in the same snapshot that
// contains the killing blow. Keep that final swing without inventing an
// impact for an initial corpse, another owner's kill or a mere target change.
inline bool localMeleeDamageObserved(uint32_t oldHealth, const LocalRealmNpc& npc,
    uint64_t attacker, uint64_t currentTarget, uint64_t previousTarget) {
    return oldHealth > npc.health && (currentTarget == npc.guid ||
        (npc.dead && npc.lootOwner == attacker && previousTarget == npc.guid));
}

struct LocalCastPresentationEvents {
    bool stopPrecast = false, interrupted = false, completed = false, started = false;
    uint32_t previousSpell = 0, completedSpell = 0, startedSpell = 0;
    uint64_t previousTarget = 0, completedTarget = 0, startedTarget = 0;
};
struct LocalCastPresentationState {
    bool seeded = false;
    uint32_t revision = 0, activeSpell = 0, remainingMs = 0, totalMs = 0;
    uint64_t activeTarget = 0;

    LocalCastPresentationEvents observe(const LocalRealmPlayer& player) {
        const bool active = !player.dead && player.castingSpellId &&
            player.castStatus == LocalCastStatus::Casting;
        LocalCastPresentationEvents event;
        if (seeded) {
            event.completed = !player.dead && player.castRevision != revision && player.lastCastSpellId;
            event.completedSpell = player.lastCastSpellId;
            event.completedTarget = player.lastCastTarget;
            event.previousSpell = activeSpell;
            event.previousTarget = activeTarget;
            event.started = active && (!activeSpell || activeSpell != player.castingSpellId ||
                player.castRemainingMs > remainingMs + 100 || event.completed);
            const bool oldEnded = activeSpell && (!active || event.started);
            event.stopPrecast = oldEnded || event.completed;
            event.interrupted = oldEnded &&
                !(event.completed && event.completedSpell == activeSpell);
            event.startedSpell = active ? player.castingSpellId : 0;
            event.startedTarget = active ? player.castTarget : 0;
        }
        seeded = true;
        revision = player.castRevision;
        activeSpell = active ? player.castingSpellId : 0;
        activeTarget = active ? player.castTarget : 0;
        remainingMs = active ? player.castRemainingMs : 0;
        totalMs = active ? player.castTotalMs : 0;
        return event;
    }
};

struct LocalProgressPresentationEvents {
    bool questAccepted = false, questRewarded = false, objectiveUpdated = false;
    bool itemReceived = false, moneyReceived = false;
};
struct LocalProgressPresentationState {
    bool seeded = false;
    uint32_t money = 0;
    std::vector<LocalItemStack> inventory;
    std::vector<LocalQuestProgress> quests;
    std::vector<uint32_t> completed;

    LocalProgressPresentationEvents observe(const LocalRealmPlayer& player) {
        LocalProgressPresentationEvents event;
        if (seeded) {
            for (const auto& quest : player.quests) {
                const auto old = std::find_if(quests.begin(), quests.end(),
                    [&](const auto& item) { return item.id == quest.id; });
                if (old == quests.end()) {
                    if (std::find(completed.begin(), completed.end(), quest.id) == completed.end())
                        event.questAccepted = true;
                } else {
                    if (quest.status == LocalQuestStatus::Complete && old->status != quest.status)
                        event.objectiveUpdated = true;
                    for (size_t i = 0; i < quest.progress.size(); ++i)
                        if (quest.progress[i] > (i < old->progress.size() ? old->progress[i] : 0))
                            event.objectiveUpdated = true;
                }
            }
            // Authority/wire validation keeps completed IDs strictly sorted.
            // Linear subset comparison avoids a quadratic scan every frame
            // as a long-lived character completes more quests.
            event.questRewarded = !std::includes(completed.begin(), completed.end(),
                player.completedQuestIds.begin(), player.completedQuestIds.end());
            const auto count = [](const auto& items, uint32_t id) {
                uint64_t result = 0;
                for (const auto& item : items) if (item.itemId == id) result += item.count;
                return result;
            };
            for (const auto& item : player.inventory)
                if (count(player.inventory, item.itemId) > count(inventory, item.itemId))
                    event.itemReceived = true;
            event.moneyReceived = player.money > money;
        }
        // Commit the next baseline before any SFX callback can run/re-enter.
        inventory = player.inventory;
        quests = player.quests;
        completed = player.completedQuestIds;
        money = player.money;
        seeded = true;
        return event;
    }
};

} // namespace wowee::game
