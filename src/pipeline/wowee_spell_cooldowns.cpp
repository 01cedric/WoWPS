#include "pipeline/wowee_spell_cooldowns.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'C', 'D'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wscd";

} // namespace

const WoweeSpellCooldown::Entry*
WoweeSpellCooldown::findById(uint32_t bucketId) const {
    for (const auto& e : entries)
        if (e.bucketId == bucketId) return &e;
    return nullptr;
}

const char* WoweeSpellCooldown::bucketKindName(uint8_t k) {
    switch (k) {
        case Spell:  return "spell";
        case Item:   return "item";
        case Class:  return "class";
        case Global: return "global";
        case Misc:   return "misc";
        default:     return "unknown";
    }
}

bool WoweeSpellCooldownLoader::save(const WoweeSpellCooldown& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeSpellCooldown::Entry& e) {
        writePOD(os, e.bucketId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.bucketKind);
        writePadding(os, 3);
        writePOD(os, e.cooldownMs);
        writePOD(os, e.categoryFlags);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeSpellCooldown WoweeSpellCooldownLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeSpellCooldown>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeSpellCooldown::Entry& e) {
        if (!readPOD(is, e.bucketId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.bucketKind)) { return false; }
        if (!skipPadding(is, 3)) { return false; }
        if (!readPOD(is, e.cooldownMs) ||
            !readPOD(is, e.categoryFlags) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeSpellCooldownLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeSpellCooldown WoweeSpellCooldownLoader::makeStarter(
    const std::string& catalogName) {
    WoweeSpellCooldown c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint32_t cdMs, uint32_t flags,
                    uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        WoweeSpellCooldown::Entry e;
        e.bucketId = id; e.name = name; e.description = desc;
        e.bucketKind = kind;
        e.cooldownMs = cdMs;
        e.categoryFlags = flags;
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    add(1, "GlobalCooldown",   WoweeSpellCooldown::Global,
        1500,
        WoweeSpellCooldown::AffectedByHaste |
        WoweeSpellCooldown::OnGCDStart,
        220, 220, 220, "Global cooldown - 1.5s, hasted, applies to "
        "every combat spell cast.");
    add(2, "ShortItemCD",      WoweeSpellCooldown::Item,
        5000,  0,
        180, 240, 180, "Short item cooldown - 5s (low-tier consumables).");
    add(3, "MediumItemCD",     WoweeSpellCooldown::Item,
        30000, 0,
        180, 240, 100, "Medium item cooldown - 30s (mid-tier "
        "consumables / wands).");
    add(4, "LongItemCD",       WoweeSpellCooldown::Item,
        60000, WoweeSpellCooldown::SharedWithItems,
        240, 220, 100, "Long item cooldown - 60s, shared between "
        "healing/mana potions.");
    return c;
}

WoweeSpellCooldown WoweeSpellCooldownLoader::makeClass(
    const std::string& catalogName) {
    WoweeSpellCooldown c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t cdMs,
                    uint32_t flags, const char* desc) {
        WoweeSpellCooldown::Entry e;
        e.bucketId = id; e.name = name; e.description = desc;
        e.bucketKind = WoweeSpellCooldown::Class;
        e.cooldownMs = cdMs;
        e.categoryFlags = flags;
        e.iconColorRGBA = packRgba(100, 200, 240);   // mage blue
        c.entries.push_back(e);
    };
    add(100, "PolymorphFamily",  0,
        0, "Mage Polymorph variants (Sheep / Pig / Turtle / Cat) - "
        "0ms cooldown but exclusive: only one variant active per "
        "target.");
    add(101, "AlterTime",        90000,
        WoweeSpellCooldown::AffectedByHaste,
        "Alter Time - 90s, hasted by spell haste.");
    add(102, "Counterspell",     24000,
        0, "Counterspell - 24s, fixed cooldown.");
    add(103, "Blink",            15000,
        0, "Blink - 15s, fixed cooldown.");
    add(104, "IceBlock",        300000,
        WoweeSpellCooldown::IgnoresCooldownReduction,
        "Ice Block - 5min, not affected by Cold Snap or CDR.");
    return c;
}

WoweeSpellCooldown WoweeSpellCooldownLoader::makeItems(
    const std::string& catalogName) {
    WoweeSpellCooldown c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t cdMs,
                    uint32_t flags, const char* desc) {
        WoweeSpellCooldown::Entry e;
        e.bucketId = id; e.name = name; e.description = desc;
        e.bucketKind = WoweeSpellCooldown::Item;
        e.cooldownMs = cdMs;
        e.categoryFlags = flags;
        e.iconColorRGBA = packRgba(240, 200, 100);   // gold for items
        c.entries.push_back(e);
    };
    add(200, "HealingPotion",      60000,
        WoweeSpellCooldown::SharedWithItems,
        "Healing potion family - 60s shared with mana potions.");
    add(201, "ManaPotion",         60000,
        WoweeSpellCooldown::SharedWithItems,
        "Mana potion family - 60s shared with healing potions.");
    add(202, "ManaJade",            1500,
        WoweeSpellCooldown::OnGCDStart,
        "Mana Jade / oil flasks - GCD-only, no item cooldown.");
    add(203, "EngineerTrinket",    60000, 0,
        "Engineer trinket - 60s standalone bucket.");
    add(204, "HearthstoneFamily", 3600000, 0,
        "Hearthstone - 60min, exclusive across alt-bind variants.");
    return c;
}

} // namespace pipeline
} // namespace wowee
