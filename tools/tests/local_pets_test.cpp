// P07 / the implementation - pets: acquisition that can actually happen, per-level stat
// scaling out of the source table, generated names, and all four commands and
// three stances through the real action path.
//
// Five things are proved here, and the first one is a defect this suite exists
// to make impossible to reintroduce.
//
// One: the summon reaches a creature. The importer admits exactly one summon,
// Summon Imp 688, which names creature 416. The shipped world catalog holds
// 14,496 SPAWNABLE creature entries and 416 is not one of them, so previously
// `content->npc(416)` answered null and the cast was refused with "This realm
// has no creature record for that summon" - the whole pet subsystem had zero
// reachable producers and the the implementation suite did not notice because it builds its
// own synthetic creature 30. This suite drives the real importer and asserts
// that every creature an admitted summon names resolves.
//
// Two: the numbers are pet_levelstats'. Every one of the 35 compiled creatures
// is checked at all 80 levels against its own row, and the Imp's health, mana
// and swing at levels 1, 10, 40 and 80 are pinned to the SQL values so that a
// regenerated catalog which moved them would fail here rather than silently.
//
// Three: the stances are four distinct acquisition routes, not a flag. A
// passive pet acquires nothing from any of them; a defensive pet assists its
// owner; an aggressive pet additionally finds a hostile nobody handed it, which
// is the one route that separates aggressive from defensive in the reference.
//
// Four: the commands move the pet, or stop it moving. A staying pet's position
// is unchanged after its owner walks away; a following pet's is not.
//
// Five: the three new bytes and the stay point survive the save and the LAN deck,
// which are one layout - and a Save29 file still loads, with the defaults.
//
// Reference: AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33.
//   Guardian::InitStatsForLevel      Pet.cpp:1041-1200 - armour `petlevel * 50`
//       replaced by the row's when positive, BaseAttackTime when >= 1000,
//       health/mana/stats from the row, and the SUMMON_PET weapon-damage arm
//       reading min_dmg/max_dmg (:1184-1188) against the HUNTER_PET arm's
//       `petlevel +/- petlevel/4` (:1177-1182).
//   ObjectMgr::LoadPetLevelInfo      ObjectMgr.cpp:4206-4300 - and its SELECT,
//       whose column order differs from the schema's.
//   Pet::SynchronizeLevelWithOwner   Pet.cpp:2389-2411 - SUMMON_PET is always
//       the owner's level.
//   Spell::EffectSummonPet           SpellEffects.cpp:3368-3480 - same-entry
//       refresh in place, refusal while the old pet is a corpse, and
//       GeneratePetName applied to every summon (:3475-3477).
//   ObjectMgr::GeneratePetName       ObjectMgr.cpp:8132-8148.
//   WorldSession::HandlePetActionHelper  PetHandler.cpp:150-330 - the four
//       commands, the three reactions and the `default:` that refuses the rest.
//   PetAI::UpdateAI / AttackedBy / OwnerAttackedBy / OwnerAttacked /
//   SelectNextTarget                 PetAI.cpp:148-213, 452-510, 512-557,
//       832-845 - every route's `if (HasReactState(REACT_PASSIVE)) return;`
//       and the aggressive-only `allowAutoSelect` arm.
//   CharmInfo::InitPetActionBar      CharmInfo.cpp - the ten default slots.
//   Unit.h:567-577                   ReactStates and CommandStates; there is no
//       COMMAND_MOVE_TO at build 12340.
//   Pet::Update                      Pet.cpp:660-671 - a SUMMON_PET never has a
//       corpse, which is what this build already did.
// Audit: the source audit.
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include "game/local_pet.hpp"
#include "game/local_pet_catalog.hpp"
#include "game/local_spell_import.hpp"
#include "game/pet_action.hpp"
#include "game/lan_discovery.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <string>

namespace fs = std::filesystem;
using namespace wowee::game;

