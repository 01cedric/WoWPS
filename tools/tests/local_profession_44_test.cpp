#include "game/local_services.hpp"
#include <cassert>
#include <iostream>
#include <set>

using namespace wowee::game;

namespace wowee::game {
const LocalItemDefinition* LocalWorldContent::item(uint32_t) const { return nullptr; }
}

int main() {
    static_assert(LocalGameplay::MaxRecipes == 1024);
    static_assert(LocalGameplay::MaxProfessions == 8);
    static_assert(kLocalMaxPrimaryProfessions == 2);

    const auto& ranks = localProfessionRanks();
    assert(ranks.size() == 6);
    const uint16_t caps[] = {75,150,225,300,375,450};
    for (size_t i=0;i<ranks.size();++i) {
        assert(ranks[i].cap == caps[i]);
        if (i) assert(ranks[i-1].cap < ranks[i].cap);
    }
    assert(localNextProfessionRank(75) && localNextProfessionRank(75)->cap == 150);
    assert(localNextProfessionRank(375) && localNextProfessionRank(375)->cap == 450);
    assert(localNextProfessionRank(450) == nullptr);

    const auto& professions = localBuiltinProfessions();
    assert(professions.size() == 14);
    std::set<uint32_t> ids;
    size_t primaries=0, secondaries=0;
    for (const auto& p : professions) {
        assert(p.id && ids.insert(p.id).second && !p.name.empty());
        if (p.category == kLocalSkillCategoryProfession) ++primaries;
        else if (p.category == kLocalSkillCategorySecondary) ++secondaries;
        else assert(false);
    }
    assert(primaries == 11 && secondaries == 3);
    assert(ids.count(755) && ids.count(773)); // Jewelcrafting + Inscription.

    LocalRecipe recipe;
    recipe.requiredSkill = 1;
    recipe.trivialLow = 50;
    recipe.trivialHigh = 100;
    assert(localCraftSkillChance(recipe, 1) == 1000);
    assert(localCraftSkillChance(recipe, 50) == 1000);
    assert(localCraftSkillChance(recipe, 75) == 500);
    assert(localCraftSkillChance(recipe, 99) == 20);
    assert(localCraftSkillChance(recipe, 100) == 0);
    assert(localCraftSkillChance(recipe, 450) == 0);

    recipe.trivialLow = 0;
    recipe.trivialHigh = 0;
    assert(localCraftSkillChance(recipe, 1) == 0);

    // A full local book comfortably exceeds the old u8/96-entry cap.
    std::vector<uint32_t> book;
    for (uint32_t i=1;i<=LocalGameplay::MaxRecipes;++i) book.push_back(100000+i);
    assert(book.size() == 1024 && book.front() == 100001 && book.back() == 101024);

    std::cout << "PASS 4.4 profession core: 14 WotLK skills, 6 rank caps, deterministic orange/yellow/green/grey progression, 1024-recipe capacity\n";
}
