// P06 / the implementation - stances and forms: the modifiers a transition applies, the
// resource it costs and credits, the bars it drives, and the proof that leaving
// a form returns every number to its caster value.
//
// Four things are proved here.
//
// One: the form passives are the client's own amounts, not constants. Five
// boost spells are admitted through the localFormProfile-keyed predicate -
// 3025 Cat, 1178 Bear, 21178 Bear2, 9635 Dire Bear, 7381 Berserker - and this
// suite recomputes every amount from Spell.dbc's own bytes with
// SpellEffectInfo::CalcValue's arithmetic before comparing it to what the
// shipped authority produces. That includes Bear's MaxLevel = 40 plateau,
// which is checked at 39, 40 and 41.
//
// Two: the criterion's last clause, "without retaining old-form effects",
// proved numerically rather than asserted. For every form the class can take,
// every number the authority exposes is recorded out of form, in form and out
// again, and the third reading must equal the first exactly.
//
// Three: the entry resource. The cost side (ManaCostPercentage 35/13/6 % of
// base mana) was already enforced; the credit side is new - Furor's energy and
// bear-rage grant and the rage Stance Mastery and Tactical Mastery retain -
// and both are measured with and without the talents.
//
// Four: the two form lists are one. allShapeshiftForms is derived from
// kLocalForms, so a bar entry cannot name a form the realm does not model, and
// a shaman gets the Ghost Wolf button it never had.
//
// Reference: AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33.
//   AuraEffect::HandleShapeshiftBoosts  SpellAuraEffects.cpp:1350-1445 - the
//       per-form grant table. NOT SpellShapeshiftForm.dbc's stanceSpell[]:
//       0 of the nine modelled forms carries one (proved below).
//   AuraEffect::HandleAuraModShapeshift SpellAuraEffects.cpp:1991-2288 - the
//       power-type reset, the Furor branch (:2100-2132) and the warrior rage
//       clamp (:2184-2223).
//   Aura::IsRemovedOnShapeLost          SpellAuras.cpp:1131-1134, swept at
//       SpellAuraEffects.cpp:1612-1637.
//   SpellEffectInfo::CalcValue          SpellInfo.cpp:409-445 - the level clamp
//       and the DieSides rule.
//   SpellInfo::CheckShapeshift          SpellInfo.cpp:1468-1519.
//   Unit::CalcArmorReducedDamage        Unit.cpp:2216-2276 - armour penetration.
//   Unit::setPowerType                  Unit.cpp:6737-6790 - POWER_ENERGY sets
//       only the maximum, which is why the reference keeps an energy field
//       across a form change and this build's deviation is recorded.
//   Spell::CheckCast ONLY_OUTDOORS      Spell.cpp:5902-5904.
//   Player::InitDataForForm             Player.cpp:10755-10800 - sends nothing.
// Audit: the source audit.
#include "game/local_bots.hpp"
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include "game/local_form_boosts.hpp"
#include "game/local_forms.hpp"
#include "game/local_melee.hpp"
#include "game/local_services.hpp"
#include "game/local_spell_import.hpp"
#include "game/shapeshift_forms.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
using namespace wowee;
using namespace wowee::game;
namespace fs = std::filesystem;

unsigned gFailures = 0;
bool expect(bool ok, const std::string& what) {
    if (!ok) { ++gFailures; std::cerr << "FAIL " << what << "\n"; }
    return ok;
}

