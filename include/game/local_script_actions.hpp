#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wowee::game {

// Immutable authored actions. Runtime actors are transient authority state;
// only the existing script state/timer rows belong in a character save.
enum class LocalScriptActionKind : uint8_t { Dialogue = 0, Spawn = 1, Despawn = 2, Move = 3, Combat = 4 };

struct LocalScriptAction {
    uint32_t id = 0;
    LocalScriptActionKind kind = LocalScriptActionKind::Dialogue;
    // actorId is a content-local stable identity, not a client supplied GUID.
    // Combat targetActorId zero means the sole player in the action scope.
    uint32_t actorId = 0, targetActorId = 0, npcEntry = 0;
    uint32_t mapId = 0, instanceId = 0;
    uint32_t lifetimeMs = 0; // Spawn only; every scripted actor expires within ten minutes.
    float x = 0, y = 0, z = 0, orientation = 0;
    std::string text;
};

// Public dialogue observation shared by local gameplay and LAN100.
struct LocalScriptDialogue {
    uint64_t revision = 0;
    uint64_t speakerGuid = 0, viewerGuid = 0;
    uint32_t mapId = 0, instanceId = 0;
    std::string text;
    uint8_t chatType = 0; // 0 = authored script notice; otherwise a creature_text ChatMsg type.
    bool operator==(const LocalScriptDialogue&) const = default;
};

inline bool localScriptDialogueTypeValid(uint8_t type) {
    return type==0 || type==12 || type==14 || type==15 || type==16 || type==41;
}

inline constexpr size_t kLocalMaxScriptActions = 256;
inline constexpr size_t kLocalMaxScriptActionRefs = 8;
inline constexpr size_t kLocalMaxScriptActionBatch = 64;
inline constexpr size_t kLocalMaxScriptActors = 32;
inline constexpr size_t kLocalMaxScriptDialogues = 32;
// Dialogue-only token: each scoped player is the speaker of their own record.
// Runtime actor IDs stay in the low 31-bit namespace used for generated GUIDs.
inline constexpr uint32_t kLocalScriptPlayerActor = UINT32_MAX;

inline bool validLocalScriptAction(const LocalScriptAction& action) {
    if (!action.id || !action.actorId || action.instanceId>65535 ||
        unsigned(action.kind) > unsigned(LocalScriptActionKind::Combat) ||
        !std::isfinite(action.x) || !std::isfinite(action.y) ||
        !std::isfinite(action.z) || !std::isfinite(action.orientation) ||
        std::abs(action.x) > 100000 || std::abs(action.y) > 100000 ||
        std::abs(action.z) > 20000 || std::abs(action.orientation) > 100000 ||
        action.text.size() > 255 || action.text.find('\0') != std::string::npos)
        return false;
    // String limits are UTF-8 byte limits because LAN packets carry bytes.
    if (action.actorId == kLocalScriptPlayerActor && action.kind != LocalScriptActionKind::Dialogue)
        return false;
    if (action.actorId != kLocalScriptPlayerActor && action.actorId > 0x7fffffffu)
        return false;
    switch (action.kind) {
    case LocalScriptActionKind::Dialogue:
        return !action.text.empty() && !action.npcEntry && !action.targetActorId && !action.lifetimeMs;
    case LocalScriptActionKind::Spawn:
        return action.npcEntry && !action.targetActorId && action.text.empty() &&
               action.lifetimeMs && action.lifetimeMs <= 600000;
    case LocalScriptActionKind::Despawn:
        return !action.npcEntry && !action.targetActorId && action.text.empty() && !action.lifetimeMs;
    case LocalScriptActionKind::Move:
        return !action.npcEntry && !action.targetActorId && action.text.empty() && !action.lifetimeMs;
    case LocalScriptActionKind::Combat:
        // The existing threat engine has no general NPC-vs-NPC damage path;
        // combat therefore targets the sole scoped player fail-closed.
        return !action.npcEntry && !action.targetActorId && action.text.empty() && !action.lifetimeMs;
    }
    return false;
}

inline bool validLocalScriptActionRefs(const std::vector<uint32_t>& ids) {
    if (ids.size() > kLocalMaxScriptActionRefs) return false;
    // Authored order is execution order; duplicates are almost always a
    // mistaken double side effect and are rejected rather than normalized.
    for (auto it=ids.begin();it!=ids.end();++it)
        if (!*it || std::find(ids.begin(),it,*it)!=it) return false;
    return true;
}

} // namespace wowee::game
