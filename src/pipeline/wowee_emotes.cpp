#include "pipeline/wowee_emotes.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'E', 'M', 'O'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wemo";

} // namespace

const WoweeEmotes::Entry*
WoweeEmotes::findById(uint32_t emoteId) const {
    for (const auto& e : entries)
        if (e.emoteId == emoteId) return &e;
    return nullptr;
}

const WoweeEmotes::Entry*
WoweeEmotes::findByCommand(const std::string& cmd) const {
    for (const auto& e : entries)
        if (e.slashCommand == cmd) return &e;
    return nullptr;
}

std::vector<const WoweeEmotes::Entry*>
WoweeEmotes::findByKind(uint8_t emoteKind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.emoteKind == emoteKind) out.push_back(&e);
    return out;
}

bool WoweeEmotesLoader::save(const WoweeEmotes& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeEmotes::Entry& e) {
        writePOD(os, e.emoteId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.slashCommand);
        writePOD(os, e.animationId);
        writePOD(os, e.soundId);
        writeStr(os, e.targetMessage);
        writeStr(os, e.noTargetMessage);
        writePOD(os, e.emoteKind);
        writePOD(os, e.sex);
        writePOD(os, e.requiredRace);
        writePOD(os, e.ttsHint);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeEmotes WoweeEmotesLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeEmotes>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeEmotes::Entry& e) {
        if (!readPOD(is, e.emoteId)) { return false; }
        if (!readStr(is, e.name) ||
            !readStr(is, e.description) ||
            !readStr(is, e.slashCommand)) { return false; }
        if (!readPOD(is, e.animationId) ||
            !readPOD(is, e.soundId)) { return false; }
        if (!readStr(is, e.targetMessage) ||
            !readStr(is, e.noTargetMessage)) { return false; }
        if (!readPOD(is, e.emoteKind) ||
            !readPOD(is, e.sex) ||
            !readPOD(is, e.requiredRace) ||
            !readPOD(is, e.ttsHint) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeEmotesLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeEmotes WoweeEmotesLoader::makeBasic(
    const std::string& catalogName) {
    using E = WoweeEmotes;
    WoweeEmotes c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* slash,
                    uint32_t anim, uint32_t snd,
                    const char* targetMsg,
                    const char* noTargetMsg, uint8_t tts,
                    const char* desc) {
        E::Entry e;
        e.emoteId = id; e.name = slash;
        e.description = desc;
        e.slashCommand = slash;
        e.animationId = anim;
        e.soundId = snd;
        e.targetMessage = targetMsg;
        e.noTargetMessage = noTargetMsg;
        e.emoteKind = E::Social;
        e.sex = E::SexBoth;
        e.ttsHint = tts;
        e.iconColorRGBA = packRgba(140, 220, 200);   // social teal
        c.entries.push_back(e);
    };
    // Animation IDs from AnimationData.dbc - well-known
    // WoW 3.3.5a values for these emotes.
    add(1, "wave",   70, 6373,
        "%s waves at %s.",   "%s waves.",
        E::TtsTalk,
        "Friendly hand wave - universal greeting.");
    add(2, "bow",    68, 6371,
        "%s bows before %s.", "%s bows.",
        E::TtsTalk,
        "Respectful bow - common in formal social "
        "encounters.");
    add(3, "laugh",  31, 6362,
        "%s laughs at %s.",  "%s laughs.",
        E::TtsTalk,
        "Hearty laugh - also fires the racial laugh "
        "voice clip.");
    add(4, "cheer",  29, 6365,
        "%s cheers at %s.",  "%s cheers.",
        E::TtsYell,
        "Boisterous cheer - pairs with raid victory "
        "moments.");
    add(5, "cry",    32, 6361,
        "%s cries on %s.",   "%s cries.",
        E::TtsWhisper,
        "Soft cry - typically used in RP for sad "
        "moments.");
    add(6, "sleep",  93, 0,
        "%s falls asleep on %s.", "%s falls asleep. Zzzz.",
        E::TtsSilent,
        "Lay-down sleep - character lies prone with "
        "snore particle effect.");
    add(7, "kneel",  35, 0,
        "%s kneels before %s.", "%s kneels down.",
        E::TtsSilent,
        "Kneel - used for swearing oaths and showing "
        "deference.");
    add(8, "applaud", 79, 6360,
        "%s applauds at %s.", "%s applauds.",
        E::TtsTalk,
        "Polite golf-clap applause - distinct from /cheer "
        "(which is louder).");
    return c;
}

WoweeEmotes WoweeEmotesLoader::makeCombat(
    const std::string& catalogName) {
    using E = WoweeEmotes;
    WoweeEmotes c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* slash,
                    uint32_t anim, uint32_t snd,
                    const char* targetMsg,
                    const char* noTargetMsg, uint8_t tts,
                    const char* desc) {
        E::Entry e;
        e.emoteId = id; e.name = slash;
        e.description = desc;
        e.slashCommand = slash;
        e.animationId = anim;
        e.soundId = snd;
        e.targetMessage = targetMsg;
        e.noTargetMessage = noTargetMsg;
        e.emoteKind = E::Combat;
        e.sex = E::SexBoth;
        e.ttsHint = tts;
        e.iconColorRGBA = packRgba(220, 80, 80);   // combat red
        c.entries.push_back(e);
    };
    add(100, "roar",      67, 6364,
        "%s roars at %s. Goosebumps appear.",
        "%s roars with bestial fury.",
        E::TtsYell,
        "Aggressive roar - pairs with intimidation "
        "moments.");
    add(101, "threaten",  77, 0,
        "%s threatens %s with violence.",
        "%s makes a threatening gesture.",
        E::TtsYell,
        "Menacing pose - finger-wag or weapon brandish.");
    add(102, "charge",    52, 6358,
        "%s charges at %s!", "%s charges forward!",
        E::TtsYell,
        "Forward-lean charge pose - RP companion to a "
        "Warrior Charge ability.");
    add(103, "victory",   85, 6376,
        "%s celebrates victory over %s.",
        "%s celebrates a great victory!",
        E::TtsYell,
        "Arms-raised victory pose - duel-end signature.");
    add(104, "surrender", 90, 0,
        "%s surrenders to %s.", "%s surrenders.",
        E::TtsWhisper,
        "Hands-up surrender - used in PvP duel forfeits.");
    return c;
}

