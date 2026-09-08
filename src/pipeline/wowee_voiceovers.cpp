#include "pipeline/wowee_voiceovers.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'V', 'O', 'X'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wvox";

} // namespace

const WoweeVoiceovers::Entry*
WoweeVoiceovers::findById(uint32_t voiceId) const {
    for (const auto& e : entries)
        if (e.voiceId == voiceId) return &e;
    return nullptr;
}

std::vector<const WoweeVoiceovers::Entry*>
WoweeVoiceovers::findForTrigger(uint32_t npcId,
                                  uint8_t eventKind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.npcId == npcId && e.eventKind == eventKind)
            out.push_back(&e);
    }
    return out;
}

bool WoweeVoiceoversLoader::save(const WoweeVoiceovers& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeVoiceovers::Entry& e) {
        writePOD(os, e.voiceId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.npcId);
        writePOD(os, e.eventKind);
        writePOD(os, e.genderHint);
        writePOD(os, e.variantIndex);
        writePOD(os, e.pad0);
        writeStr(os, e.audioPath);
        writeStr(os, e.transcript);
        writePOD(os, e.durationMs);
        writePOD(os, e.volumeDb);
        writePOD(os, e.pad1);
        writePOD(os, e.pad2);
        writePOD(os, e.pad3);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeVoiceovers WoweeVoiceoversLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeVoiceovers>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeVoiceovers::Entry& e) {
        if (!readPOD(is, e.voiceId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.npcId) ||
            !readPOD(is, e.eventKind) ||
            !readPOD(is, e.genderHint) ||
            !readPOD(is, e.variantIndex) ||
            !readPOD(is, e.pad0)) { return false; }
        if (!readStr(is, e.audioPath) ||
            !readStr(is, e.transcript)) { return false; }
        if (!readPOD(is, e.durationMs) ||
            !readPOD(is, e.volumeDb) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.pad2) ||
            !readPOD(is, e.pad3) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeVoiceoversLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeVoiceovers WoweeVoiceoversLoader::makeQuestgiver(
    const std::string& catalogName) {
    using V = WoweeVoiceovers;
    WoweeVoiceovers c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t kind, uint8_t variant,
                    const char* audioPath,
                    const char* transcript,
                    uint32_t durMs, const char* desc) {
        V::Entry e;
        e.voiceId = id; e.name = name; e.description = desc;
        e.npcId = 18486;        // generic questgiver
                                 // (placeholder)
        e.eventKind = kind;
        e.genderHint = V::Male;
        e.variantIndex = variant;
        e.audioPath = audioPath;
        e.transcript = transcript;
        e.durationMs = durMs;
        e.volumeDb = 0;
        e.iconColorRGBA = packRgba(220, 220, 100);   // quest gold
        c.entries.push_back(e);
    };
    add(1, "Greeting", V::Greeting, 0,
        "Sound\\Creature\\Questgiver\\Greeting01.ogg",
        "Hail, hero. I have a task that needs doing.",
        2400,
        "Initial greeting on first NPC right-click. "
        "First impression voice line.");
    add(2, "QuestStart", V::QuestStart, 0,
        "Sound\\Creature\\Questgiver\\Accept01.ogg",
        "Excellent. The blade and crown both await.",
        3100,
        "Played when player clicks Accept on the quest. "
        "Tone of approval.");
    add(3, "QuestProgress", V::QuestProgress, 0,
        "Sound\\Creature\\Questgiver\\Progress01.ogg",
        "Be quick about it. Time grows short.",
        2200,
        "Played when player re-talks NPC mid-quest. "
        "Gentle reminder.");
    add(4, "QuestComplete", V::QuestComplete, 0,
        "Sound\\Creature\\Questgiver\\Reward01.ogg",
        "Well done! Take this as a token of my gratitude.",
        3800,
        "Played at quest turn-in. Most emotive line - "
        "celebration register.");
    add(5, "Goodbye", V::Goodbye, 0,
        "Sound\\Creature\\Questgiver\\Goodbye01.ogg",
        "Safe travels, friend.",
        1500,
        "Played when chat window closes. Short, "
        "polite sign-off.");
    return c;
}