struct ClientTables {
    std::map<std::string, pipeline::DBCFile> files;
    std::map<std::string, std::vector<uint8_t>> bytes;
    std::map<uint32_t, uint32_t> rows;
    const pipeline::DBCFile* get(const char* name) { return &files.at(name); }
    void load(const fs::path& dbc) {
        for (const auto* name : {"Spell", "SpellRange", "SpellCastTimes", "SpellDuration", "SpellIcon",
                                 "SpellRadius", "SpellRuneCost", "SkillLine", "SkillLineAbility",
                                 "Talent", "TalentTab", "SpellShapeshiftForm"}) {
            std::ifstream f(dbc / (std::string(name) + ".dbc"), std::ios::binary);
            bytes[name] = {std::istreambuf_iterator<char>(f), {}};
            assert(files[name].load(bytes[name]));
        }
        const auto* spells = get("Spell");
        for (uint32_t row = 0; row < spells->getRecordCount(); ++row)
            rows[spells->getUInt32(row, 0)] = row;
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
    bool has(uint32_t id) const { return rows.count(id) != 0; }
    uint32_t u(uint32_t id, uint32_t col) { return get("Spell")->getUInt32(rows.at(id), col); }
    int32_t i(uint32_t id, uint32_t col) { return get("Spell")->getInt32(rows.at(id), col); }
    float f(uint32_t id, uint32_t col) { return get("Spell")->getFloat(rows.at(id), col); }
};

ClientTables* gTables = nullptr;
LocalSpellImport gImported;
std::shared_ptr<LocalWorldContent> gContent;

const LocalSpellDefinition* find(uint32_t id) {
    for (const auto& d : gImported.spells) if (d.id == id) return &d;
    return nullptr;
}
bool accepted(uint32_t id) { const auto* d = find(id); return d && d->clientSpell && d->unsupportedReason.empty(); }

LocalRealmPlayer makePlayer(uint8_t clazz, uint8_t level, uint8_t race = 4) {
    LocalRealmPlayer p;
    p.guid = 1; p.name = "Form"; p.classId = clazz; p.race = race; p.level = level;
    p.health = p.maxHealth = 100; p.gameplayInitialized = true;
    const auto pools = localResourcePools(p, *gContent);
    p.maxHealth = p.health = pools.health;
    p.maxMana = p.mana = pools.mana; p.druidManaCapacity = pools.mana;
    return p;
}

// SpellEffectInfo::CalcValue, SpellInfo.cpp:409-445, recomputed here from the
// client's own bytes so the suite never compares the build against itself.
int64_t calcValue(uint32_t spellId, uint32_t slot, uint32_t level) {
    const int64_t base = gTables->i(spellId, 80 + slot);
    const int64_t dieSides = gTables->i(spellId, 74 + slot);
    const float perLevel = gTables->f(spellId, 77 + slot);
    const int64_t baseLevel = gTables->u(spellId, 38), spellLevel = gTables->u(spellId, 39);
    const int64_t maxLevel = gTables->u(spellId, 37);
    int64_t amount = base;
    if (perLevel != 0.f) {
        int64_t l = int64_t(level);
        if (maxLevel > 0 && l > maxLevel) l = maxLevel;
        else if (l < baseLevel) l = baseLevel;
        l -= std::max(baseLevel, spellLevel);
        amount += int64_t(double(l) * double(perLevel));
    }
    if (dieSides == 1) amount += 1;
    return amount;
}

// ---------------------------------------------------------------------------
// 1. The grant table is the switch, not the DBC column.
// ---------------------------------------------------------------------------
void grantTable() {
    const auto* forms = gTables->get("SpellShapeshiftForm");
    expect(forms->getRecordCount() == 32 && forms->getFieldCount() == 35,
           "SpellShapeshiftForm.dbc is 32 records of 35 columns");
    std::set<uint32_t> withStanceSpell;
    for (uint32_t row = 0; row < forms->getRecordCount(); ++row)
        for (uint32_t k = 0; k < 8; ++k)
            if (forms->getUInt32(row, 27 + k)) withStanceSpell.insert(forms->getUInt32(row, 0));
    expect(withStanceSpell == std::set<uint32_t>({7, 10, 11, 12, 20, 21}),
           "only forms 7, 10, 11, 12, 20 and 21 carry a stanceSpell[] entry");
    unsigned modelled = 0;
    for (const auto& f : kLocalForms) if (withStanceSpell.count(f.form)) ++modelled;
    expect(modelled == 0, "0 of the nine modelled forms has a stanceSpell[], so that loop is dead here");

    // Every kLocalForms row's bar index is SpellShapeshiftForm.dbc column 1.
    unsigned barMismatch = 0;
    for (const auto& f : kLocalForms) {
        for (uint32_t row = 0; row < forms->getRecordCount(); ++row)
            if (forms->getUInt32(row, 0) == f.form && forms->getUInt32(row, 1) != f.bar) ++barMismatch;
    }
    expect(!barMismatch, "every modelled form's bonus action bar is the client's own column 1");
    std::cout << "PASS grant table: HandleShapeshiftBoosts is the grant table; 6 of 32 records use "
                 "stanceSpell[] and none is a form this build models; all 9 bar indices match column 1\n";
}

// ---------------------------------------------------------------------------
// 2. The five boost spells, admitted, with the client's own amounts.
// ---------------------------------------------------------------------------
void boostAdmission() {
    const uint32_t five[] = {3025, 1178, 21178, 9635, 7381};
    for (auto id : five) expect(accepted(id), "boost spell " + std::to_string(id) + " is accepted");
    // and nothing else from the switch is
    const uint32_t others[] = {34123, 5419, 5421, 21156, 7376, 24905, 69366, 33948, 34764,
                               40122, 40121, 54817, 54879, 27792, 27795, 49868, 71167, 67116};
    unsigned leaked = 0;
    for (auto id : others) if (accepted(id)) ++leaked;
    expect(!leaked, "the other 18 boost spells the switch names stay unaccepted");

    // The amounts, against the client's own bytes.
    const auto* cat = find(3025); const auto* bear = find(1178);
    const auto* bear2 = find(21178); const auto* dire = find(9635); const auto* zerk = find(7381);
    expect(cat && cat->passiveAttackPower == uint16_t(calcValue(3025, 0, 0)) &&
           cat->passiveAttackPowerPerLevel == gTables->f(3025, 77),
           "Cat 3025 carries its own aura-99 base and per-level term");
    expect(bear && bear->passiveAttackPower == uint16_t(calcValue(1178, 2, 0)) &&
           bear->passiveTotalStatPct[2] == uint8_t(calcValue(1178, 1, 0)),
           "Bear 1178 carries its aura-99 base and its +25% stamina");
    expect(dire && dire->passiveAttackPower == uint16_t(calcValue(9635, 2, 0)) &&
           dire->passiveTotalStatPct[2] == uint8_t(calcValue(9635, 1, 0)),
           "Dire Bear 9635 carries its aura-99 base and its +25% stamina");
    expect(zerk && zerk->passiveMeleeCritPct == uint8_t(calcValue(7381, 0, 0)) &&
           zerk->passiveMeleeCritPct == 3, "Berserker 7381 carries +3% weapon crit");
    expect(bear2 && bear2->requiredForms == 144,
           "21178 is cast for both bear forms even though its Stances column is 0");
    expect(bear2->requiredForms == 144 && gTables->u(21178, 12) == 0,
           "and the mask comes from the switch, not from Stances");
    // Battle Stance's own aura 280, read and asserted previously and
    // discarded until now.
    const auto* battle = find(2457);
    expect(battle && battle->passiveArmorPenetrationPct == uint8_t(calcValue(2457, 1, 0)) &&
           battle->passiveArmorPenetrationPct == 10,
           "Battle Stance carries its own +10% armour penetration");
    // The three amounts kLocalForms already had are now proved against the bytes:
    // decodeClientFormBoost rejects the spell outright if they disagree, so an
    // accepted 1178/9635/21178/7376-equivalent IS that proof.
    expect(localFormProfileByForm(5)->armorPercent == 100 + calcValue(1178, 0, 0) &&
           localFormProfileByForm(8)->armorPercent == 100 + calcValue(9635, 0, 0) &&
           localFormProfileByForm(5)->threatPercent == 100 + calcValue(21178, 0, 0) &&
           localFormProfileByForm(19)->takenPercent == 100 + calcValue(7381, 1, 0) &&
           localFormProfileByForm(1)->threatPercent == 100 + calcValue(3025, 1, 0),
           "the kLocalForms constants the boosts also carry equal the client's own amounts");
    std::cout << "PASS admission: 5 of the 23 boost spells the switch names are admitted, the other 18 "
                 "are not, and every carried amount is the client's own\n";
}

// ---------------------------------------------------------------------------
// 3. Attack power, stamina, health and crit, per level.
// ---------------------------------------------------------------------------
struct FormReading {
    float attackPower = 0, crit = 0, offHandCrit = 0, dodge = 0, parry = 0, block = 0, hit = 0;
    std::array<int32_t, 5> attributes{};
    uint32_t armorPenetrationPct = 0, health = 0, mana = 0, equipmentArmor = 0;
    bool operator==(const FormReading&) const = default;
};
FormReading read(const LocalRealmPlayer& p) {
    const auto s = localMeleeStats(p, *gContent);
    const auto pools = localResourcePools(p, *gContent);
    return {s.attackPower, s.crit, s.offHandCrit, s.dodge, s.parry, s.block, s.hit,
            s.attributes, s.armorPenetrationPct, pools.health, pools.mana,
            localFormEquipmentArmor(p, 1000)};
}

void amountsPerLevel() {
    struct Case { uint32_t form; uint32_t boost; uint32_t apSlot; };
    const Case cases[] = {{768, 3025, 0}, {5487, 1178, 2}, {9634, 9635, 2}};
    for (uint8_t level : {20, 40, 60, 80}) {
        auto p = makePlayer(11, level);
        const auto naked = read(p);
        for (const auto& c : cases) {
            auto shifted = makePlayer(11, level);
            enterLocalForm(shifted, *localFormProfile(c.form));
            const auto in = read(shifted);
            const int64_t expectedBoost = calcValue(c.boost, c.apSlot, level);
            // The caster-form AP plus the form's own agility term plus the boost.
            auto without = makePlayer(11, level);
            enterLocalForm(without, *localFormProfile(c.form));
            const float formOnly = in.attackPower - float(localFormBoostAttackPower(shifted, *gContent));
            expect(std::llround(in.attackPower - formOnly) == expectedBoost,
                   "level " + std::to_string(level) + " form " + std::to_string(c.form) +
                   ": the aura-99 term is the client's own " + std::to_string(expectedBoost));
            expect(localFormBoostAttackPower(shifted, *gContent) == uint32_t(expectedBoost),
                   "level " + std::to_string(level) + " form " + std::to_string(c.form) +
                   ": localFormBoostAttackPower agrees");
            // Stamina and the health it buys.
            const int64_t staminaPct = c.form == 768 ? 0 : calcValue(c.boost, 1, level);
            const int32_t expectedStamina = staminaPct
                ? int32_t(float(naked.attributes[2]) * (1 + float(staminaPct) / 100))
                : naked.attributes[2];
            expect(in.attributes[2] == expectedStamina,
                   "level " + std::to_string(level) + " form " + std::to_string(c.form) +
                   ": stamina is " + std::to_string(expectedStamina));
            if (staminaPct)
                expect(in.health > naked.health,
                       "level " + std::to_string(level) + " form " + std::to_string(c.form) +
                       ": the stamina multiplier buys health");
        }
    }
    // The level-80 headline numbers the audit measured.
    auto l80 = makePlayer(11, 80);
    const auto naked80 = read(l80).attackPower;
    auto cat = makePlayer(11, 80); enterLocalForm(cat, *localFormProfile(768));
    auto bear = makePlayer(11, 80); enterLocalForm(bear, *localFormProfile(5487));
    auto dire = makePlayer(11, 80); enterLocalForm(dire, *localFormProfile(9634));
    expect(naked80 == 150 && read(cat).attackPower == 396 && read(bear).attackPower == 270 &&
           read(dire).attackPower == 390,
           "a naked level-80 night-elf druid has 150 AP unshifted, 396 in Cat, 270 in Bear, 390 in Dire Bear");
    expect(read(bear).attributes[2] == 122 && read(l80).attributes[2] == 98,
           "and 98 stamina unshifted, 122 in Bear");
    std::cout << "PASS amounts: the aura-99 and aura-137 terms are the client's own at levels 20/40/60/80; "
                 "level 80 naked druid AP 150 -> 396 Cat / 270 Bear / 390 Dire Bear, stamina 98 -> 122\n";
}

void bearPlateau() {
    // 1178 has MaxLevel 40, so CalcValue clamps and the boost stops growing.
    int64_t at39 = 0, at40 = 0, at41 = 0;
    for (auto [level, out] : std::initializer_list<std::pair<uint8_t, int64_t*>>{
             {39, &at39}, {40, &at40}, {41, &at41}}) {
        auto p = makePlayer(11, level);
        enterLocalForm(p, *localFormProfile(5487));
        *out = localFormBoostAttackPower(p, *gContent);
    }
    expect(gTables->u(1178, 37) == 40, "Bear Form (Passive) 1178 has MaxLevel 40");
    expect(at39 == calcValue(1178, 2, 39) && at40 == calcValue(1178, 2, 40) && at41 == calcValue(1178, 2, 41),
           "the bear boost follows CalcValue at 39, 40 and 41");
    expect(at39 == 117 && at40 == 120 && at41 == 120,
           "and plateaus: 117 at 39, 120 at 40, 120 at 41");
    // Dire Bear has no ceiling.
    auto d80 = makePlayer(11, 80), d40 = makePlayer(11, 40);
    enterLocalForm(d80, *localFormProfile(9634)); enterLocalForm(d40, *localFormProfile(9634));
    expect(gTables->u(9635, 37) == 0 &&
           localFormBoostAttackPower(d80, *gContent) > localFormBoostAttackPower(d40, *gContent),
           "Dire Bear's 9635 has no MaxLevel and keeps growing");
    std::cout << "PASS plateau: Bear's MaxLevel 40 holds its attack-power term at 120 from level 40 up "
                 "(117 / 120 / 120 at 39 / 40 / 41); Dire Bear has no ceiling\n";
}

void berserkerCrit() {
    auto battle = makePlayer(1, 80), zerk = makePlayer(1, 80), none = makePlayer(1, 80);
    enterLocalForm(battle, *localFormProfile(2457));
    enterLocalForm(zerk, *localFormProfile(2458));
    const auto b = read(battle), z = read(zerk), n = read(none);
    expect(std::abs((z.crit - b.crit) - 3.f) < 1e-4f,
           "Berserker Stance has exactly 3% more weapon crit than Battle Stance");
    expect(std::abs(b.crit - n.crit) < 1e-4f, "Battle Stance changes no crit at all");
    expect(std::abs((z.offHandCrit - b.offHandCrit) - 3.f) < 1e-4f,
           "and the off hand gets it too, because the boost has no equipped-item requirement");
    // Armour penetration: Battle Stance only, and it reaches the damage rule.
    expect(b.armorPenetrationPct == 10 && z.armorPenetrationPct == 0 && n.armorPenetrationPct == 0,
           "Battle Stance is the only form with armour penetration, at 10%");
    const uint32_t armor = 8000;
    const uint32_t penetrated = localArmorAfterPenetration(armor, 80, 10);
    expect(penetrated < armor && localArmorReducedDamage(1000, penetrated, 80) >
                                 localArmorReducedDamage(1000, armor, 80),
           "and penetrated armour mitigates less, through the shipped curve");
    expect(localArmorAfterPenetration(armor, 80, 0) == armor,
           "no penetration leaves the armour alone");
    std::cout << "PASS crit and penetration: Berserker +3% on both hands, Battle +10% armour penetration "
                 "reaching Unit::CalcArmorReducedDamage's own cap\n";
}

// ---------------------------------------------------------------------------
// 4. The criterion's last clause, proved numerically.
// ---------------------------------------------------------------------------
void nothingRetained() {
    unsigned checked = 0;
    for (const auto& f : kLocalForms) {
        for (uint8_t level : {20, 40, 60, 80}) {
            auto p = makePlayer(f.clazz, level, f.clazz == 11 ? 4 : 1);
            const auto before = read(p);
            enterLocalForm(p, f);
            const auto during = read(p);
            leaveLocalForm(p);
            const auto after = read(p);
            ++checked;
            expect(after == before,
                   "form " + std::to_string(f.form) + " at level " + std::to_string(level) +
                   ": every number returns to its caster value on leaving");
            // And the form actually did something for at least the forms that
            // carry a modifier, so the equality above is not vacuous.
            if (f.form == 1 || f.form == 5 || f.form == 8 || f.form == 19)
                expect(!(during == before),
                       "form " + std::to_string(f.form) + " at level " + std::to_string(level) +
                       ": the form changed at least one number while it was active");
        }
    }
    expect(checked == 36, "all nine forms are exercised at four levels");
    // Every helper answers zero without a form, which is why there is nothing
    // to sweep: the reference's IsRemovedOnShapeLost population here is five
    // passive talent ranks, all recomputed live.
    auto p = makePlayer(11, 80);
    expect(!localFormBoostAttackPower(p, *gContent) && !localFormBoostCritPct(p, *gContent) &&
           localFormBoostStatMultiplier(p, *gContent, 2) == 1.f &&
           !localFormArmorPenetrationPct(p, *gContent),
           "out of form every boost helper answers zero");
    const uint32_t sweep[] = {16942, 16943, 16944, 17002, 24866};
    unsigned population = 0;
    for (auto id : sweep)
        if (accepted(id) && gTables->u(id, 12) && !(gTables->u(id, 6) & 0x80000u) &&
            !(gTables->u(id, 4) & 0x10000u) && (gTables->u(id, 4) & 0x40u)) ++population;
    expect(population == 5,
           "the reference's IsRemovedOnShapeLost passive population here is exactly those five talent ranks");
    std::cout << "PASS nothing retained: 36 (form, level) pairs enter and leave with every number "
                 "restored exactly; the reference's sweep population is 5 passives this build recomputes\n";
}

// ---------------------------------------------------------------------------
// 5. Entry resource: the cost side and the new credit side.
// ---------------------------------------------------------------------------
void entryResource() {
    // Cost. ManaCostPercentage of BASE mana, not the intellect-inflated pool.
    expect(gTables->u(5487, 204) == 35 && gTables->u(9634, 204) == 35 && gTables->u(768, 204) == 35 &&
           gTables->u(783, 204) == 13 && gTables->u(1066, 204) == 13 && gTables->u(2645, 204) == 6,
           "ManaCostPercentage is 35 / 13 / 6 % on the six forms that cost mana");
    auto druid = makePlayer(11, 80);
    const auto pools = localResourcePools(druid, *gContent);
    expect(pools.mana == 5361 && localBaseMana(druid) == 3496,
           "a level-80 night-elf druid has base mana 3,496 and a 5,361 pool");
    const auto* bearSpell = find(5487);
    expect(bearSpell && localSpellBaseResourceCost(druid, *gContent, *bearSpell) == 1223,
           "one 35% shift costs 1,223");
    uint32_t four = 0;
    for (auto id : {5487u, 768u, 783u, 1066u}) four += localSpellBaseResourceCost(druid, *gContent, *find(id));
    expect(four == 3354, "the four druid form entries together cost 3,354");
    // Natural Shapeshifter 3 is aura 108 operation 14 and was already accepted.
    auto reduced = makePlayer(11, 80);
    reduced.talents = {{826, 3}};
    expect(localSpellBaseResourceCost(reduced, *gContent, *bearSpell) == 856,
           "Natural Shapeshifter rank 3 takes that 1,223 to 856");

    // Credit: Furor.
    const uint32_t furorRanks[] = {17056, 17058, 17059, 17060, 17061};
    for (unsigned rank = 0; rank < 5; ++rank)
        expect(accepted(furorRanks[rank]) &&
               find(furorRanks[rank])->furorChancePct == uint8_t(calcValue(furorRanks[rank], 0, 0)),
               "Furor rank " + std::to_string(rank + 1) + " carries its own dummy amount");
    for (unsigned rank = 1; rank <= 5; ++rank) {
        auto p = makePlayer(11, 80); p.talents = {{822, uint8_t(rank)}};
        const auto chance = localFurorChancePct(p, *gContent);
        expect(chance == 20 * rank, "Furor rank " + std::to_string(rank) + " is a " +
               std::to_string(20 * rank) + "% chance / " + std::to_string(20 * rank) + " energy");
        // Cat: the energy the entry leaves.
        auto cat = p; enterLocalForm(cat, *localFormProfile(768),
                                     localFormEntryResource(p, *gContent, *localFormProfile(768), 0));
        expect(cat.mana == 20 * rank && cat.maxMana == 100,
               "entering Cat with Furor " + std::to_string(rank) + " leaves " +
               std::to_string(20 * rank) + " energy");
        // Bear: a roll below the chance grants exactly 10 rage; at or above, nothing.
        auto lucky = p, unlucky = p;
        enterLocalForm(lucky, *localFormProfile(5487),
                       localFormEntryResource(p, *gContent, *localFormProfile(5487), 20 * rank - 1));
        enterLocalForm(unlucky, *localFormProfile(5487),
                       localFormEntryResource(p, *gContent, *localFormProfile(5487), 99));
        expect(lucky.mana == kLocalFurorBearRage, "a winning bear roll grants 10 rage");
        expect(rank == 5 ? unlucky.mana == kLocalFurorBearRage : unlucky.mana == 0,
               "a losing bear roll grants none, and rank 5 cannot lose");
    }
    auto noFuror = makePlayer(11, 80);
    auto catNone = noFuror;
    enterLocalForm(catNone, *localFormProfile(768),
                   localFormEntryResource(noFuror, *gContent, *localFormProfile(768), 0));
    expect(!catNone.mana && !localFurorChancePct(noFuror, *gContent),
           "without Furor a form entry still mints nothing, exactly as before the reference");

    // Credit: Stance Mastery and Tactical Mastery.
    const uint32_t tactical[] = {12295, 12676, 12677};
    for (unsigned rank = 0; rank < 3; ++rank)
        expect(accepted(tactical[rank]) &&
               find(tactical[rank])->retainedRage == uint8_t(calcValue(tactical[rank], 0, 0)),
               "Tactical Mastery rank " + std::to_string(rank + 1) + " carries its own dummy amount");
    expect(accepted(12678) && find(12678)->retainedRage == uint8_t(calcValue(12678, 0, 0)) &&
           find(12678)->retainedRage == 10, "Stance Mastery is 10 rage and is not a talent");
    struct RageCase { bool stanceMastery; uint8_t tacticalRank; uint32_t expected; };
    const RageCase rageCases[] = {{false, 0, 0}, {true, 0, 10}, {false, 1, 5}, {false, 3, 15}, {true, 3, 25}};
    for (const auto& rc : rageCases) {
        auto w = makePlayer(1, 80, 1);
        if (rc.stanceMastery) w.knownSpells.push_back(12678);
        if (rc.tacticalRank) w.talents = {{128, rc.tacticalRank}};
        expect(localRetainedRage(w, *gContent) == rc.expected,
               "retained rage with Stance Mastery=" + std::to_string(rc.stanceMastery) +
               " Tactical Mastery=" + std::to_string(rc.tacticalRank) + " is " + std::to_string(rc.expected));
        // A stance change keeps min(previous rage, retained).
        enterLocalForm(w, *localFormProfile(2457));
        w.mana = 60;
        const auto entry = localFormEntryResource(w, *gContent, *localFormProfile(71), 0);
        enterLocalForm(w, *localFormProfile(71), entry);
        expect(w.mana == rc.expected,
               "and a Battle -> Defensive change leaves " + std::to_string(rc.expected) + " rage");
    }
    std::cout << "PASS entry resource: cost 35/13/6 % (1,223 for one 35% shift at level 80, 3,354 for four, "
                 "856 with Natural Shapeshifter 3); credit 0/20/40/60/80/100 Furor energy, 10 bear rage on a "
                 "winning roll, 0/5/10/15/25 rage retained across a stance change\n";
}

// ---------------------------------------------------------------------------
// 6. One form list.
// ---------------------------------------------------------------------------
void formLists() {
    unsigned entries = 0, modelled = 0;
    for (uint8_t clazz = 1; clazz <= 11; ++clazz) {
        const auto bar = allShapeshiftForms(clazz);
        for (const auto& e : bar) {
            ++entries;
            // Every entry's form id is the spell's own SPELL_AURA_MOD_SHAPESHIFT
            // misc value, whether or not this realm models the form.
            int32_t misc = -1;
            for (uint32_t k = 0; k < 3; ++k)
                if (gTables->u(e.spellId, 71 + k) == 6 && gTables->u(e.spellId, 95 + k) == 36)
                    misc = gTables->i(e.spellId, 110 + k);
            expect(misc == int32_t(e.formId),
                   "bar entry " + std::to_string(e.spellId) + " carries the client's own form id");
            expect(e.formId >= 1 && e.formId <= gTables->get("SpellShapeshiftForm")->getRecordCount(),
                   "and that form id is a record SpellShapeshiftForm.dbc actually has");
            // Where the realm models it, the two tables must agree - which is
            // what the static_asserts in shapeshift_forms.cpp enforce, checked
            // again here against the realm's accepted definition.
            if (const auto* profile = localFormProfile(e.spellId)) {
                ++modelled;
                expect(profile->clazz == clazz && profile->form == e.formId,
                       "bar entry " + std::to_string(e.spellId) + " agrees with kLocalForms");
                expect(accepted(e.spellId) && find(e.spellId)->formId == e.formId,
                       "and the realm accepts it with that form id");
            }
        }
        // Every kLocalForms row for this class is reachable from the bar, except
        // Dire Bear, which is a substitution onto Bear's button.
        for (const auto& f : kLocalForms) {
            if (f.clazz != clazz || f.spell == 9634) continue;
            expect(std::any_of(bar.begin(), bar.end(), [&](const auto& e) { return e.spellId == f.spell; }),
                   "kLocalForms row " + std::to_string(f.spell) + " has a bar entry");
        }
    }
    expect(entries == 14 && modelled == 8,
           "the bar table is 14 entries, 8 of them forms this realm models");
    expect(allShapeshiftForms(7).size() == 1 && allShapeshiftForms(7)[0].spellId == 2645 &&
           allShapeshiftForms(7)[0].formId == 16,
           "a shaman now has the Ghost Wolf button it never had");
    expect(allShapeshiftForms(6).empty(),
           "the three death-knight presences are gone: none is a shapeshift aura at all");
    // The forms the realm does not model keep their button for connected play,
    // and the local realm's own bar builder drops them by acceptance instead.
    unsigned unmodelled = 0;
    for (uint8_t clazz = 1; clazz <= 11; ++clazz)
        for (const auto& e : allShapeshiftForms(clazz))
            if (!localFormProfile(e.spellId)) { ++unmodelled; expect(!accepted(e.spellId),
                   "bar entry " + std::to_string(e.spellId) + " is one the local realm refuses"); }
    expect(unmodelled == 6,
           "six entries - Stealth, Shadowform, Moonkin, Tree of Life and the two flight forms - are "
           "client forms this realm does not model, kept for connected play and filtered out of the "
           "local stance bar by its acceptance test");
    // The presences are not shapeshift auras at all.
    for (auto id : {48266u, 48263u, 48265u}) {
        bool hasShapeshiftEffect = false;
        for (uint32_t k = 0; k < 3; ++k)
            if (gTables->u(id, 71 + k) == 6 && gTables->u(id, 95 + k) == 36) hasShapeshiftEffect = true;
        expect(!hasShapeshiftEffect,
               "death-knight presence " + std::to_string(id) + " carries no aura-36 effect");
    }
    expect(gTables->get("SpellShapeshiftForm")->getRecordCount() == 32,
           "and the form ids 33 and 34 they used to claim do not exist");
    // knownShapeshiftForms still substitutes Dire Bear onto Bear's button.
    std::unordered_set<uint32_t> known{5487, 9634, 768};
    const auto shown = knownShapeshiftForms(11, known);
    expect(shown.size() == 2 && shown[0].spellId == 9634 && shown[0].formId == 8,
           "a druid who knows Dire Bear sees it on Bear's button and gets two buttons, not three");
    std::cout << "PASS form lists: one table of 14 entries, 8 of them the realm's own and agreeing with it by "
                 "static_assert, 6 client forms kept for connected play and filtered out locally, every form id "
                 "the client's own aura-36 misc value, the shaman button restored and the three "
                 "non-shapeshift presences removed\n";
}

// ---------------------------------------------------------------------------
// 7. Travel Form's own attribute.
// ---------------------------------------------------------------------------
void outdoors() {
    unsigned carriers = 0;
    for (const auto& f : kLocalForms) if (gTables->u(f.spell, 4) & 0x8000u) ++carriers;
    expect(carriers == 2 && (gTables->u(783, 4) & 0x8000u) && (gTables->u(2645, 4) & 0x8000u),
           "exactly two modelled form spells carry SPELL_ATTR0_ONLY_OUTDOORS: Travel Form and Ghost Wolf");
    for (auto id : {783u, 2645u}) expect(find(id) && find(id)->sourceOnlyOutdoors,
           "and the importer now carries that bit for " + std::to_string(id));
    for (auto id : {5487u, 9634u, 768u, 1066u, 2457u, 71u, 2458u})
        expect(find(id) && !find(id)->sourceOnlyOutdoors,
               "and not for " + std::to_string(id));
    auto p = makePlayer(11, 80);
    const auto* travel = find(783);
    p.movementState = 0;
    expect(localFormEnvironmentRetained(p, *travel) && localFormEnvironmentReady(p, *travel),
           "Travel Form is allowed outdoors");
    p.movementState = kLocalMovementIndoors;
    expect(!localFormEnvironmentRetained(p, *travel) && !localFormEnvironmentReady(p, *travel),
           "and refused indoors, which it was not before the reference");
    const auto* wolf = find(2645);
    auto s = makePlayer(7, 80, 1);
    s.movementState = kLocalMovementIndoors;
    expect(!localFormEnvironmentRetained(s, *wolf), "Ghost Wolf's existing indoor refusal is unchanged");
    s.movementState = 0;
    expect(localFormEnvironmentRetained(s, *wolf), "and it is allowed outdoors");
    // Aquatic still needs liquid; Travel still refuses liquid and instances.
    auto d = makePlayer(11, 80);
    const auto* aquatic = find(1066);
    expect(!localFormEnvironmentRetained(d, *aquatic), "Aquatic Form still needs liquid");
    d.movementState = kLocalMovementInLiquid;
    expect(localFormEnvironmentRetained(d, *aquatic) && !localFormEnvironmentRetained(d, *travel),
           "and Travel Form still refuses liquid");
    d.movementState = 0; d.instanceId = 7;
    expect(!localFormEnvironmentRetained(d, *travel), "and instances");
    std::cout << "PASS outdoors: SPELL_ATTR0_ONLY_OUTDOORS is read from the client and gates both Travel "
                 "Form and Ghost Wolf, through one predicate the cast check and the eviction tick share\n";
}

// ---------------------------------------------------------------------------
// 8. The census and the formats.
// ---------------------------------------------------------------------------
void censusAndFormats() {
    unsigned acceptedIds = 0;
    for (const auto& row : gImported.audit)
        if (row.status == "Supported decoder; imported") ++acceptedIds;
    expect(acceptedIds == 1004,
           "the audited-accepted set is 1,004: 990 at the reference plus 5 boost spells and 9 entry-resource ranks");
    const uint32_t gained[] = {3025, 1178, 21178, 9635, 7381,
                               17056, 17058, 17059, 17060, 17061, 12295, 12676, 12677, 12678};
    for (auto id : gained) expect(accepted(id), std::to_string(id) + " is in the accepted set");
    expect(sizeof(gained) / sizeof(gained[0]) == 14, "and there are exactly fourteen of them");

    // the implementation moved both for the pet roster. P06 still added no persisted and no
    // replicated field of its own, which is what this check is for.
    expect(SaveVersion == 30, "Save 30: P06 added no persisted field");
    expect(lan::GameplayVersion == 85, "LAN 85: P06 added no replicated field");

    // The content fingerprint must move with each new definition field, so a
    // incompatible peer cannot join a incompatible host and disagree about a form's numbers.
    std::vector<LocalSpellDefinition> spells;
    for (const auto& d : gImported.spells)
        if (d.clientSpell && d.allowableClasses && d.name.size() <= 96 && d.unsupportedReason.size() <= 256)
            spells.push_back(d);
    std::sort(spells.begin(), spells.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    const auto fingerprintOf = [](std::vector<LocalSpellDefinition> set) {
        LocalGameplay game; auto c = std::make_shared<LocalWorldContent>();
        game.useContent(c); std::string error;
        assert(game.setStarterSpells(set, "", error));
        return game.content().fingerprint;
    };
    const auto after = fingerprintOf(spells);
    const char* names[] = {"passiveAttackPower", "passiveAttackPowerPerLevel", "passiveArmorPenetrationPct",
                           "furorChancePct", "retainedRage", "sourceOnlyOutdoors"};
    for (unsigned field = 0; field < 6; ++field) {
        auto without = spells;
        for (auto& d : without) {
            switch (field) {
                case 0: d.passiveAttackPower = 0; break;
                case 1: d.passiveAttackPowerPerLevel = 0; break;
                case 2: d.passiveArmorPenetrationPct = 0; break;
                case 3: d.furorChancePct = 0; break;
                case 4: d.retainedRage = 0; break;
                default: d.sourceOnlyOutdoors = false; break;
            }
        }
        expect(after != fingerprintOf(without),
               std::string("the content fingerprint moves with ") + names[field]);
    }
    std::cout << "PASS census and formats: 990 -> 1,004 accepted (5 boost spells, 9 entry-resource ranks, "
                 "0 lost); Save " << unsigned(SaveVersion) << " and LAN " << unsigned(lan::GameplayVersion)
              << " unchanged; the fingerprint moves with all six new definition fields\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "Usage: local_forms_test DBC_DIRECTORY\n"; return 2; }
    ClientTables tables; tables.load(argv[1]);
    gTables = &tables;
    gImported = tables.import();
    // Build the catalog through the shipped loader so the talent index, the
    // rank chains and the fingerprint are the ones the realm really uses.
    static LocalGameplay game;
    gContent = std::make_shared<LocalWorldContent>();
    game.useContent(gContent);
    std::string error;
    std::vector<LocalSpellDefinition> catalog;
    for (const auto& d : gImported.spells)
        if (d.clientSpell && d.allowableClasses && d.name.size() <= 96 && d.unsupportedReason.size() <= 256)
            catalog.push_back(d);
    std::sort(catalog.begin(), catalog.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    if (!game.setStarterSpells(catalog, "", error)) {
        std::cerr << "FAIL the shipped loader refused the imported set: " << error << "\n";
        return 1;
    }
    grantTable();
    boostAdmission();
    amountsPerLevel();
    bearPlateau();
    berserkerCrit();
    nothingRetained();
    entryResource();
    formLists();
    outdoors();
    censusAndFormats();
    if (gFailures) { std::cerr << gFailures << " checks failed\n"; return 1; }
    return 0;
}