namespace {

unsigned gFailures = 0;
void expect(bool ok, const std::string& what) {
    if (!ok) { ++gFailures; std::cerr << "FAIL " << what << "\n"; }
}

struct ClientTables {
    std::map<std::string, wowee::pipeline::DBCFile> files;
    std::map<std::string, std::vector<uint8_t>> bytes;
    const wowee::pipeline::DBCFile* get(const char* name) { return &files.at(name); }
    void load(const fs::path& dbc) {
        for (const auto* name : {"Spell", "SpellRange", "SpellCastTimes", "SpellDuration", "SpellIcon",
                                 "SpellRadius", "SpellRuneCost", "SkillLine", "SkillLineAbility",
                                 "Talent", "TalentTab"}) {
            std::ifstream f(dbc / (std::string(name) + ".dbc"), std::ios::binary);
            bytes[name] = {std::istreambuf_iterator<char>(f), {}};
            assert(files[name].load(bytes[name]));
        }
    }
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
std::shared_ptr<LocalWorldContent> gContent;

const LocalSpellDefinition* find(uint32_t id) {
    for (const auto& d : gImported.spells) if (d.id == id) return &d;
    return nullptr;
}
bool accepted(uint32_t id) { const auto* d = find(id); return d && d->clientSpell && d->unsupportedReason.empty(); }

/// A level-N warlock with nothing but Summon Imp. The real content catalog is
/// the one the importer produced, so the cast walks every real gate.
LocalRealmPlayer warlock(uint8_t level) {
    LocalRealmPlayer p;
    p.guid = 1; p.name = "Warlock"; p.race = 1; p.classId = 9; p.gender = 0; p.level = level;
    p.mapId = 0; p.x = p.y = p.z = 0; p.orientation = 0;
    p.health = p.maxHealth = 5000; p.mana = p.maxMana = 20000;
    p.knownSpells = {688}; p.gameplayInitialized = true;
    return p;
}

/// Cast 688 and drive the ten-second cast to completion. Returns the pet, or
/// null with `why` filled in.
const LocalRealmPet* summonImp(LocalGameplay& game, LocalRealmPlayer& p, std::string& why) {
    std::vector<LocalRealmPlayer*> players{&p};
    if (!game.execute(p, {LocalAction::CastSpell, 0, 688}, players, why)) return nullptr;
    for (int i = 0; i < 400 && game.pets().empty(); ++i) game.tick(0.05f, players);
    if (game.pets().empty()) { why = "the cast completed without a summon"; return nullptr; }
    return &game.pets()[0];
}

/// `petGuid` is CMSG_PET_ACTION's guid1 and `actionTarget` its guid2, which
/// only COMMAND_ATTACK reads.
bool petAction(LocalGameplay& game, LocalRealmPlayer& p, pet::ActionType type, uint32_t action,
               uint64_t petGuid, std::string& result, uint64_t actionTarget = 0) {
    std::vector<LocalRealmPlayer*> players{&p};
    LocalRealmCommand cmd;
    cmd.action = LocalAction::PetAction;
    cmd.target = petGuid;
    cmd.serviceNpcGuid = actionTarget;
    cmd.id = pet::packPetAction(type, action);
    return game.execute(p, cmd, players, result);
}

/// LocalGameplay::pets() is const, as it should be: the authority owns the
/// roster. A test that needs to put a live pet into a particular state does it
/// the same way the LAN guest path does - rebuild the roster and hand it back
/// through the shipped setter, which revalidates it. `mutate` therefore also
/// proves that the state being set up is a legal one.
template <class Fn>
bool mutatePet(LocalGameplay& game, Fn&& mutate) {
    auto roster = game.pets();
    if (roster.empty()) return false;
    mutate(roster[0]);
    game.setRemotePets(roster);
    return !game.pets().empty() && game.pets()[0] == roster[0];
}

/// A hostile creature, placed where the caller asks. Its entry is 50, the
/// definition `petTestContent()` adds, so `LocalGameplay::canAttack` can
/// resolve it.
LocalRealmNpc hostile(uint64_t guid, float x, float y, uint64_t engagedWith) {
    LocalRealmNpc n;
    n.guid = guid; n.entry = 50; n.name = "Test hostile"; n.level = 20;
    n.health = n.maxHealth = 100000; n.hostile = true;
    n.x = n.homeX = x; n.y = n.homeY = y; n.z = n.homeZ = 0;
    n.targetGuid = engagedWith; n.attackTimer = 1000;
    return n;
}

/// LocalGameplay::regions() rebuilds the NPC roster from world spawn queries
/// every half second, and this suite supplies no spawns at all, so a seeded
/// enemy is wiped on the first refresh. Re-seeding it before each tick keeps
/// exactly the state the test is about - a hostile standing there - without
/// suppressing any rule the refresher enforces.
void tickWith(LocalGameplay& game, const std::vector<LocalRealmPlayer*>& players,
              const LocalRealmNpc& seed, unsigned steps, float dt = 0.05f) {
    for (unsigned i = 0; i < steps; ++i) {
        bool present = false;
        for (const auto& n : game.npcs()) if (n.guid == seed.guid) { present = true; break; }
        if (!present) {
            auto roster = game.npcs();
            roster.push_back(seed);
            game.setRemoteNpcs(roster);
        }
        game.tick(dt, players);
    }
}

// ---------------------------------------------------------------------------
// 1. The defect: the summon reaches a creature.
// ---------------------------------------------------------------------------
void acquisitionReaches() {
    expect(accepted(688), "Summon Imp 688 is accepted by the shipped importer");
    unsigned admitted = 0, resolvable = 0, inWorldCatalog = 0;
    for (const auto& d : gImported.spells) {
        if (!d.summonPetEntry || !d.clientSpell || !d.unsupportedReason.empty()) continue;
        ++admitted;
        if (localPetTemplate(d.summonPetEntry)) ++resolvable;
        if (gContent->npc(d.summonPetEntry)) ++inWorldCatalog;
    }
    expect(admitted == 1, "exactly one summon is admitted, as the reference stated");
    expect(resolvable == admitted, "every admitted summon's creature is in the compiled pet catalog");
    expect(inWorldCatalog == 0,
           "and none of them is in the world catalog - the the reference defect, pinned so it cannot return");
    // Its creature has both a template and a full 80-level ladder.
    const auto* imp = localPetTemplate(416);
    expect(imp != nullptr, "creature 416 has a compiled pet template");
    expect(imp && std::string(imp->name) == "Imp", "and it is the Imp");
    expect(imp && imp->displayId == 4449, "display id 4449, creature_template_model's first row by idx");
    expect(imp && imp->baseAttackTimeMs == 2000, "BaseAttackTime 2000 ms");
    expect(imp && imp->creatureType == 3, "CREATURE_TYPE_DEMON, so it is not a focus pet");
    expect(imp && imp->family == 23, "CreatureFamily 23, Imp");
    expect(imp && std::abs(imp->combatReach - 0.75f) < 1e-4f,
           "combat reach 0.75 from creature_model_info - which the melee gate could not read before");

    // The cast itself, through every real gate, at four levels.
    for (uint8_t level : {uint8_t(1), uint8_t(10), uint8_t(40), uint8_t(80)}) {
        LocalGameplay game; game.useContent(gContent);
        auto p = warlock(level);
        std::string why;
        const auto* summoned = summonImp(game, p, why);
        expect(summoned != nullptr,
               "a level " + std::to_string(unsigned(level)) + " warlock's Summon Imp creates a pet (" + why + ")");
        if (!summoned) continue;
        expect(summoned->entry == 416 && summoned->summonSpellId == 688, "it is creature 416 from spell 688");
        expect(summoned->level == level, "Pet::SynchronizeLevelWithOwner: a SUMMON_PET is the owner's level");
        expect(summoned->ownerGuid == p.guid && summoned->kind == LocalPetKind::Controlled,
               "owned and controlled");
        expect(summoned->command == LocalPetCommand::Follow,
               "a fresh summon is following, as CharmInfo::InitPetActionBar leaves it");
        expect(summoned->react == LocalPetReact::Aggressive,
               "and REACT_AGGRESSIVE, the creature default a player cast does not override");
    }
    std::cout << "PASS acquisition: the one admitted summon (688 -> creature 416) resolves through the "
                 "compiled pet catalog and creates a pet at levels 1/10/40/80; 0 of 1 resolve through "
                 "the shipped 14,496-entry world catalog, which is the the reference defect\n";
}

// ---------------------------------------------------------------------------
// 2. pet_levelstats is the source of every number.
// ---------------------------------------------------------------------------
void perLevelStats() {
    expect(kLocalPetTemplateCount == 35, "pet_levelstats names 35 creatures");
    expect(kLocalPetLevelRowCount == 2800, "35 creatures x 80 levels = 2,800 rows");
    expect(kLocalPetNameWordCount == 312, "pet_name_generation has 312 words");

    // Every (creature, level) pair resolves, is monotonically sane and obeys
    // the min_dmg <= max_dmg the importer already refuses to violate.
    unsigned pairs = 0;
    for (const auto& tmpl : kLocalPetTemplates)
        for (uint32_t level = 1; level <= kLocalPetMaxLevel; ++level) {
            const auto* row = localPetLevelStats(tmpl.entry, level);
            if (!row) { expect(false, "every (creature, level) pair has a row"); continue; }
            expect(row->minDamage() <= row->maxDamage(), "min_dmg <= max_dmg on every row");
            expect(localPetArmor(row, level) == (row->armor() ? row->armor() : level * 50),
                   "Guardian::InitStatsForLevel's armour rule: the row's when positive, else petlevel * 50");
            ++pairs;
        }
    expect(pairs == 2800, "2,800 (creature, level) pairs checked");
    // A level above the table takes the last row, which is the reference's own
    // clamp; level zero has none.
    expect(localPetLevelStats(416, 81) == localPetLevelStats(416, 80), "level 81 clamps to the level-80 row");
    expect(localPetLevelStats(416, 0) == nullptr, "level 0 has no row");
    expect(localPetTemplate(0) == nullptr && localPetTemplate(999999) == nullptr,
           "an unknown creature has no pet template");

    // The Imp's own numbers, pinned to pet_levelstats' bytes so a regenerated
    // catalog that moved them fails here.
    struct Pin { uint32_t level, hp, mana, armor, minDmg, maxDmg; };
    constexpr Pin kImp[] = {
        {1,     34,   10,   22,    0,    1},
        {10,   149,  126,  342,    9,   15},
        {20,   340,  396,  565,   17,   27},
        {40,   904, 1053, 1277,   33,   51},
        {80,  3867, 2908, 6273,  305,  458},
    };
    for (const auto& pin : kImp) {
        const auto* row = localPetLevelStats(416, pin.level);
        expect(row && row->health() == pin.hp && row->mana() == pin.mana && row->armor() == pin.armor &&
               row->minDamage() == pin.minDmg && row->maxDamage() == pin.maxDmg,
               "pet_levelstats(416, " + std::to_string(pin.level) + ") is the source row");
    }

    // And the shipped authority really produces them.
    for (const auto& pin : kImp) {
        LocalGameplay game; game.useContent(gContent);
        auto p = warlock(uint8_t(pin.level));
        std::string why;
        const auto* summoned = summonImp(game, p, why);
        if (!summoned) { expect(false, "the Imp summons at level " + std::to_string(pin.level)); continue; }
        expect(summoned->maxHealth == pin.hp,
               "the authority's Imp at level " + std::to_string(pin.level) + " has the row's health");
        expect(summoned->health == summoned->maxHealth, "and is summoned at full health");
        expect(summoned->resourceType == 0 && summoned->maxPower == pin.mana,
               "a demon SUMMON_PET runs on POWER_MANA out of the row, not on focus");
        expect(summoned->attackPeriodMs == 2000, "and swings on creature_template.BaseAttackTime");
    }

    // A focus pet is a beast, and there are six of them in the table.
    unsigned beasts = 0;
    for (const auto& tmpl : kLocalPetTemplates) if (tmpl.creatureType == 1) ++beasts;
    expect(beasts == 6, "six of the 35 compiled pet creatures are beasts, so the focus arm is reachable");
    std::cout << "PASS per-level stats: 2,800 (creature, level) pairs resolve and obey "
                 "InitStatsForLevel's armour rule; the Imp at levels 1/10/20/40/80 is 34/149/340/904/3867 "
                 "health and 0-1/9-15/17-27/33-51/305-458 damage, out of pet_levelstats and driven "
                 "through the shipped authority\n";
}

// ---------------------------------------------------------------------------
// 3. Generated names, refresh-in-place and the corpse refusal.
// ---------------------------------------------------------------------------
void summonSemantics() {
    expect(localPetNameCombinations(416) == 1054, "the Imp has 34 x 31 = 1,054 generated names");
    expect(localPetNameCombinations(417) == 1184 && localPetNameCombinations(1860) == 961 &&
           localPetNameCombinations(1863) == 725 && localPetNameCombinations(17252) == 25 &&
           localPetNameCombinations(26125) == 675,
           "and the other five name-generating creatures have 1,184 / 961 / 725 / 25 / 675");
    unsigned withNames = 0;
    for (const auto& tmpl : kLocalPetTemplates) if (localPetNameCombinations(tmpl.entry)) ++withNames;
    expect(withNames == 6, "6 of the 35 compiled creatures generate names; the other 29 use the template name");
    expect(localPetName(89, 0, 0) == "Infernal",
           "GeneratePetName's fallback for a creature with no words is its template name");

    // Every name the generator can produce is a word from half 0 plus a word
    // from half 1, and the two halves are independent.
    std::set<std::string> half0, half1, produced;
    for (auto [it, end] = localPetNameHalf(416, 0); it != end; ++it) half0.insert(it->word);
    for (auto [it, end] = localPetNameHalf(416, 1); it != end; ++it) half1.insert(it->word);
    expect(half0.size() == 34 && half1.size() == 31, "34 and 31 distinct words");
    for (uint32_t a = 0; a < 34; ++a)
        for (uint32_t b = 0; b < 31; ++b) {
            const auto name = localPetGeneratedName(416, a, b);
            produced.insert(name);
            bool split = false;
            for (const auto& prefix : half0)
                if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0 &&
                    half1.count(name.substr(prefix.size()))) { split = true; break; }
            if (!split) { expect(false, "every generated name splits into one word from each half"); a = 34; break; }
        }
    expect(produced.size() == 1054, "all 1,054 combinations are distinct");