WoweeVoiceovers WoweeVoiceoversLoader::makeBoss(
    const std::string& catalogName) {
    using V = WoweeVoiceovers;
    WoweeVoiceovers c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t kind, uint8_t variant,
                    const char* audioPath,
                    const char* transcript,
                    uint32_t durMs, int8_t volume,
                    const char* desc) {
        V::Entry e;
        e.voiceId = id; e.name = name; e.description = desc;
        e.npcId = 36597;        // The Lich King (placeholder)
        e.eventKind = kind;
        e.genderHint = V::Male;
        e.variantIndex = variant;
        e.audioPath = audioPath;
        e.transcript = transcript;
        e.durationMs = durMs;
        e.volumeDb = volume;
        e.iconColorRGBA = packRgba(220, 60, 60);   // boss red
        c.entries.push_back(e);
    };
    add(100, "BossAggro", V::Aggro, 0,
        "Sound\\Creature\\LichKing\\Aggro01.ogg",
        "So, the Light's vengeance has come.",
        3500, +3,
        "Boss aggro line played on combat start. +3dB "
        "louder than ambient for emphasis.");
    add(101, "Boss75Pct", V::Phase, 0,
        "Sound\\Creature\\LichKing\\Phase75.ogg",
        "You are not the only ones with arrows in "
        "your quivers!",
        4200, +3,
        "Phase 1 -> Phase 2 transition at 75%% boss "
        "health.");
    add(102, "Boss50Pct", V::Phase, 1,
        "Sound\\Creature\\LichKing\\Phase50.ogg",
        "I will freeze you from the inside out!",
        3800, +3,
        "Phase 2 -> Phase 3 at 50%% health. Frostmourne "
        "phase begins.");
    add(103, "Boss25Pct", V::Phase, 2,
        "Sound\\Creature\\LichKing\\Phase25.ogg",
        "I'll keep you alive to witness the end.",
        3200, +3,
        "Phase 3 -> final phase at 25%% health. Most "
        "dangerous mechanics activate.");
    add(104, "BossMechanicCall", V::Special, 0,
        "Sound\\Creature\\LichKing\\Defile01.ogg",
        "Apocalypse!",
        2000, +5,
        "Special mechanic call (Defile cast warning). "
        "+5dB above ambient - must be audible over "
        "raid noise.");
    add(105, "BossDeath", V::Death, 0,
        "Sound\\Creature\\LichKing\\Death01.ogg",
        "No... it cannot be...",
        4500, +2,
        "Death line - emotional conclusion.");
    return c;
}

WoweeVoiceovers WoweeVoiceoversLoader::makeVendor(
    const std::string& catalogName) {
    using V = WoweeVoiceovers;
    WoweeVoiceovers c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t kind, uint8_t variant,
                    uint8_t gender,
                    const char* audioPath,
                    const char* transcript,
                    uint32_t durMs, const char* desc) {
        V::Entry e;
        e.voiceId = id; e.name = name; e.description = desc;
        e.npcId = 11498;        // generic vendor
        e.eventKind = kind;
        e.genderHint = gender;
        e.variantIndex = variant;
        e.audioPath = audioPath;
        e.transcript = transcript;
        e.durationMs = durMs;
        e.volumeDb = 0;
        e.iconColorRGBA = packRgba(140, 200, 255);   // vendor blue
        c.entries.push_back(e);
    };
    add(200, "VendorGreeting", V::Greeting, 0, V::Female,
        "Sound\\Creature\\Vendor\\Greeting01.ogg",
        "Looking to buy or sell?",
        1900,
        "Played when player opens the vendor window. "
        "Female-cast voice line.");
    // Buy and Sell both use eventKind=Special since
    // there's no dedicated buy/sell enum value; distinct
    // variantIndex (0 = buy, 1 = sell) keeps them
    // disambiguated for the trigger handler.
    add(201, "VendorBuy", V::Special, 0, V::Female,
        "Sound\\Creature\\Vendor\\Buy01.ogg",
        "A fine choice. Will there be anything else?",
        2400,
        "Played when player buys an item. variantIndex=0 "
        "for buy; sell is variantIndex=1 to avoid the "
        "validator's per-(npc,event,variant) collision.");
    add(202, "VendorSell", V::Special, 1, V::Female,
        "Sound\\Creature\\Vendor\\Sell01.ogg",
        "I'll take this off your hands.",
        2100,
        "Played when player sells an item back to "
        "the vendor.");
    add(203, "VendorGoodbye", V::Goodbye, 0, V::Female,
        "Sound\\Creature\\Vendor\\Goodbye01.ogg",
        "Come back soon!",
        1300,
        "Played when vendor window closes.");
    return c;
}

} // namespace pipeline
} // namespace wowee