WoweeEmotes WoweeEmotesLoader::makeRolePlay(
    const std::string& catalogName) {
    using E = WoweeEmotes;
    WoweeEmotes c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* slash,
                    uint32_t anim, uint32_t snd,
                    const char* targetMsg,
                    const char* noTargetMsg, uint8_t tts,
                    const char* desc) {
        E::Entry e;
        e.emoteId = id; e.name = slash;
        e.description = desc;
        e.slashCommand = slash;
        e.animationId = anim;
        e.soundId = snd;
        e.targetMessage = targetMsg;
        e.noTargetMessage = noTargetMsg;
        e.emoteKind = E::RolePlay;
        e.sex = E::SexBoth;
        e.ttsHint = tts;
        e.iconColorRGBA = packRgba(180, 100, 240);   // rp purple
        c.entries.push_back(e);
    };
    add(200, "bonk",   72, 0,
        "%s bonks %s on the head.", "%s bonks the air.",
        E::TtsTalk,
        "Light tap on the head - friendly playful "
        "gesture.");
    add(201, "ponder", 61, 0,
        "%s ponders %s.", "%s ponders deep thoughts.",
        E::TtsWhisper,
        "Hand-on-chin contemplation pose.");
    add(202, "soothe", 75, 0,
        "%s soothes %s gently.", "%s makes soothing noises.",
        E::TtsWhisper,
        "Calming pat-pat motion - used in RP healer "
        "scenes.");
    add(203, "plead",  47, 0,
        "%s pleads with %s.", "%s pleads desperately.",
        E::TtsWhisper,
        "Hands-clasped pleading pose - kneels slightly.");
    add(204, "shoo",   77, 0,
        "%s shoos %s away.", "%s shoos the air.",
        E::TtsTalk,
        "Brushing-away gesture - dismiss undesired "
        "attention.");
    add(205, "scoff",  42, 0,
        "%s scoffs at %s.", "%s scoffs.",
        E::TtsTalk,
        "Disdainful scoff with arms-crossed pose.");
    return c;
}

} // namespace pipeline
} // namespace wowee