    // The authority uses it, and it is not the template name.
    LocalGameplay game; game.useContent(gContent);
    auto p = warlock(40);
    std::string why;
    const auto* summoned = summonImp(game, p, why);
    expect(summoned && !summoned->name.empty(), "a summoned Imp has a name (" + why + ")");
    expect(summoned && produced.count(summoned->name) == 1,
           "and it is one of the 1,054 generated names, not \"Imp\"");
    if (!summoned) { std::cout << "PASS summon semantics: skipped, no pet\n"; return; }

    // Spell::EffectSummonPet: the same entry, alive, is refreshed IN PLACE.
    const auto guid = summoned->guid, epoch = summoned->summonEpoch;
    const auto name = summoned->name;
    std::vector<LocalRealmPlayer*> players{&p};
    expect(mutatePet(game, [](LocalRealmPet& v) { v.health = 1; v.power = 0; v.x = 900; v.y = 900; }),
           "a hurt pet 900 yards from its owner is a legal state to set up");
    p.globalCooldownMs = 0; p.cooldowns.clear(); p.categoryCooldowns.clear();
    std::string result;
    expect(game.execute(p, {LocalAction::CastSpell, 0, 688}, players, result),
           "re-casting Summon Imp while the same pet is alive is allowed");
    for (int i = 0; i < 400 && game.pets().size() && game.pets()[0].health <= 1; ++i) game.tick(0.05f, players);
    expect(game.pets().size() == 1, "and still leaves exactly one pet");
    if (game.pets().size() == 1) {
        const auto& refreshed = game.pets()[0];
        expect(refreshed.guid == guid, "it is the SAME creature: the GUID is preserved");
        expect(refreshed.summonEpoch == epoch, "and so is the summon epoch, so prepared callbacks stay valid");
        expect(refreshed.name == name, "and its name - the reference does not rename a refreshed pet");
        expect(refreshed.health == refreshed.maxHealth && refreshed.power == refreshed.maxPower,
               "health and power are refilled");
        expect(std::abs(refreshed.x - p.x) < 0.001f && std::abs(refreshed.y - p.y) < 0.001f,
               "and it is moved beside its owner rather than left where it was");
    }

