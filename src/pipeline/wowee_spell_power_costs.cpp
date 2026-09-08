#include "pipeline/wowee_spell_power_costs.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'P', 'C'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wspc";

} // namespace

const WoweeSpellPowerCost::Entry*
WoweeSpellPowerCost::findById(uint32_t powerCostId) const {
    for (const auto& e : entries)
        if (e.powerCostId == powerCostId) return &e;
    return nullptr;
}

int32_t WoweeSpellPowerCost::resolveCost(uint32_t powerCostId,
                                          uint32_t casterLevel,
                                          int32_t maxPower) const {
    const Entry* e = findById(powerCostId);
    if (!e) return 0;
    if (e->powerType == NoCost) return 0;
    int64_t cost = static_cast<int64_t>(e->baseCost) +
                   static_cast<int64_t>(e->perLevelCost) *
                   static_cast<int64_t>(casterLevel);
    if (e->percentOfBase != 0.0f) {
        cost += static_cast<int64_t>(
            static_cast<float>(maxPower) * e->percentOfBase);
    }
    if (cost < 0) cost = 0;
    return static_cast<int32_t>(cost);
}

const char* WoweeSpellPowerCost::powerTypeName(uint8_t k) {
    switch (k) {
        case Mana:        return "mana";
        case Rage:        return "rage";
        case Focus:       return "focus";
        case Energy:      return "energy";
        case Happiness:   return "happiness";
        case RunicPower:  return "runic-power";
        case Runes:       return "runes";
        case SoulShards:  return "soul-shards";
        case HolyPower:   return "holy-power";
        case Eclipse:     return "eclipse";
        case Health:      return "health";
        case NoCost:      return "no-cost";
        default:          return "unknown";
    }
}

bool WoweeSpellPowerCostLoader::save(const WoweeSpellPowerCost& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeSpellPowerCost::Entry& e) {
        writePOD(os, e.powerCostId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.powerType);
        writePadding(os, 3);
        writePOD(os, e.baseCost);
        writePOD(os, e.perLevelCost);
        writePOD(os, e.percentOfBase);
        writePOD(os, e.costFlags);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeSpellPowerCost WoweeSpellPowerCostLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeSpellPowerCost>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeSpellPowerCost::Entry& e) {
        if (!readPOD(is, e.powerCostId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.powerType)) { return false; }
        if (!skipPadding(is, 3)) { return false; }
        if (!readPOD(is, e.baseCost) ||
            !readPOD(is, e.perLevelCost) ||
            !readPOD(is, e.percentOfBase) ||
            !readPOD(is, e.costFlags) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeSpellPowerCostLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeSpellPowerCost WoweeSpellPowerCostLoader::makeStarter(
    const std::string& catalogName) {
    using P = WoweeSpellPowerCost;
    WoweeSpellPowerCost c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t type,
                    float pct, int32_t base, const char* desc) {
        P::Entry e;
        e.powerCostId = id; e.name = name; e.description = desc;
        e.powerType = type;
        e.baseCost = base;
        e.percentOfBase = pct;
        e.iconColorRGBA = packRgba(80, 140, 240);   // mana blue
        c.entries.push_back(e);
    };
    add(1, "NoCost",      P::NoCost, 0.0f,  0,
        "Free spell - no resource cost (Auto Attack).");
    add(2, "LowMana",     P::Mana,   0.05f, 0,
        "Low-cost spell - 5% of max mana (Frostbolt).");
    add(3, "MediumMana",  P::Mana,   0.15f, 0,
        "Medium-cost spell - 15% of max mana (Fireball).");
    add(4, "HighMana",    P::Mana,   0.30f, 0,
        "High-cost spell - 30% of max mana (Pyroblast).");
    return c;
}

WoweeSpellPowerCost WoweeSpellPowerCostLoader::makeRage(
    const std::string& catalogName) {
    using P = WoweeSpellPowerCost;
    WoweeSpellPowerCost c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, int32_t rage,
                    uint32_t flags, const char* desc) {
        P::Entry e;
        e.powerCostId = id; e.name = name; e.description = desc;
        e.powerType = P::Rage;
        e.baseCost = rage;
        e.costFlags = flags;
        e.iconColorRGBA = packRgba(220, 60, 60);   // rage red
        c.entries.push_back(e);
    };
    // Warrior abilities, fixed rage cost (no level scaling).
    add(100, "HeroicStrikeRage",  15, 0,
        "Heroic Strike - 15 rage on next melee.");
    add(101, "SlamRage",          20, 0,
        "Slam - 20 rage, channeled.");
    add(102, "WhirlwindRage",     25, P::RequiresCombatStance,
        "Whirlwind - 25 rage, requires Berserker stance.");
    add(103, "MortalStrikeRage",  30, 0,
        "Mortal Strike - 30 rage instant strike.");
    return c;
}

WoweeSpellPowerCost WoweeSpellPowerCostLoader::makeMixed(
    const std::string& catalogName) {
    using P = WoweeSpellPowerCost;
    WoweeSpellPowerCost c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t type,
                    int32_t cost, uint32_t flags, const char* desc) {
        P::Entry e;
        e.powerCostId = id; e.name = name; e.description = desc;
        e.powerType = type;
        e.baseCost = cost;
        e.costFlags = flags;
        e.iconColorRGBA = packRgba(180, 180, 180);   // neutral
        c.entries.push_back(e);
    };
    add(200, "HunterFocus30",   P::Focus,      30, 0,
        "Hunter Focus - 30 focus (Cobra Shot).");
    add(201, "RogueEnergy40",   P::Energy,     40, P::RefundOnMiss,
        "Rogue Energy - 40 energy (Sinister Strike), refunds "
        "on miss/dodge/parry.");
    add(202, "DKRunic30",       P::RunicPower, 30, 0,
        "Death Knight Runic Power - 30 RP (Death Coil).");
    add(203, "PaladinHoly1",    P::HolyPower,   1, 0,
        "Paladin Holy Power - 1 HP per finisher (Templar's "
        "Verdict, Word of Glory).");
    add(204, "WarlockShard1",   P::SoulShards,  1, 0,
        "Warlock Soul Shard - 1 shard (Soulburn finishers).");
    return c;
}

} // namespace pipeline
} // namespace wowee
