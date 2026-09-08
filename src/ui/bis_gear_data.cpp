// Curated "max out" gear sets - see bis_gear_data.hpp for intent.
#include "ui/bis_gear_data.hpp"

#include <cstring>
#include <unordered_map>

namespace wowee {
namespace ui {

namespace {

// Class ids (Blizzard): 1 Warrior, 2 Paladin, 3 Hunter, 4 Rogue, 5 Priest,
// 6 Death Knight, 7 Shaman, 8 Mage, 9 Warlock, 11 Druid.

// A universal staple every kit gets prepended (Hearthstone).
constexpr uint32_t kHearthstone = 6948;

// ---- Classic (level 60) ----------------------------------------------------
// Anchored on class legendaries where they exist (Thunderfury, Sulfuras,
// Atiesh variants, Rhok'delar) plus a couple of iconic epics.
const std::unordered_map<uint8_t, std::vector<uint32_t>> kClassic = {
    {1,  {19019, 17182, 16966, 16963}},          // Warrior: Thunderfury, Sulfuras, Wrath legs/helm
    {2,  {17182, 16955, 16949}},                  // Paladin: Sulfuras, Judgement set pieces
    {3,  {18713, 19361, 16939}},                  // Hunter: Rhok'delar, Dragonstalker pieces
    {4,  {19019, 17076, 16908}},                  // Rogue: Thunderfury, Bloodfang pieces
    {5,  {22631, 16922, 16919}},                  // Priest: Atiesh (Priest), Transcendence pieces
    {7,  {17182, 16963, 22637}},                  // Shaman: Sulfuras, Wrath legs, Atiesh (offhand era)
    {8,  {22589, 16916, 16929}},                  // Mage: Atiesh (Mage), Netherwind pieces
    {9,  {22630, 16932, 16936}},                  // Warlock: Atiesh (Warlock), Nemesis pieces
    {11, {22632, 16901, 16833}},                  // Druid: Atiesh (Druid), Cenarion pieces
};

// ---- TBC (level 70) --------------------------------------------------------
// Warglaives for rogue/warrior, Thori'dal for hunter, plus notable epics.
const std::unordered_map<uint8_t, std::vector<uint32_t>> kTbc = {
    {1,  {32837, 32838, 30622}},                  // Warrior: both Warglaives, Warbringer piece
    {2,  {30902, 32235}},                          // Paladin: Crystalforge/ Lightbringer weapon+armor
    {3,  {34334, 34333}},                          // Hunter: Thori'dal, Golden Bow of Quel'Thalas
    {4,  {32837, 32838}},                          // Rogue: Warglaives of Azzinoth
    {5,  {34336, 34210}},                          // Priest: Sunflare, Absolution set weapon
    {7,  {34211, 32944}},                          // Shaman: Cataclysm's Edge era staff/weapon
    {8,  {34336, 34349}},                          // Mage: Sunflare, Tempest Regalia weapon
    {9,  {34336, 34205}},                          // Warlock: Sunflare, Malefic Raiment weapon
    {11, {34335, 34181}},                          // Druid: Staff of Immaculate Recovery era
};

// ---- WotLK (level 80) ------------------------------------------------------
// Shadowmourne for plate melee, Val'anyr for healers, plus recognizable ICC
// weapons.
const std::unordered_map<uint8_t, std::vector<uint32_t>> kWotlk = {
    {1,  {49623, 50414}},                          // Warrior: Shadowmourne, Bloodfall
    {2,  {49623, 46017}},                          // Paladin: Shadowmourne (ret) / Val'anyr (holy)
    {3,  {50034, 50638}},                          // Hunter: Fal'inrush / Zod's Repeating Longbow
    {4,  {49888, 49889}},                          // Rogue: Havoc's Call / Bloodvenom Blade era
    {5,  {46017, 50719}},                          // Priest: Val'anyr, Bloodsurge weapon
    {6,  {49623, 49888}},                          // Death Knight: Shadowmourne, one-hand era
    {7,  {46017, 50656}},                          // Shaman: Val'anyr (resto), Bloodsurge (enh)
    {8,  {50734, 50655}},                          // Mage: Nibelung / Bloodsurge caster staff
    {9,  {50735, 50213}},                          // Warlock: Halion caster weapon era
    {11, {46017, 50040}},                          // Druid: Val'anyr (resto) / feral weapon era
};

} // namespace

std::vector<uint32_t> getMaxOutGear(const char* expansion, uint8_t classId) {
    const std::unordered_map<uint8_t, std::vector<uint32_t>>* table = nullptr;
    if (expansion && std::strcmp(expansion, "wotlk") == 0)      table = &kWotlk;
    else if (expansion && std::strcmp(expansion, "tbc") == 0)   table = &kTbc;
    else                                                        table = &kClassic; // classic/turtle

    std::vector<uint32_t> out{kHearthstone};
    if (table) {
        auto it = table->find(classId);
        if (it != table->end())
            out.insert(out.end(), it->second.begin(), it->second.end());
    }
    return out;
}

} // namespace ui
} // namespace wowee