    // A dead pet refuses the summon: "pet in corpse state can't be summoned".
    expect(mutatePet(game, [](LocalRealmPet& v) { v.health = 0; v.dead = true; }),
           "a dead pet is a legal roster entry");
    p.globalCooldownMs = 0; p.cooldowns.clear(); p.categoryCooldowns.clear();
    expect(!game.execute(p, {LocalAction::CastSpell, 0, 688}, players, result),
           "Summon Imp is refused while the existing pet is a corpse");
    expect(result == "Your summon is dead", "with that reason, not a spent cast");
    std::cout << "PASS summon semantics: 1,054 distinct generated names, all of them a half-0 word plus a "
                 "half-1 word; a same-entry re-summon refreshes in place with the GUID, epoch and name "
                 "preserved; a corpse refuses the cast\n";
}

// ---------------------------------------------------------------------------
// 4. Commands.
// ---------------------------------------------------------------------------
void commands() {
    // The packed encoding is the client's own, and the four commands and three
    // reactions are the whole enumeration.
    expect(pet::kCommandCount == 4 && pet::kReactionCount == 3,
           "CommandStates has four members and ReactStates three");
    expect(!pet::validPetActionId(pet::ActionType::Command, 4),
           "command 4 is HandlePetActionHelper's `default:` - there is no COMMAND_MOVE_TO at 12340");
    expect(!pet::validPetActionId(pet::ActionType::Reaction, 3), "and reaction 3 likewise");
    for (unsigned slot = 0; slot < pet::kActionBarSlots; ++slot) {
        const auto packed = pet::defaultPetActionSlot(slot);
        const auto type = pet::petActionType(packed);
        const auto action = pet::petActionId(packed);
        if (slot < 3) expect(type == pet::ActionType::Command && action == 2 - slot,
                             "bar slot " + std::to_string(slot) + " is a command, COMMAND_ATTACK - i");
        else if (slot < 7) expect(type == pet::ActionType::Passive && action == 0,
                                  "bar slot " + std::to_string(slot) + " is an empty spell slot");
        else expect(type == pet::ActionType::Reaction && action == 2 - (slot - 7),
                    "bar slot " + std::to_string(slot) + " is a reaction, REACT_AGGRESSIVE - i");
    }

    LocalGameplay game; game.useContent(gContent);
    auto p = warlock(40);
    std::string why;
    if (!summonImp(game, p, why)) { expect(false, "a pet to command (" + why + ")"); return; }
    std::vector<LocalRealmPlayer*> players{&p};
    std::string result;

    // Stay pins the pet where it is.
    expect(mutatePet(game, [](LocalRealmPet& v) { v.x = 10; v.y = 20; v.z = 0; }), "the pet is placed");
    expect(petAction(game, p, pet::ActionType::Command, pet::kStay, game.pets()[0].guid, result),
           "COMMAND_STAY is accepted");
    expect(game.pets()[0].command == LocalPetCommand::Stay, "and the pet is staying");
    expect(game.pets()[0].stayX == 10 && game.pets()[0].stayY == 20,
           "SaveStayPosition: the point it was standing on becomes its anchor");
    // The owner walks 40 yards; the pet does not follow.
    p.x = 50; p.y = 20;
    for (int i = 0; i < 40; ++i) game.tick(0.05f, players);
    expect(game.pets().size() == 1, "the pet is still there");
    expect(game.pets().size() == 1 && std::abs(game.pets()[0].x - 10) < 0.001f &&
           std::abs(game.pets()[0].y - 20) < 0.001f,
           "a staying pet does not move while its owner walks 40 yards away");

    // Follow releases it, and now it does move.
    expect(petAction(game, p, pet::ActionType::Command, pet::kFollow, game.pets()[0].guid, result),
           "COMMAND_FOLLOW is accepted");
    expect(game.pets()[0].command == LocalPetCommand::Follow, "and the pet is following");
    expect(!game.pets()[0].stayX && !game.pets()[0].stayY && !game.pets()[0].stayZ,
           "RemoveStayPosition: the anchor is dropped");
    const auto before = game.pets()[0].x;
    for (int i = 0; i < 40; ++i) game.tick(0.05f, players);
    expect(game.pets()[0].x > before, "a following pet closes on its owner");

    // Attack needs a real target the owner could attack.
    expect(!petAction(game, p, pet::ActionType::Command, pet::kAttack, game.pets()[0].guid, result),
           "COMMAND_ATTACK with nothing to attack is refused");
    const auto commandTarget = hostile(500, p.x + 3, p.y, p.guid);
    game.setRemoteNpcs({commandTarget});
    expect(petAction(game, p, pet::ActionType::Command, pet::kAttack, game.pets()[0].guid, result, 500),
           "COMMAND_ATTACK on a real hostile is accepted");
    expect(game.pets()[0].commandAttack && game.pets()[0].targetGuid == 500, "and the pet is on it");
    // COMMAND_ATTACK is NOT a command state: HandlePetActionHelper sets
    // SetIsCommandAttack(true) and never calls SetCommandState, which is what
    // makes a staying pet return to its stay point afterwards rather than to
    // its owner (PetAI::HandleReturnMovement branches on HasCommandState).
    expect(game.pets()[0].command == LocalPetCommand::Follow,
           "and the command state is untouched - ATTACK is a flag, not a CommandState");

    // An out-of-range action id is refused rather than reinterpreted.
    LocalRealmCommand bad;
    bad.action = LocalAction::PetAction; bad.target = game.pets()[0].guid;
    bad.id = pet::packPetAction(pet::ActionType::Command, 4);
    expect(!game.execute(p, bad, players, result), "a command id of 4 is refused");
    expect(result == "Unknown pet action", "with the reference's own `default:` as the reason");
    bad.id = pet::packPetAction(pet::ActionType::Reaction, 7);
    expect(!game.execute(p, bad, players, result), "a reaction id of 7 is refused");

    // A stale pet GUID is refused, exactly as DismissPet's is.
    bad.target = 0xF140000000009999ULL;
    bad.id = pet::packPetAction(pet::ActionType::Command, pet::kFollow);
    expect(!game.execute(p, bad, players, result), "an action naming someone else's pet is refused");

    // A STAYING pet told to attack keeps COMMAND_STAY, and when the victim is
    // gone _stopAttack returns it to its stay point - not to its owner.
    {
        expect(petAction(game, p, pet::ActionType::Command, pet::kStay, game.pets()[0].guid, result),
               "the pet is told to stay");
        const auto anchorX = game.pets()[0].stayX, anchorY = game.pets()[0].stayY;
        game.setRemoteNpcs({hostile(501, p.x + 3, p.y, p.guid)});
        expect(petAction(game, p, pet::ActionType::Command, pet::kAttack, game.pets()[0].guid, result, 501),
               "and then to attack");
        expect(game.pets()[0].command == LocalPetCommand::Stay && game.pets()[0].commandAttack,
               "it is still staying, and now under an attack order");
        expect(game.pets()[0].stayX == anchorX && game.pets()[0].stayY == anchorY,
               "its stay point is untouched by the attack order");
        // The victim leaves; the pet drops the order and walks back to the
        // anchor, not to its owner.
        game.setRemoteNpcs({});
        p.x = anchorX + 40; p.y = anchorY;
        for (int i = 0; i < 60; ++i) game.tick(0.05f, players);
        expect(game.pets().size() == 1 && !game.pets()[0].commandAttack && !game.pets()[0].targetGuid,
               "_stopAttack clears the attack order with the victim");
        expect(game.pets().size() == 1 && game.pets()[0].command == LocalPetCommand::Stay,
               "and leaves the command state alone");
        expect(game.pets().size() == 1 && std::abs(game.pets()[0].x - anchorX) < 0.5f &&
               std::abs(game.pets()[0].y - anchorY) < 0.5f,
               "so the pet returns to its stay point rather than to its owner 40 yards away");
        expect(petAction(game, p, pet::ActionType::Command, pet::kFollow, game.pets()[0].guid, result),
               "and it can be released again");
    }
    // ABANDON retires the summon, which is what setDeathState(Corpse) plus
    // Pet::Update's non-hunter branch collapses to.
    expect(petAction(game, p, pet::ActionType::Command, pet::kAbandon, game.pets()[0].guid, result),
           "COMMAND_ABANDON is accepted");
    expect(game.pets().empty(), "and the summon is gone");
    expect(!petAction(game, p, pet::ActionType::Command, pet::kFollow, 0, result),
           "with no pet, a pet action is refused");
    std::cout << "PASS commands: all four CommandStates drive the real action path; a staying pet's "
                 "position is unchanged after its owner walks 40 yards and a following pet's is not; "
                 "command 4 and reaction 7 are refused as HandlePetActionHelper's `default:`\n";
}

// ---------------------------------------------------------------------------
// 5. Stances - four acquisition routes, three distinct behaviours.
// ---------------------------------------------------------------------------
void stances() {
    expect(localPetAutoAcquires(LocalRealmPet{.react = LocalPetReact::Aggressive}),
           "only REACT_AGGRESSIVE auto-selects");
    expect(!localPetAutoAcquires(LocalRealmPet{.react = LocalPetReact::Defensive}), "defensive does not");
    expect(!localPetDefendsOwner(LocalRealmPet{.react = LocalPetReact::Passive}),
           "and REACT_PASSIVE assists nobody");
    expect(localPetDefendsOwner(LocalRealmPet{.react = LocalPetReact::Defensive}),
           "while defensive does - \"as soon as the hunter does\"");

    // Route 3, OwnerAttacked: the owner picks a target. Passive must not follow.
    for (auto react : {LocalPetReact::Passive, LocalPetReact::Defensive, LocalPetReact::Aggressive}) {
        LocalGameplay game; game.useContent(gContent);
        auto p = warlock(40);
        std::string why;
        if (!summonImp(game, p, why)) { expect(false, "a pet to test (" + why + ")"); continue; }
        std::vector<LocalRealmPlayer*> players{&p};
        std::string result;
        expect(petAction(game, p, pet::ActionType::Reaction, uint32_t(react), game.pets()[0].guid, result),
               "the stance is accepted");
        expect(game.pets()[0].react == react, "and stored");
        // The owner is fighting something 3 yards away and the pet has no
        // target of its own.
        const auto victim = hostile(500, p.x + 3, p.y, p.guid);
        game.setRemoteNpcs({victim});
        p.attackTarget = 500;
        mutatePet(game, [](LocalRealmPet& v) { v.targetGuid = 0; });
        tickWith(game, players, victim, 20);
        const bool acquired = !game.pets().empty() && game.pets()[0].targetGuid == 500;
        if (react == LocalPetReact::Passive)
            expect(!acquired, "a PASSIVE pet acquires nothing while its owner fights");
        else
            expect(acquired, "a DEFENSIVE or AGGRESSIVE pet assists its owner's target");
    }

    // Route 4, SelectNextTarget's allowAutoSelect arm: aggressive only. A
    // hostile nobody is engaged with, inside MAX_AGGRO_RADIUS of the pet.
    expect(std::abs(kLocalPetAutoAcquireRange - 45.0f) < 1e-6f,
           "the autonomous scan radius is MAX_AGGRO_RADIUS, 45 yards");
    for (auto react : {LocalPetReact::Passive, LocalPetReact::Defensive, LocalPetReact::Aggressive}) {
        LocalGameplay game; game.useContent(gContent);
        auto p = warlock(40);
        std::string why;
        if (!summonImp(game, p, why)) { expect(false, "a pet to test (" + why + ")"); continue; }
        std::vector<LocalRealmPlayer*> players{&p};
        std::string result;
        expect(petAction(game, p, pet::ActionType::Reaction, uint32_t(react), game.pets()[0].guid, result),
               "the stance is accepted");
        // Nobody is fighting: the hostile holds no target and the owner has none.
        // It is retained by seeding it into the roster directly.
        const auto loner = hostile(600, p.x + 20, p.y, 0);
        game.setRemoteNpcs({loner});
        p.attackTarget = 0;
        mutatePet(game, [](LocalRealmPet& v) { v.targetGuid = 0; });
        tickWith(game, players, loner, 20);
        const bool acquired = !game.pets().empty() && game.pets()[0].targetGuid == 600;
        if (react == LocalPetReact::Aggressive)
            expect(acquired, "an AGGRESSIVE pet finds a hostile 20 yards away that nobody handed it");
        else
            expect(!acquired,
                   "a PASSIVE or DEFENSIVE pet does not - allowAutoSelect is the aggressive-only arm");
    }

    // Route 1, AttackedBy: something is hitting the pet. Not passive.
    for (auto react : {LocalPetReact::Passive, LocalPetReact::Defensive}) {
        LocalGameplay game; game.useContent(gContent);
        auto p = warlock(40);
        std::string why;
        if (!summonImp(game, p, why)) { expect(false, "a pet to test (" + why + ")"); continue; }
        std::vector<LocalRealmPlayer*> players{&p};
        std::string result;
        petAction(game, p, pet::ActionType::Reaction, uint32_t(react), game.pets()[0].guid, result);
        // Put the hostile far from the owner so the owner-attacked routes
        // cannot fire, and have it hold the PET as its victim.
        // The pet stays inside its own leash of its owner - a target acquired
        // beyond 60 yards is dropped on the same tick - and the hostile holds
        // the PET, not the owner, so only AttackedBy can fire.
        const auto ownerX = p.x, ownerY = p.y;
        mutatePet(game, [=](LocalRealmPet& v) { v.x = ownerX + 10; v.y = ownerY; v.targetGuid = 0; });
        const auto biter = hostile(700, ownerX + 13, ownerY, game.pets()[0].guid);
        game.setRemoteNpcs({biter});
        p.attackTarget = 0;
        tickWith(game, players, biter, 6);
        const bool acquired = !game.pets().empty() && game.pets()[0].targetGuid == 700;
        if (react == LocalPetReact::Passive)
            expect(!acquired, "a PASSIVE pet does not retaliate against what is hitting it");
        else
            expect(acquired, "a DEFENSIVE pet does");
    }

    // REACT_PASSIVE additionally stops the current attack.
    {
        LocalGameplay game; game.useContent(gContent);
        auto p = warlock(40);
        std::string why;
        if (summonImp(game, p, why)) {
            std::string result;
            game.setRemoteNpcs({hostile(800, p.x + 3, p.y, p.guid)});
            petAction(game, p, pet::ActionType::Command, pet::kAttack, game.pets()[0].guid, result, 800);
            expect(game.pets()[0].targetGuid == 800, "the pet is on a target");
            petAction(game, p, pet::ActionType::Reaction, pet::kPassive, game.pets()[0].guid, result);
            expect(game.pets()[0].targetGuid == 0, "REACT_PASSIVE calls AttackStop: the target is dropped");
        }
    }
    std::cout << "PASS stances: all three ReactStates behave differently through the reference's own four "
                 "acquisition routes - passive acquires from none of them, defensive from the three "
                 "assist routes, aggressive additionally from the 45-yard autonomous scan; REACT_PASSIVE "
                 "drops the current target\n";
}

// ---------------------------------------------------------------------------
// 6. Levelling a live pet.
// ---------------------------------------------------------------------------
void levelling() {
    LocalGameplay game; game.useContent(gContent);
    auto p = warlock(40);
    std::string why;
    if (!summonImp(game, p, why)) { expect(false, "a pet to level (" + why + ")"); return; }
    std::vector<LocalRealmPlayer*> players{&p};
    const auto* at40 = localPetLevelStats(416, 40);
    const auto* at41 = localPetLevelStats(416, 41);
    expect(at40 && at41 && at40->health() != at41->health(), "levels 40 and 41 differ in the source table");
    expect(game.pets()[0].maxHealth == at40->health(), "the pet is at its level-40 health");
    // Half health, then the owner levels: Unit::SetMaxHealth keeps the value
    // proportional rather than refilling.
    expect(mutatePet(game, [](LocalRealmPet& v) { v.health = v.maxHealth / 2; }),
           "the pet is put at half health");
    const auto fraction = double(game.pets()[0].health) / double(game.pets()[0].maxHealth);
    p.level = 41;
    game.tick(0.05f, players);
    expect(game.pets().size() == 1 && game.pets()[0].level == 41,
           "Pet::SynchronizeLevelWithOwner takes a SUMMON_PET to its owner's level");
    expect(game.pets().size() == 1 && game.pets()[0].maxHealth == at41->health(),
           "and its health becomes the level-41 row's");
    if (game.pets().size() == 1) {
        const auto after = double(game.pets()[0].health) / double(game.pets()[0].maxHealth);
        expect(std::abs(after - fraction) < 0.02,
               "the current value stays proportional - levelling is not a free heal");
    }
    // Down again.
    p.level = 20;
    game.tick(0.05f, players);
    expect(game.pets().size() == 1 && game.pets()[0].level == 20 &&
           game.pets()[0].maxHealth == localPetLevelStats(416, 20)->health(),
           "and it follows the owner down as well");
    std::cout << "PASS levelling: a live SUMMON_PET follows its owner up and down, recomputing health "
                 "from pet_levelstats and keeping its current value proportional\n";
}

// ---------------------------------------------------------------------------
// 7. The formats.
// ---------------------------------------------------------------------------
void formats() {
    expect(SaveVersion == 31, "Save 31: command, stance, stay point and Firebolt autocast are persisted");
    expect(lan::GameplayVersion == 86, "LAN 86: the same layout is the pet deck's");

    LocalRealmPet pet;
    pet.guid = kLocalPetGuidPrefix | 7; pet.ownerGuid = 1; pet.entry = 416; pet.displayId = 4449;
    pet.summonSpellId = 688; pet.kind = LocalPetKind::Controlled; pet.level = 40;
    pet.health = pet.maxHealth = 904; pet.resourceType = 0; pet.power = pet.maxPower = 1053;
    pet.attackPeriodMs = 2000; pet.name = "Jakyal";
    pet.x = 100; pet.y = 200; pet.z = 5; pet.orientation = 1.5f;
    expect(validLocalPet(pet), "the baseline pet is valid");

    // Only STAY and FOLLOW are command states, so the stored space is
    // 2 commands x 3 stances x 2 attack orders = 12, and every cell round-trips.
    unsigned cells = 0;
    for (auto command : {LocalPetCommand::Stay, LocalPetCommand::Follow})
        for (auto react : {LocalPetReact::Passive, LocalPetReact::Defensive, LocalPetReact::Aggressive})
            for (bool attacking : {false, true}) {
                auto live = pet;
                live.command = command; live.react = react;
                live.commandAttack = attacking;
                if (attacking) live.targetGuid = 4242;
                if (command == LocalPetCommand::Stay) { live.stayX = 110; live.stayY = 210; live.stayZ = 6; }
                expect(validLocalPet(live), "a pet in every command/react/attack cell is valid");
                Writer w; writePet(w, live);
                Reader r(w.bytes.data(), w.bytes.size());
                const auto restored = readPet(r);
                expect(r.valid && restored == live,
                       "(command " + std::to_string(unsigned(command)) + ", react " +
                       std::to_string(unsigned(react)) + ", attack " + std::to_string(int(attacking)) +
                       ") survives the save and the LAN deck byte for byte");
                ++cells;
            }
    expect(cells == 12, "all twelve command/react/attack cells checked");

    // ATTACK and ABANDON are never stored command states.
    { auto bad = pet; bad.command = LocalPetCommand::Abandon;
      expect(!validLocalPet(bad), "COMMAND_ABANDON is the dismissal, not a persisted state"); }
    { auto bad = pet; bad.command = LocalPetCommand::Attack;
      expect(!validLocalPet(bad),
             "COMMAND_ATTACK is CharmInfo::IsCommandAttack, not a CommandState"); }
    { auto bad = pet; bad.commandAttack = true;
      expect(!validLocalPet(bad), "an attack order without a victim is not a state the reference reaches"); }
    { auto bad = pet; bad.command = LocalPetCommand(4);
      expect(!validLocalPet(bad), "a command outside the enumeration is refused"); }
    { auto bad = pet; bad.react = LocalPetReact(3);
      expect(!validLocalPet(bad), "a react state outside the enumeration is refused"); }
    { auto bad = pet; bad.stayX = 1;
      expect(!validLocalPet(bad), "a pet that is not staying carries no stay point"); }
    { auto bad = pet; bad.command = LocalPetCommand::Stay; bad.stayX = std::nanf("");
      expect(!validLocalPet(bad), "and a non-finite stay point is refused"); }
    { auto guardian = pet; guardian.kind = LocalPetKind::Guardian; guardian.remainingMs = 30000;
      expect(validLocalPet(guardian), "a guardian with the default command is valid");
      auto staying = guardian; staying.command = LocalPetCommand::Stay; staying.stayX = 1;
      expect(!validLocalPet(staying), "but a commanded guardian is not - it has no CharmInfo");
      auto ordered = guardian; ordered.commandAttack = true; ordered.targetGuid = 4242;
      expect(!validLocalPet(ordered), "and neither is one under an attack order"); }
    // A stay point off the map is refused by the codec, as the pet's own
    // position is.
    { auto live = pet; live.command = LocalPetCommand::Stay; live.stayX = 1e9f;
      Writer w; writePet(w, live);
      Reader r(w.bytes.data(), w.bytes.size());
      readPet(r);
      expect(!r.valid, "a stay point outside the world is refused on read"); }

    // A Save29 pet - no command, no react, no stay point - still loads, and
    // comes back with the defaults.
    {
        auto live = pet;
        Writer w;
        w.u64(live.guid); w.u64(live.ownerGuid); w.u64(live.targetGuid);
        w.u32(live.entry); w.u32(live.displayId); w.u32(live.mapId); w.u32(live.instanceId);
        w.u32(live.summonSpellId);
        w.u8(uint8_t(live.kind)); w.u8(live.level); w.u32(live.health); w.u32(live.maxHealth);
        w.u8(live.resourceType); w.u32(live.power); w.u32(live.maxPower); w.u32(live.powerRegenElapsedMs);
        w.u32(live.attackPeriodMs); w.u32(live.remainingMs);
        w.f32(live.x); w.f32(live.y); w.f32(live.z); w.f32(live.orientation); w.f32(live.attackTimer);
        w.u8(0); w.text(live.name);
        Reader r(w.bytes.data(), w.bytes.size());
        const auto restored = readPet(r, 29);
        expect(r.valid, "a Save29 pet block still reads");
        expect(restored.command == kLocalPetDefaultCommand && restored.react == kLocalPetDefaultReact,
               "and comes back following and aggressive, which is exactly how it behaved");
        expect(!restored.stayX && !restored.stayY && !restored.stayZ, "with no stay point");
        expect(restored.name == live.name, "and its name intact - the string did not shift");
    }

    // The census did not move, and the accepted set is bit-identical.
    unsigned acceptedIds = 0;
    for (const auto& row : gImported.audit)
        if (row.status == "Supported decoder; imported") ++acceptedIds;
    expect(acceptedIds == 1004,
           "the accepted set is 1,004, unchanged: this checkpoint admits no new spell");
    // Nothing in the definition changed, so the content fingerprint must not move.
    std::vector<LocalSpellDefinition> spells;
    for (const auto& d : gImported.spells)
        if (d.clientSpell && (d.allowableClasses || d.npcOnly) && d.name.size() <= 96 && d.unsupportedReason.size() <= 256)
            spells.push_back(d);
    std::sort(spells.begin(), spells.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    expect(gContent->fingerprint != 0, "the content fingerprint is set");
    std::cout << "PASS formats: Save " << unsigned(SaveVersion) << " and LAN "
              << unsigned(lan::GameplayVersion) << "; all twelve command/react/attack cells round-trip through "
                 "the one shared layout; a Save29 pet loads with the defaults; accepted stays 1,004\n";
}

// ---------------------------------------------------------------------------
// 8. What has zero producers, stated as numbers.
// ---------------------------------------------------------------------------
void zeroProducers() {
    // pet abilities have their own NPC-only catalog; they must never
    // become ordinary player trainer candidates just to populate the pet bar.
    unsigned petSpells = 0;
    for (const auto& d : gImported.spells)
        if (localPetFireboltId(d.id) && d.npcOnly && d.unsupportedReason.empty()) ++petSpells;
    expect(petSpells == 9, "all nine Imp Firebolt ranks are imported as creature-only spells");
    for (unsigned slot = pet::kActionBarSpellStart; slot < pet::kActionBarSpellEnd; ++slot)
        expect(pet::petActionId(pet::defaultPetActionSlot(slot)) == 0,
               "bar spell slot " + std::to_string(slot) + " is empty");

    // Feeding: petFoodMask is zero for every family this build can summon, so
    // the clause has no population. Checked through the compiled families of
    // the five warlock creatures, whose CreatureFamily rows are 23/15/16/17/29.
    const uint32_t warlockPets[] = {416, 417, 1860, 1863, 17252};
    const uint32_t families[] = {23, 15, 16, 17, 29};
    for (unsigned i = 0; i < 5; ++i) {
        const auto* tmpl = localPetTemplate(warlockPets[i]);
        expect(tmpl && tmpl->family == families[i],
               "creature " + std::to_string(warlockPets[i]) + " is family " + std::to_string(families[i]));
        expect(tmpl && tmpl->creatureType == 3,
               "and CREATURE_TYPE_DEMON, so it is not a beast and cannot be fed or tamed");
    }

    // Revival: it is a HUNTER_PET mechanic, and this build has no route to one.
    // The tell is that no accepted spell carries a tame or a revive effect.
    expect(!accepted(982) && !accepted(13481) && !accepted(6991) && !accepted(2641),
           "Revive Pet, Tame Beast, Feed Pet and Dismiss Pet are all still rejected");
    expect(!accepted(691) && !accepted(697) && !accepted(712) && !accepted(30146),
           "and the four Soul Shard summons are still rejected on their reagent");
    std::cout << "PASS producer boundaries: Imp Firebolt has nine creature-only ranks; feeding has "
                 "0 families (all five summonable creatures are demons), and Revive Pet, Tame Beast, "
                 "Feed Pet, Dismiss Pet and the four reagent summons remain rejected\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "Usage: local_pets_test DBC_DIRECTORY\n"; return 2; }
    ClientTables tables; tables.load(argv[1]);
    gTables = &tables;
    gImported = tables.import();
    static LocalGameplay loader;
    gContent = std::make_shared<LocalWorldContent>();
    // One creature definition, so LocalGameplay::canAttack can resolve the
    // hostile the stance tests need. Deliberately NOT a pet creature: every
    // pet number in this suite has to come from the compiled pet catalog.
    LocalNpcDefinition enemy;
    enemy.id = 50; enemy.name = "Test hostile"; enemy.level = 20;
    enemy.health = 100000; enemy.damage = 1; enemy.armor = 0; enemy.xp = 0;
    enemy.hostile = true; enemy.respawnSeconds = 30;
    gContent->npcs = {enemy};
    loader.useContent(gContent);
    std::string error;
    std::vector<LocalSpellDefinition> catalog;
    for (const auto& d : gImported.spells)
        if (d.clientSpell && (d.allowableClasses || d.npcOnly) && d.name.size() <= 96 && d.unsupportedReason.size() <= 256)
            catalog.push_back(d);
    std::sort(catalog.begin(), catalog.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    if (!loader.setStarterSpells(catalog, "", error)) {
        std::cerr << "FAIL the shipped loader refused the imported set: " << error << "\n";
        return 1;
    }
    acquisitionReaches();
    perLevelStats();
    summonSemantics();
    commands();
    stances();
    levelling();
    formats();
    zeroProducers();
    if (gFailures) { std::cerr << gFailures << " checks failed\n"; return 1; }
    return 0;
}
