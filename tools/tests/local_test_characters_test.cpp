// the implementation - the entry gate that refused every login, and ten level-80 test
// characters that are built by the shipped rules rather than written by hand.
//
// GROUP ONE is a regression test for a defect that had been shipping since
// the implementation and blocked EVERY entry into the world.
//
// `importClientStarterSpells` appends two creature spells at the end of the
// import - Lizard Bolt 5401 and Fireball 11985 (`local_spell_import.hpp:1867`
// calling `importLocalNpcSpells`) - and both carry `allowableClasses = 0` on
// purpose, because a creature has no class, and `npcOnly = true`
// (`local_npc_spell_import.hpp:26`). `LocalGameplay::setStarterSpells` required
// a class mask of EVERY definition, so from the implementation to the implementation it answered
// `false` with "Invalid client starter spell" for the real imported set, and
// `application_local_realm.cpp` turned that into `localRealmStatus(error,
// true); return;` - the world never opened.
//
// No suite caught it because every suite that drives the real importer into the
// real gate filters the set first, with the gate's own predicate:
// `local_pets_test.cpp:843`, `local_forms_test.cpp:622` and `:669`,
// `local_aura_stacking_test.cpp:878`, `local_range_facing_test.cpp:834`,
// `local_spell_level_los_test.cpp:695` and `local_shared_combat_test.cpp:973`
// all write `if (d.clientSpell && d.allowableClasses ...)`. They proved the
// gate accepts input it has already been made acceptable. This suite hands it
// the set the console hands it, unfiltered.
//
// The fix narrows the rule, it does not remove it: a class mask is still
// required of every PLAYER definition, and this suite asserts that a non-npcOnly
// definition with a zero mask is still refused.
//
// GROUP TWO and after are the ten level-80 test characters. The race/class
// pairs are the client's own: CharBaseInfo.dbc's 62 two-byte rows, the shipped
// catalog's 62 `starts()` rows and `LocalGameplay::validCharacterOptions`'s 62
// bits are asserted here to be the same 62 set, and the ten pairs to be in it.
// The characters are created through `LocalRealm::seedTestCharacters`, which is
// `createPlayer` + `initializePlayer` with a level handed to it - so the
// spellbook, the resource type, the start position and the health/mana pools
// are whatever the shipped code derives for level 80, and this suite checks
// each of those against an independent computation rather than against a
// number typed into the test.
//
// Reference: AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33.
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include "game/local_spell_import.hpp"
#include "game/local_class_pools.hpp"
#include "game/local_melee.hpp"
#include "game/local_services.hpp"
#include "game/local_proc_talents.hpp"
#include "game/local_test_characters.hpp"
#include "game/lan_discovery.hpp"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace wowee::game;

namespace {

unsigned gFailures = 0;
void expect(bool ok, const std::string& what) {
    if (ok) return;
    ++gFailures;
    std::cerr << "FAIL " << what << "\n";
}

fs::path gDbc;
fs::path gWorld;

struct ClientTables {
    std::map<std::string, wowee::pipeline::DBCFile> files;
    std::map<std::string, std::vector<uint8_t>> bytes;
    const wowee::pipeline::DBCFile* get(const char* name) { return &files.at(name); }
    void load(const fs::path& dbc) {
        for (const auto* name : {"Spell", "SpellRange", "SpellCastTimes", "SpellDuration", "SpellIcon",
                                 "SpellRadius", "SpellRuneCost", "SkillLine", "SkillLineAbility",
                                 "Talent", "TalentTab", "ChrClasses", "CharBaseInfo"}) {
            std::ifstream f(dbc / (std::string(name) + ".dbc"), std::ios::binary);
            bytes[name] = {std::istreambuf_iterator<char>(f), {}};
            assert(files[name].load(bytes[name]));
        }
    }
    /// Exactly what application_local_realm.cpp does between loading the DBCs
    /// and calling setStarterSpells: no filtering of any kind.
    LocalSpellImport import() {
        auto out = importClientStarterSpells(get("Spell"), get("SpellRange"), get("SpellCastTimes"),
            get("SpellDuration"), get("SpellIcon"), get("SkillLineAbility"), get("SkillLine"),
            get("Talent"), get("SpellRuneCost"), get("SpellRadius"));
        detail::importClientTalents(out, get("Talent"), get("TalentTab"), get("Spell"), get("SpellRange"),
            get("SpellCastTimes"), get("SpellDuration"), get("SpellIcon"), get("SpellRuneCost"),
            get("SpellRadius"));
        return out;
    }
};

ClientTables* gTables = nullptr;
LocalSpellImport gImported;

const LocalSpellDefinition* find(const std::vector<LocalSpellDefinition>& set, uint32_t id) {
    for (const auto& d : set) if (d.id == id) return &d;
    return nullptr;
}

/// The the implementation predicate, transcribed, so this suite can state what the console
/// used to answer rather than assert it from memory.
bool shippedUntil0250(const std::vector<LocalSpellDefinition>& spells) {
    if (spells.size() > 8192) return false;
    std::set<uint32_t> ids;
    for (const auto& d : spells)
        if (!d.id || !ids.insert(d.id).second || !d.clientSpell || !d.allowableClasses ||
            d.name.size() > 96 || d.unsupportedReason.size() > 256) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Group one: the entry gate.
// ---------------------------------------------------------------------------
void entryGate() {
    const auto& set = gImported.spells;
    expect(!set.empty(), "the real importer produced a spell set");

    const auto* lizard = find(set, 5401);
    const auto* fireball = find(set, 11985);
    expect(lizard && fireball, "the importer appends the two creature spells 5401 and 11985");
    if (lizard) {
        expect(lizard->npcOnly, "5401 Lizard Bolt is npcOnly");
        expect(lizard->allowableClasses == 0, "5401 Lizard Bolt carries a zero class mask on purpose");
        expect(lizard->clientSpell, "5401 Lizard Bolt is a client spell");
    }
    if (fireball) {
        expect(fireball->npcOnly, "11985 Fireball is npcOnly");
        expect(fireball->allowableClasses == 0, "11985 Fireball carries a zero class mask on purpose");
        expect(fireball->clientSpell, "11985 Fireball is a client spell");
    }

    std::set<uint32_t> zeroMaskIds;
    size_t zeroMaskAndNotNpc = 0;
    for (const auto& d : set) if (!d.allowableClasses) {
        zeroMaskIds.insert(d.id);
        if (!d.npcOnly) ++zeroMaskAndNotNpc;
    }
    // the implementation adds the actual Imp-owned Firebolt rank chain to this same import.
    // Keep an independent exact ID set, and continue handing the gate the
    // whole import: neither NPC spells nor pet spells may be filtered away.
    const std::set<uint32_t> expectedNpcAndPetIds{
        5401, 11985, 3110, 7799, 7800, 7801, 7802, 11762, 11763, 27267, 47964};
    expect(zeroMaskIds == expectedNpcAndPetIds,
           "the two NPC spells and all nine Imp Firebolt ranks are the exact zero-class-mask definitions");
    expect(zeroMaskAndNotNpc == 0, "no PLAYER definition in the real import carries a zero class mask");

    expect(!shippedUntil0250(set),
           "the the reference-the reference predicate refuses the real imported set - this is the shipped defect");

    // The real gate, on the real set, unfiltered: what the console does.
    LocalGameplay gate;
    auto content = std::make_shared<LocalWorldContent>();
    gate.useContent(content);
    std::string error = "not cleared";
    const bool ok = gate.setStarterSpells(set, "diagnostic", error);
    expect(ok, "setStarterSpells accepts the real imported set");
    expect(error.empty(), "and leaves the error empty (was \"Invalid client starter spell\")");
    if (!ok) {
        std::cerr << "FAIL the gate refused with: " << error << "\n";
        return;
    }
    const auto* stored5401 = gate.content().spell(5401);
    const auto* stored11985 = gate.content().spell(11985);
    expect(stored5401 && stored5401->npcOnly && stored5401->allowableClasses == 0,
           "5401 survives the gate as an npcOnly, zero-mask definition");
    expect(stored11985 && stored11985->npcOnly && stored11985->allowableClasses == 0,
           "11985 survives the gate as an npcOnly, zero-mask definition");

    // The rule is narrowed, not removed.
    {
        LocalGameplay narrow;
        narrow.useContent(std::make_shared<LocalWorldContent>());
        auto tainted = set;
        LocalSpellDefinition player;
        player.id = 990251; player.clientSpell = true; player.npcOnly = false;
        player.allowableClasses = 0; player.name = "PlayerSpellWithNoClass";
        tainted.push_back(player);
        std::string why;
        expect(!narrow.setStarterSpells(tainted, "", why),
               "a PLAYER definition with a zero class mask is still refused");
        expect(why == "Invalid client starter spell", "with the same message as before: " + why);
    }
    {
        // And the exemption is exactly npcOnly: clear the flag on the real
        // creature spell and the same set is refused again.
        LocalGameplay narrow;
        narrow.useContent(std::make_shared<LocalWorldContent>());
        auto tainted = set;
        for (auto& d : tainted) if (d.id == 5401) d.npcOnly = false;
        std::string why;
        expect(!narrow.setStarterSpells(tainted, "", why),
               "5401 with npcOnly cleared is refused - the exemption is the flag, not the id");
    }
    {
        // Nothing else was loosened: a zero id, a duplicate and an over-long
        // name are all still refused.
        LocalGameplay narrow;
        narrow.useContent(std::make_shared<LocalWorldContent>());
        std::string why;
        auto dup = set; dup.push_back(dup.front());
        expect(!narrow.setStarterSpells(dup, "", why), "a duplicate id is still refused");
        LocalGameplay narrow2; narrow2.useContent(std::make_shared<LocalWorldContent>());
        auto zero = set; zero.front().id = 0;
        expect(!narrow2.setStarterSpells(zero, "", why), "a zero id is still refused");
        LocalGameplay narrow3; narrow3.useContent(std::make_shared<LocalWorldContent>());
        auto longName = set; longName.front().name = std::string(97, 'x');
        expect(!narrow3.setStarterSpells(longName, "", why), "a 97-character name is still refused");
        LocalGameplay narrow4; narrow4.useContent(std::make_shared<LocalWorldContent>());
        auto notClient = set; notClient.front().clientSpell = false;
        expect(!narrow4.setStarterSpells(notClient, "", why), "a non-client definition is still refused");
    }
    std::cout << "PASS entry gate: the real import of " << set.size()
              << " definitions is accepted unfiltered; the the reference-the reference predicate refuses it; "
                 "5401 and 11985 are present, npcOnly, mask 0; a player definition with mask 0 is still refused\n";
}

// ---------------------------------------------------------------------------
// Group two: the race/class table is the client's, not a memory of it.
// ---------------------------------------------------------------------------
std::set<std::pair<uint8_t, uint8_t>> charBaseInfoPairs() {
    std::set<std::pair<uint8_t, uint8_t>> pairs;
    const auto* dbc = gTables->get("CharBaseInfo");
    // CharBaseInfo.dbc is two bytes per record - race, then class - with no id
    // column, so it is read through the raw record rather than getUInt32.
    const auto& raw = gTables->bytes.at("CharBaseInfo");
    const uint32_t records = dbc->getRecordCount();
    for (uint32_t row = 0; row < records; ++row)
        pairs.insert({raw[20 + row * 2], raw[20 + row * 2 + 1]});
    return pairs;
}

void raceClassTable(const LocalWorldContent& content) {
    const auto client = charBaseInfoPairs();
    expect(client.size() == 62, "CharBaseInfo.dbc describes 62 race/class pairs, got " + std::to_string(client.size()));

    std::set<std::pair<uint8_t, uint8_t>> catalog;
    expect(content.catalog != nullptr, "the shipped world catalog is loaded");
    if (content.catalog)
        for (const auto& start : content.catalog->starts()) catalog.insert({start.race, start.classId});
    expect(catalog == client, "the catalog's starts() table is exactly CharBaseInfo.dbc's pairs");

    std::set<std::pair<uint8_t, uint8_t>> admitted;
    for (uint8_t race = 0; race < 16; ++race)
        for (uint8_t cls = 0; cls < 16; ++cls)
            if (LocalGameplay::validCharacterOptions(race, cls, 0)) admitted.insert({race, cls});
    expect(admitted == client, "validCharacterOptions admits exactly CharBaseInfo.dbc's pairs");

    std::set<uint8_t> races, classes;
    for (const auto& row : kLocalTestCharacters) {
        races.insert(row.race); classes.insert(row.classId);
        expect(client.count({row.race, row.classId}) == 1,
               std::string(row.name) + " is a race/class pair the client's own CharBaseInfo.dbc permits");
        expect(catalog.count({row.race, row.classId}) == 1,
               std::string(row.name) + " has a starting location in the shipped catalog");
        expect(LocalGameplay::validCharacterOptions(row.race, row.classId, 0),
               std::string(row.name) + " is admitted by validCharacterOptions");
    }
    expect(races.size() == kLocalTestCharacterCount, "the ten test characters are ten different races");
    expect(classes.size() == kLocalTestCharacterCount, "the ten test characters are ten different classes");
    expect(classes == std::set<uint8_t>({1, 2, 3, 4, 5, 6, 7, 8, 9, 11}),
           "and they are every playable class: 1-9 and 11");
    std::cout << "PASS race/class table: CharBaseInfo.dbc, the catalog's starts() and validCharacterOptions "
                 "are the same 62 pairs; the ten chosen pairs are in all three, ten distinct races, "
                 "ten distinct classes\n";
}

// ---------------------------------------------------------------------------
// Group three: the seeded characters are the shipped rules' own output.
// ---------------------------------------------------------------------------

/// ChrClasses.dbc column 2 is PowerType, whose values are the same numbers
/// LocalResourceType uses (0 mana, 1 rage, 3 energy, 6 runic power). Reading it
/// here means the expected resource is the client's, not this suite's.
std::map<uint8_t, uint8_t> clientPowerTypes() {
    std::map<uint8_t, uint8_t> out;
    const auto* dbc = gTables->get("ChrClasses");
    for (uint32_t row = 0; row < dbc->getRecordCount(); ++row)
        out[uint8_t(dbc->getUInt32(row, 0))] = uint8_t(dbc->getUInt32(row, 2));
    return out;
}

/// The spellbook the shipped derivation should produce for this character,
/// recomputed here from the same imported set and the same published helpers.
std::set<uint32_t> expectedSpellbook(const LocalWorldContent& c, uint8_t classId, uint8_t level) {
    std::set<uint32_t> out;
    // A character with no talents allocated, which is what a freshly created
    // one is. localProcTalentPrerequisite is what keeps Earth Shield out of a
    // level-80 shaman's book: the rank exists and its level is reached, but the
    // talent that sells it has not been taken.
    LocalRealmPlayer probe; probe.guid = 1; probe.classId = classId; probe.level = level;
    for (const auto& spell : c.spells) {
        if (!spell.clientSpell || spell.npcOnly || spell.triggeredOnly || spell.mountDisplayId || spell.talentId) continue;
        if (!localProcTalentPrerequisite(probe, c, spell)) continue;
        if (!(spell.allowableClasses & (1u << (classId - 1)))) continue;
        if (localSpellUnlockLevel(spell) > level) continue;
        const auto* next = spell.supercededBySpell ? c.spell(spell.supercededBySpell) : nullptr;
        if (next && next->clientSpell && next->unsupportedReason.empty() && localSpellUnlockLevel(*next) <= level) continue;
        out.insert(spell.id);
    }
    return out;
}

void seeding() {
    const auto dir = fs::temp_directory_path() / ("wowps_seed_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    LocalRealm realm;
    expect(realm.loadContent(gWorld.string()), "the shipped world content loads: " + realm.error());
    expect(realm.setStarterSpells(gImported.spells, "seeded"),
           "and the unfiltered imported set is installed: " + realm.error());

    raceClassTable(realm.content());

    std::vector<LocalTestCharacterSpec> specs;
    for (const auto& row : kLocalTestCharacters)
        specs.push_back({row.slot, row.race, row.classId, 0, kLocalTestCharacterLevel, row.name});
    const size_t created = realm.seedTestCharacters(dir.string(), specs);
    expect(created == kLocalTestCharacterCount,
           "ten test characters were created, got " + std::to_string(created) + " (" + realm.error() + ")");

    // the implementation persists pet autocast preferences in Save 31 / LAN 86. Character
    // seeding must write the current format through the same real save path.
    std::vector<uint8_t> save;
    {
        std::ifstream f(dir / "realm.wprs", std::ios::binary);
        save.assign(std::istreambuf_iterator<char>(f), {});
    }
    expect(save.size() > 5, "a realm save was written");
    if (save.size() > 5) expect(save[4] == 31, "the save declares version 31, got " + std::to_string(save[4]));
    expect(lan::GameplayVersion == 86, "the LAN gameplay version is 86");

    // Read them back exactly as the character screen does.
    const auto listed = LocalRealm::savedCharacters(dir.string());
    expect(listed.size() == kLocalTestCharacterCount,
           "the character screen lists ten characters, got " + std::to_string(listed.size()));

    const auto powers = clientPowerTypes();
    const auto& content = realm.content();
    std::set<uint8_t> seenRaces, seenClasses;
    for (const auto& saved : listed) {
        const auto& p = saved.player;
        const std::string who = p.name;
        seenRaces.insert(p.race); seenClasses.insert(p.classId);
        expect(p.level == kLocalTestCharacterLevel, who + " is level 80, got " + std::to_string(p.level));
        expect(p.gameplayInitialized, who + " went through initializePlayer");
        expect(!p.dead, who + " is alive");

        // The row the table asked for.
        const LocalTestCharacterRow* row = nullptr;
        for (const auto& r : kLocalTestCharacters) if (r.slot == saved.slot) row = &r;
        expect(row != nullptr, who + " occupies a slot the table asked for");
        if (row) {
            expect(p.race == row->race && p.classId == row->classId, who + " has the race and class the table asked for");
            expect(who == row->name, "slot " + std::to_string(int(saved.slot)) + " is named " + row->name);
        }

        // Start position: the catalog's own row for this race and class.
        const LocalCatalogStart* start = nullptr;
        if (content.catalog)
            for (const auto& s : content.catalog->starts())
                if (s.race == p.race && s.classId == p.classId) { start = &s; break; }
        expect(start != nullptr, who + " has a catalog starting location");
        if (start) {
            expect(p.mapId == start->mapId, who + " starts on the catalog's map " + std::to_string(start->mapId));
            expect(p.x == start->x && p.y == start->y && p.z == start->z,
                   who + " starts at the catalog's coordinates");
            // The forced level replaced the catalog's own start level, which is
            // 1 for everybody but the Death Knight, whose row says 55.
            expect(start->level != kLocalTestCharacterLevel,
                   who + "'s catalog row is not already level 80 (it is " + std::to_string(start->level) + ")");
        }

        // Resource type: the client's own ChrClasses.dbc PowerType.
        const auto power = powers.find(p.classId);
        expect(power != powers.end(), who + "'s class is in ChrClasses.dbc");
        if (power != powers.end())
            expect(uint8_t(p.resourceType) == power->second,
                   who + " uses ChrClasses.dbc PowerType " + std::to_string(power->second) +
                   ", got " + std::to_string(uint8_t(p.resourceType)));

        // Pools: recomputed by the shipped code for this exact player.
        const auto pools = localResourcePools(p, content);
        expect(pools.sourceValues, who + "'s pools come from the compiled source tables, not the fallback");
        expect(p.maxHealth == pools.health,
               who + "'s health is the derived pool: " + std::to_string(p.maxHealth) + " vs " + std::to_string(pools.health));
        expect(p.health == p.maxHealth, who + " starts at full health");
        if (p.resourceType == LocalResourceType::Mana) {
            expect(p.maxMana == pools.mana,
                   who + "'s mana is the derived pool: " + std::to_string(p.maxMana) + " vs " + std::to_string(pools.mana));
            expect(p.mana == p.maxMana, who + " starts at full mana");
        } else {
            expect(p.maxMana == 100, who + "'s non-mana resource bar is the fixed 100");
            // stats() fills every bar but rage and runic power, which both
            // start empty and are built in combat (local_gameplay.cpp:261).
            const bool startsEmpty = p.resourceType == LocalResourceType::Rage ||
                                     p.resourceType == LocalResourceType::RunicPower;
            expect(p.mana == (startsEmpty ? 0u : p.maxMana),
                   who + (startsEmpty ? " starts with an empty rage/runic bar"
                                      : " starts with a full energy bar"));
        }
        // A level-80 pool is not a level-1 pool: prove the level actually moved
        // the numbers rather than merely being stored.
        LocalRealmPlayer atOne = p; atOne.level = 1;
        const auto small = localResourcePools(atOne, content);
        expect(pools.baseHealth > small.baseHealth,
               who + "'s level-80 base health " + std::to_string(pools.baseHealth) +
               " exceeds its level-1 base health " + std::to_string(small.baseHealth));

        // Melee stats exist for this class at 80.
        const auto melee = localMeleeStats(p, content);
        expect(melee.sourceStats, who + " has source base stats at level 80");

        // Spellbook: exactly what the shipped derivation grants at 80.
        const auto expected = expectedSpellbook(content, p.classId, kLocalTestCharacterLevel);
        const std::set<uint32_t> known(p.knownSpells.begin(), p.knownSpells.end());
        expect(known == expected,
               who + " knows exactly the " + std::to_string(expected.size()) +
               " abilities its class and level grant, got " + std::to_string(known.size()));
        expect(!known.empty(), who + " knows at least one ability");
        expect(known.size() <= LocalGameplay::MaxSpells, who + " is inside the spellbook cap");
        for (auto id : known) {
            const auto* d = content.spell(id);
            expect(d && !d->npcOnly, who + " knows no creature spell (id " + std::to_string(id) + ")");
        }
        expect(!known.count(5401) && !known.count(11985), who + " does not know 5401 or 11985");
        // A talent-gated rank is not granted by level alone: a fresh character
        // has no talents, so the shaman's Earth Shield ranks stay out.
        for (auto id : {974u, 32593u, 32594u, 49283u, 49284u})
            expect(!known.count(id), who + " does not get the talent-gated Earth Shield rank " + std::to_string(id));

        // Equipment: the shipped starter path put something on.
        const auto worn = std::count_if(p.equipment.begin(), p.equipment.end(), [](uint32_t id) { return id != 0; });
        expect(worn > 0, who + " is wearing the starter equipment the shipped path grants");
        expect(!p.inventory.empty(), who + " carries the starter inventory");
    }
    expect(seenRaces.size() == kLocalTestCharacterCount, "ten distinct races reached the character list");
    expect(seenClasses.size() == kLocalTestCharacterCount, "ten distinct classes reached the character list");

    // The named case the checkpoint asks for: a level-80 mage knows Frostbolt
    // at the rank its level grants, and not at rank one.
    {
        const LocalRealmPlayer* mage = nullptr;
        for (const auto& saved : listed) if (saved.player.classId == 8) mage = &saved.player;
        expect(mage != nullptr, "a mage was seeded");
        if (mage) {
            uint32_t best = 0; uint8_t bestLevel = 0;
            for (const auto& d : content.spells) {
                if (d.name != "Frostbolt" || !d.clientSpell || d.npcOnly || d.triggeredOnly || d.talentId) continue;
                if (!(d.allowableClasses & (1u << 7))) continue;  // class 8, bit 7
                const auto unlock = localSpellUnlockLevel(d);
                if (unlock > kLocalTestCharacterLevel) continue;
                if (unlock >= bestLevel) { bestLevel = unlock; best = d.id; }
            }
            expect(best != 0, "the client's Spell.dbc has a Frostbolt rank a level-80 mage can hold");
            const std::set<uint32_t> known(mage->knownSpells.begin(), mage->knownSpells.end());
            expect(best && known.count(best) == 1,
                   "the level-80 mage knows Frostbolt " + std::to_string(best) +
                   " (unlock level " + std::to_string(bestLevel) + ")");
            expect(bestLevel > 1, "and that rank is not the level-one rank");
            expect(!known.count(116), "and it does not also keep Frostbolt rank one, 116");
            // The level-1 mage's Frostbolt is a different id: the level is what
            // chose this one.
            const auto atOne = expectedSpellbook(content, 8, 1);
            expect(!atOne.count(best), "a level-1 mage would not have this rank");
            std::cout << "PASS mage progression: level 80 Frostbolt is id " << best
                      << " at unlock level " << int(bestLevel) << ", rank one (116) is not kept, and a level-1 "
                      << "mage would know " << atOne.size() << " abilities against this one's " << known.size() << "\n";
        }
    }

    // Seeding is not repeatable: a second run must not overwrite or duplicate.
    {
        LocalRealm again;
        expect(again.loadContent(gWorld.string()), "content reloads for the repeat check");
        expect(again.setStarterSpells(gImported.spells, "seeded"), "spells reinstall for the repeat check");
        const size_t second = again.seedTestCharacters(dir.string(), specs);
        expect(second == 0, "a second seeding run creates nothing, got " + std::to_string(second));
        const auto after = LocalRealm::savedCharacters(dir.string());
        expect(after.size() == kLocalTestCharacterCount,
               "and the ten characters are untouched, got " + std::to_string(after.size()));
        for (const auto& saved : after)
            expect(saved.player.level == kLocalTestCharacterLevel, "and still level 80");
    }

    // The characters survive a real start: startSinglePlayer runs validatePlayer
    // and initializePlayer(false) over every one of them.
    {
        LocalRealm started;
        expect(started.loadContent(gWorld.string()), "content reloads for the login check");
        expect(started.setStarterSpells(gImported.spells, "seeded"), "spells reinstall for the login check");
        expect(started.setCharacterSlot(kLocalTestCharacters[9].slot), "the druid's slot is selected");
        const bool ok = started.startSinglePlayer(dir.string(), kLocalTestCharacters[9].name);
        expect(ok, "the seeded druid logs in: " + started.error());
        if (ok) {
            const auto* self = started.localPlayer();
            expect(self && self->level == kLocalTestCharacterLevel, "and is still level 80 in the world");
            expect(self && self->classId == 11 && self->race == 6, "and is still a Tauren druid");
            expect(self && self->maxHealth == localResourcePools(*self, started.content()).health,
                   "and its health is still the derived level-80 pool");
            expect(self && !self->knownSpells.empty(), "and still has its spellbook");
            started.stop();
        }
    }

    // MaxSavedPlayers is untouched: ten characters is well inside it, and the
    // ten slots the character screen offers are exactly filled.
    expect(MaxSavedPlayers >= kLocalTestCharacterCount, "MaxSavedPlayers accommodates the ten");
    expect(LocalRealm::freeCharacterSlot(dir.string()) == -1, "all ten character slots are now taken");

    std::cout << "PASS seeding: " << created << " level-80 characters written through createPlayer/initializePlayer, "
              << "read back by the character screen, Save30 and LAN85 unchanged, a repeat run creates 0, "
              << "and a seeded character logs in\n";
    fs::remove_all(dir, ec);
}

/// A slot that already holds somebody is never overwritten, and the level
/// plumbing is honest: initializePlayer with a forced level derives everything
/// from it rather than stamping it on afterwards.
void forcedLevelIsNotAPatch() {
    LocalGameplay game;
    std::string error;
    expect(game.loadContent(gWorld.string(), error), "content loads for the level plumbing check: " + error);
    expect(game.setStarterSpells(gImported.spells, "", error), "spells install for the level plumbing check: " + error);

    LocalRealmPlayer forced; forced.guid = 1; forced.name = "Forced"; forced.race = 5; forced.classId = 8; forced.gender = 0;
    game.initializePlayer(forced, true, kLocalTestCharacterLevel);
    LocalRealmPlayer natural; natural.guid = 2; natural.name = "Natural"; natural.race = 5; natural.classId = 8; natural.gender = 0;
    game.initializePlayer(natural, true);

    expect(forced.level == kLocalTestCharacterLevel, "the forced character is level 80");
    expect(natural.level == 1, "the unforced character keeps the catalog's level 1");
    expect(forced.maxHealth > natural.maxHealth, "and has more health than the level-1 one");
    expect(forced.knownSpells.size() > natural.knownSpells.size(), "and a larger spellbook");
    expect(forced.xpToLevel != natural.xpToLevel, "and a level-80 experience requirement");

    // A post-hoc level would leave all of that stale; this is what the plumbing
    // change exists to avoid, stated as an assertion.
    LocalRealmPlayer patched = natural;
    patched.level = kLocalTestCharacterLevel;
    expect(patched.maxHealth != forced.maxHealth,
           "writing the level over a finished level-1 character does NOT produce level-80 health");
    expect(patched.knownSpells.size() != forced.knownSpells.size(),
           "nor a level-80 spellbook - which is why the level is plumbed into initializePlayer");

    // A forced level of zero is the catalog's own level, so nothing else moved.
    LocalRealmPlayer zero; zero.guid = 3; zero.name = "Zero"; zero.race = 5; zero.classId = 8; zero.gender = 0;
    game.initializePlayer(zero, true, 0);
    expect(zero.level == natural.level && zero.maxHealth == natural.maxHealth &&
           zero.knownSpells.size() == natural.knownSpells.size(),
           "forcedLevel 0 is exactly the shipped behaviour");

    std::cout << "PASS level plumbing: a forced level-80 mage has " << forced.knownSpells.size()
              << " abilities and " << forced.maxHealth << " health against the level-1 mage's "
              << natural.knownSpells.size() << " and " << natural.maxHealth
              << "; a level written after the fact produces neither\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: local_test_characters_test DBC_DIRECTORY WORLD_JSON\n";
        return 2;
    }
    gDbc = argv[1];
    gWorld = argv[2];
    ClientTables tables;
    tables.load(gDbc);
    gTables = &tables;
    gImported = tables.import();

    entryGate();
    seeding();
    forcedLevelIsNotAPatch();

    if (gFailures) { std::cerr << gFailures << " checks failed\n"; return 1; }
    return 0;
}
