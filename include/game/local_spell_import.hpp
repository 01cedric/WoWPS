#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_auction_catalog.hpp"
#include "game/local_mount_models.hpp"
#include "pipeline/dbc_loader.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace wowee::game {
struct LocalSpellImport {
    std::vector<LocalSpellDefinition> spells;
    std::vector<LocalRecipe> recipes;
    std::string diagnostic;
};

// How much of the client's own spell data this console will hold.
//
// Spell.dbc is 49,839 rows of 234 columns and is deliberately released again
// as soon as this import finishes, so what matters is what the import keeps.
// A LocalSpellDefinition costs roughly 250 bytes once its name and icon path
// are counted, and a LocalRecipe roughly 150; the caps below therefore bound
// the retained cost at about 250 KB of abilities and 300 KB of recipes on a
// console whose realm already holds a multi-megabyte world catalog. They are
// applied in ascending spell id, so a truncated import is the same truncated
// import on every console and the content fingerprint still agrees.
//
// They are also generous rather than tight: only abilities this ruleset can
// actually cast are kept, and only recipes that name both reagents and a
// created item, which is a small fraction of either table.
inline constexpr size_t kLocalMaxImportedClassAbilities = 1024;
inline constexpr size_t kLocalMaxImportedRecipes = 2048;

// SkillLineCategory 7 is a class skill line - the lines a class trainer
// teaches from. 9 and 11 are the secondary skills and primary professions.
inline constexpr uint32_t kLocalSkillCategoryClass = 7;

namespace detail {

/// The four tables every decoded spell needs, plus the optional icon table,
/// with a sorted id index for each so a bulk import is a binary search per
/// lookup rather than a scan of the whole table per spell.
struct ClientSpellTables {
    const pipeline::DBCFile* spells = nullptr;
    const pipeline::DBCFile* ranges = nullptr;
    const pipeline::DBCFile* casts = nullptr;
    const pipeline::DBCFile* durations = nullptr;
    const pipeline::DBCFile* icons = nullptr;
    const pipeline::DBCFile* runeCosts = nullptr;
    bool ready = false;
    std::vector<std::pair<uint32_t, uint32_t>> rangeIndex, castIndex, durationIndex, iconIndex, runeCostIndex;

    static void buildIndex(const pipeline::DBCFile* table,
                           std::vector<std::pair<uint32_t, uint32_t>>& index) {
        if (!table || !table->isLoaded()) return;
        index.reserve(table->getRecordCount());
        for (uint32_t row = 0; row < table->getRecordCount(); ++row)
            index.emplace_back(table->getUInt32(row, 0), row);
        std::sort(index.begin(), index.end());
    }
    static int32_t lookup(const std::vector<std::pair<uint32_t, uint32_t>>& index, uint32_t id) {
        const auto found = std::lower_bound(index.begin(), index.end(), id,
            [](const std::pair<uint32_t, uint32_t>& entry, uint32_t key) { return entry.first < key; });
        return found != index.end() && found->first == id ? int32_t(found->second) : -1;
    }
};

/// Decode one Spell.dbc row into the local ruleset's shape. Sets
/// `unsupportedReason` for anything this simulation cannot honestly run and
/// returns whether the spell came through supported.
///
/// This is the original starter-spell decoder, moved out whole so the class
/// abilities selected from SkillLineAbility.dbc go through exactly the same
/// rules. Build 12340 column layout, matching Data/expansions/wotlk/dbc_layouts.json.
inline bool decodeClientSpell(const ClientSpellTables& t, uint32_t row, LocalSpellDefinition& d) {
    const auto u=[&](uint32_t col){return t.spells->getUInt32(row,col);};
    const auto i=[&](uint32_t col){return t.spells->getInt32(row,col);};
    const auto f=[&](uint32_t col){return t.spells->getFloat(row,col);};
    const auto unavailable=[&](const std::string& reason) {
        if(d.unsupportedReason.empty()) d.unsupportedReason=reason;
    };
    const auto sourceName=t.spells->getString(row,136);
    if(!sourceName.empty()&&sourceName.size()<=96) d.name=sourceName;
    d.iconId=u(133);d.resourceType=uint8_t(u(41));d.mana=u(42);d.manaPercent=u(204);
    // Keep presentation metadata in this compact cache; local casting must not
    // load the full Spell.dbc name cache again.
    d.visualId=u(131);d.schoolMask=u(225);
    d.baseLevel=u(39);d.maxLevel=u(37);d.cooldownMs=std::max(u(29),u(30));d.globalCooldownMs=u(206);
    if(d.resourceType==1||d.resourceType==6) d.mana=(d.mana+9)/10; // displayed rage/runic units
    if(u(41)!=0&&u(41)!=1&&u(41)!=3&&u(41)!=5&&u(41)!=6)
        unavailable("This power system is not implemented");
    if(u(226)) {
        const auto runeRow=ClientSpellTables::lookup(t.runeCostIndex,u(226));
        if(runeRow<0) unavailable("SpellRuneCost record missing or incompatible");
        else {
            bool valid=true;
            for(uint32_t kind=0;kind<3;++kind) {
                const auto cost=t.runeCosts->getUInt32(runeRow,1+kind);
                if(cost>2) valid=false;
                else d.runeCost[kind]=uint8_t(cost);
            }
            const auto gain=t.runeCosts->getUInt32(runeRow,4);
            if(gain>1000||gain%10) valid=false;
            else d.runicPowerGain=uint16_t(gain/10);
            if(!valid) unavailable("Invalid or unsupported rune cost/power gain");
        }
    }
    if(u(41)==5&&(d.mana||d.manaPercent)) unavailable("Rune spell has a non-rune resource cost");
    if(u(43)||u(44)||u(45)) unavailable("Scaling or periodic resource costs are not implemented");
    if(u(4)&0x404u) unavailable("Next-swing attacks are not implemented");
    if(u(5)&0x44u) unavailable("Channeled spells are not implemented");
    if(u(12)||u(13)||u(14)||u(15)) unavailable("Shapeshift or stance requirements are not implemented");
    if(u(20)||u(21)||u(22)||u(23)||u(24)||u(25)||u(26)||u(27)) unavailable("Aura requirements are not implemented");
    if(i(68)>=0) unavailable("Spell equipment requirements are not implemented");
    for(uint32_t reagent=0;reagent<8;++reagent) if(i(52+reagent)>0) unavailable("Spell reagents are not implemented");
    if(u(50)||u(51)) unavailable("Totem requirements are not implemented");
    const auto castRow=ClientSpellTables::lookup(t.castIndex,u(28));
    if(castRow<0) unavailable("Cast-time record missing");
    else if(t.casts->getInt32(castRow,1)<0||t.casts->getInt32(castRow,1)>60000||t.casts->getInt32(castRow,2)!=0)
        unavailable("Variable or invalid cast time is not implemented");
    else d.castTimeMs=uint32_t(t.casts->getInt32(castRow,1));
    const auto durationRow=ClientSpellTables::lookup(t.durationIndex,u(40));
    if(u(40)&&durationRow<0) unavailable("Duration record missing");
    else if(durationRow>=0) {
        const auto duration=t.durations->getInt32(durationRow,1);
        if(duration>0&&duration<=600000) d.durationMs=uint32_t(duration);
        if(t.durations->getInt32(durationRow,2)!=0) unavailable("Variable duration is not implemented");
    }
    bool harm=false,healing=false;
    for(uint32_t effect=0;effect<3;++effect) {
        const auto type=u(71+effect); if(!type) continue;
        const auto target=u(86+effect), secondary=u(89+effect);
        if(secondary || (target!=1&&target!=6&&target!=21)) unavailable("Area or scripted targeting is not implemented");
        const auto base=i(80+effect), dice=i(74+effect);const auto scale=f(77+effect);
        if(base < -1 || base > 100000 || dice<0 || dice>100000 || !std::isfinite(scale)||std::abs(scale)>10000 || f(119+effect)!=0) {
            unavailable("Invalid or combo-point effect amount is not implemented");continue;
        }
        // DBC base points encode one less than the minimum; preserve the dice
        // range and choose a deterministic midpoint in the local ruleset.
        const uint32_t low=uint32_t(base+1),high=low+(dice>1?uint32_t(dice-1):0);
        if(type==2) {d.damage+=low;d.damageMax+=high;d.damagePerLevel+=scale;harm=true;}
        else if(type==10) {d.heal+=low;d.healMax+=high;d.healPerLevel+=scale;healing=true;}
        else if(type==6&&u(95+effect)==3&&d.durationMs&&u(98+effect)>0&&u(98+effect)<=d.durationMs&&scale==0&&!d.periodicDamage) {
            d.periodicDamage=(low+high)/2;d.periodicIntervalMs=u(98+effect);harm=true;
        } else unavailable("Unsupported effect "+std::to_string(type)+(type==6?" / aura "+std::to_string(u(95+effect)):""));
    }
    if(harm&&healing) unavailable("Mixed hostile/friendly spells are not implemented");
    if(!harm&&!healing) unavailable("No supported direct damage, healing or periodic damage effect");
    const auto rangeRow=ClientSpellTables::lookup(t.rangeIndex,u(46));
    if(rangeRow<0) unavailable("Range record missing");
    else {
        d.minRange=t.ranges->getFloat(rangeRow,healing?2:1);d.range=t.ranges->getFloat(rangeRow,healing?4:3);
        if(!std::isfinite(d.range)||!std::isfinite(d.minRange)||d.minRange<0||d.range<d.minRange||d.range>100)
            unavailable("Invalid or unsupported range");
    }
    if(d.mana>100000||d.manaPercent>100||d.cooldownMs>3600000||d.globalCooldownMs>60000)
        unavailable("Invalid spell resource or cooldown metadata");
    if(!t.iconIndex.empty()) {
        const auto iconRow=ClientSpellTables::lookup(t.iconIndex,d.iconId);
        if(iconRow>=0) {d.iconPath=t.icons->getString(iconRow,1);if(d.iconPath.size()>256)d.iconPath.clear();}
    }
    return d.unsupportedReason.empty();
}

// Ground mounts only: preserve the client's cast time, source creature and
// run-speed aura. Flight, vehicle and scripted mount effects stay unsupported.
inline bool decodeClientGroundMount(const ClientSpellTables& t,uint32_t row,LocalSpellDefinition& d) {
    const auto u=[&](uint32_t c){return t.spells->getUInt32(row,c);};
    const auto i=[&](uint32_t c){return t.spells->getInt32(row,c);};
    d.id=u(0);d.clientSpell=true;d.allowableClasses=0x5ff;d.resourceType=255;d.range=0;
    d.name=t.spells->getString(row,136);d.iconId=u(133);d.visualId=u(131);d.baseLevel=u(39);
    if(d.name.empty() || d.name.size()>96 || u(42) || u(43) || u(44) || u(45) || u(226))return false;
    for(unsigned e=0;e<3;++e) {
        if(!u(71+e))continue;
        if(u(71+e)!=6 || u(86+e)!=1 || u(89+e))return false;
        if(u(95+e)==78) {d.mountCreatureId=u(110+e);d.mountDisplayId=localMountDisplay(d.mountCreatureId);}
        else if(u(95+e)==31 && i(80+e)>=0 && i(80+e)<=199)d.mountSpeedPercent=uint32_t(i(80+e)+1);
        else return false;
    }
    if(!d.mountDisplayId || !d.mountSpeedPercent)return false;
    const auto cast=ClientSpellTables::lookup(t.castIndex,u(28));
    if(cast<0 || t.casts->getInt32(cast,1)<0 || t.casts->getInt32(cast,1)>10000 || t.casts->getInt32(cast,2))return false;
    d.castTimeMs=uint32_t(t.casts->getInt32(cast,1));
    const auto icon=ClientSpellTables::lookup(t.iconIndex,d.iconId);
    if(icon>=0) d.iconPath=t.icons->getString(icon,1);
    if(d.iconPath.size()>256)d.iconPath.clear();
    return true;
}

/// One SkillLineAbility.dbc row, reduced to what this import needs.
/// Build 12340: ID, SkillLine, Spell, RaceMask, ClassMask, ExcludeRace,
/// ExcludeClass, MinSkillLineRank, SupercededBySpell, AcquireMethod,
/// TrivialSkillLineRankHigh, TrivialSkillLineRankLow, CharacterPoints[2].
struct AbilityRow {
    uint32_t spellId = 0, skillId = 0, classMask = 0, supercededBy = 0;
    uint16_t requiredSkill = 0, trivialHigh = 0, trivialLow = 0;
    uint32_t category = 0;   ///< the SkillLine category this came from
};

} // namespace detail

// 3.3.5a (12340) only. Column meanings agree with the existing WotLK layout and
// AzerothCore's SpellEntry/Spell Effects Reference. No client records, text,
// artwork or numeric spell data ship with this source; everything below is read
// from the player's own installation at runtime.
//
// Without the last three tables this behaves exactly as it always did: the
// twenty hand-picked starter abilities and nothing else. With them, the class
// trainer has a real progression to sell and the profession trainer has real
// recipes, both selected from the client's own SkillLineAbility.dbc rather than
// from a list written here.
inline LocalSpellImport importClientStarterSpells(
    const pipeline::DBCFile* spells, const pipeline::DBCFile* ranges,
    const pipeline::DBCFile* casts, const pipeline::DBCFile* durations,
    const pipeline::DBCFile* icons = nullptr,
    const pipeline::DBCFile* skillLineAbility = nullptr,
    const pipeline::DBCFile* skillLine = nullptr,
    const pipeline::DBCFile* talents = nullptr,
    const pipeline::DBCFile* runeCosts = nullptr) {
    struct Starter { uint32_t id; uint8_t cls; };
    constexpr Starter starters[] = {{78,1},{635,2},{21084,2},{75,3},{2973,3},
        {1752,4},{2098,4},{585,5},{2050,5},{45462,6},{45477,6},{45902,6},
        {403,7},{331,7},{133,8},{168,8},{686,9},{687,9},{5176,11},{5185,11}};
    const auto valid=[](const pipeline::DBCFile* table, uint32_t fields) {
        return table && table->isLoaded() && table->getFieldCount() == fields && table->getRecordSize() == fields * 4;
    };
    LocalSpellImport out;
    detail::ClientSpellTables tables;
    tables.spells=spells;tables.ranges=ranges;tables.casts=casts;tables.durations=durations;
    // Range has two localized names and their locale masks: 40 fields in 12340.
    tables.ready=valid(spells,234)&&valid(ranges,40)&&valid(casts,4)&&valid(durations,4);
    if(tables.ready) {
        detail::ClientSpellTables::buildIndex(ranges,tables.rangeIndex);
        detail::ClientSpellTables::buildIndex(casts,tables.castIndex);
        detail::ClientSpellTables::buildIndex(durations,tables.durationIndex);
        if(valid(runeCosts,5)) {tables.runeCosts=runeCosts;detail::ClientSpellTables::buildIndex(runeCosts,tables.runeCostIndex);}
        if(valid(icons,2)) {tables.icons=icons;detail::ClientSpellTables::buildIndex(icons,tables.iconIndex);}
    }

    // --- Which spells are class abilities, and which are recipes -------------
    //
    // SkillLine.dbc says what a skill line is for; SkillLineAbility.dbc says
    // which spells belong to it and, for a trade skill, what it costs in skill
    // to learn and where it stops paying. Neither is server data.
    std::vector<detail::AbilityRow> wanted;
    std::vector<uint32_t> talentSpells;
    if(tables.ready&&valid(skillLineAbility,14)&&skillLine&&skillLine->isLoaded()&&skillLine->getFieldCount()>=38) {
        std::map<uint32_t,uint32_t> lineCategory;
        for(uint32_t row=0;row<skillLine->getRecordCount();++row) {
            const auto id=skillLine->getUInt32(row,0);
            const auto category=skillLine->getUInt32(row,1);
            if(id&&(category==kLocalSkillCategoryClass||category==kLocalSkillCategoryProfession||
                    category==kLocalSkillCategorySecondary)) lineCategory.emplace(id,category);
        }
        // Talent-granted spells are excluded where Talent.dbc is available: a
        // talent is bought with talent points, and a class trainer offering one
        // for gold would be selling something it does not own. Without the
        // table they are simply left in - a wrong offer, never a wrong effect.
        if(talents&&talents->isLoaded()&&talents->getFieldCount()>=13) {
            const uint32_t first=4, last=std::min<uint32_t>(talents->getFieldCount(),13);
            for(uint32_t row=0;row<talents->getRecordCount();++row)
                for(uint32_t col=first;col<last;++col)
                    if(const auto id=talents->getUInt32(row,col)) talentSpells.push_back(id);
            std::sort(talentSpells.begin(),talentSpells.end());
            talentSpells.erase(std::unique(talentSpells.begin(),talentSpells.end()),talentSpells.end());
        }
        for(uint32_t row=0;row<skillLineAbility->getRecordCount();++row) {
            detail::AbilityRow entry;
            entry.skillId=skillLineAbility->getUInt32(row,1);
            entry.spellId=skillLineAbility->getUInt32(row,2);
            const auto line=lineCategory.find(entry.skillId);
            if(!entry.spellId||line==lineCategory.end()) continue;
            entry.category=line->second;
            entry.classMask=skillLineAbility->getUInt32(row,4);
            entry.requiredSkill=uint16_t(std::min<uint32_t>(skillLineAbility->getUInt32(row,7),65535));
            entry.supercededBy=skillLineAbility->getUInt32(row,8);
            entry.trivialHigh=uint16_t(std::min<uint32_t>(skillLineAbility->getUInt32(row,10),65535));
            entry.trivialLow=uint16_t(std::min<uint32_t>(skillLineAbility->getUInt32(row,11),65535));
            if(entry.category==kLocalSkillCategoryClass) {
                // A class ability with no class mask belongs to everyone with
                // the skill line, which for a class line means the class itself;
                // one with a mask names it outright. Without either there is
                // nothing to gate a trainer on, so it is left out.
                if(!entry.classMask) continue;
                if(std::binary_search(talentSpells.begin(),talentSpells.end(),entry.spellId)) continue;
            }
            wanted.push_back(entry);
        }
        std::sort(wanted.begin(),wanted.end(),[](const auto& a,const auto& b) {
            if(a.spellId!=b.spellId) return a.spellId<b.spellId;
            return a.skillId<b.skillId;
        });
        // One entry per spell. A spell in several lines of the same kind keeps
        // the first line and the union of the class masks.
        std::vector<detail::AbilityRow> merged;
        for(auto& entry:wanted) {
            if(!merged.empty()&&merged.back().spellId==entry.spellId) {
                merged.back().classMask|=entry.classMask;
                if(!merged.back().supercededBy) merged.back().supercededBy=entry.supercededBy;
                continue;
            }
            merged.push_back(entry);
        }
        wanted=std::move(merged);
    }

    // --- One pass over Spell.dbc --------------------------------------------
    std::map<uint32_t,uint32_t> selectedRows;
    std::vector<uint32_t> mountIds;
    for(const auto& item:kLocalAuctionItems)if(item.mountSpell)mountIds.push_back(item.mountSpell);
    std::sort(mountIds.begin(),mountIds.end());mountIds.erase(std::unique(mountIds.begin(),mountIds.end()),mountIds.end());
    std::vector<std::pair<uint32_t,uint32_t>> mountRows;
    std::vector<std::pair<uint32_t,uint32_t>> abilityRows, recipeRows;  // spell id -> Spell.dbc row
    if(tables.ready) for(uint32_t row=0;row<spells->getRecordCount();++row) {
        const auto id=spells->getUInt32(row,0);
        if(std::binary_search(mountIds.begin(),mountIds.end(),id))mountRows.emplace_back(id,row);
        for(const auto& candidate:starters) if(id==candidate.id) selectedRows.emplace(id,row);
        const auto found=std::lower_bound(wanted.begin(),wanted.end(),id,
            [](const detail::AbilityRow& entry,uint32_t key){return entry.spellId<key;});
        if(found==wanted.end()||found->spellId!=id) continue;
        if(found->category==kLocalSkillCategoryClass) abilityRows.emplace_back(id,row);
        else recipeRows.emplace_back(id,row);
    }

    size_t supported=0;
    for(const auto& starter:starters) {
        LocalSpellDefinition d; d.id=starter.id;d.allowableClasses=1u<<(starter.cls-1);
        d.clientSpell=true;d.name="Spell #"+std::to_string(d.id);d.range=0;
        const auto found=selectedRows.find(starter.id);
        if(!tables.ready) d.unsupportedReason="WotLK spell tables missing or incompatible (Spell, SpellRange, SpellCastTimes, SpellDuration)";
        else if(found==selectedRows.end()) d.unsupportedReason="Starter spell is missing from installed Spell.dbc";
        else if(detail::decodeClientSpell(tables,found->second,d)) ++supported;
        out.spells.push_back(std::move(d));
    }

    // Class abilities, in ascending spell id so a truncated import truncates
    // identically on every console. Only what the ruleset can actually cast is
    // kept: an ability that would refuse to fire is not a trainer's stock, and
    // holding one costs the same memory as holding a usable one.
    size_t abilitiesRejected=0;
    for(const auto& entry:abilityRows) {
        if(out.spells.size()>=kLocalMaxImportedClassAbilities+sizeof(starters)/sizeof(starters[0])) break;
        if(selectedRows.count(entry.first)) continue;   // already a starter
        const auto row=std::lower_bound(wanted.begin(),wanted.end(),entry.first,
            [](const detail::AbilityRow& a,uint32_t key){return a.spellId<key;});
        LocalSpellDefinition d; d.id=entry.first; d.clientSpell=true; d.range=0;
        d.name="Spell #"+std::to_string(d.id);
        d.allowableClasses=row->classMask;
        d.supercededBySpell=row->supercededBy;
        if(!detail::decodeClientSpell(tables,entry.second,d)) { ++abilitiesRejected; continue; }
        out.spells.push_back(std::move(d));
    }
    size_t supportedMounts=0;
    for(const auto& entry:mountRows) {
        LocalSpellDefinition mount;
        if(detail::decodeClientGroundMount(tables,entry.second,mount)) {
            out.spells.push_back(std::move(mount));++supportedMounts;
        }
    }
    std::sort(out.spells.begin(),out.spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});

    // Recipes. A trade-skill spell this realm can run is one that names both
    // its reagents and the item it produces; anything else - an enchant, a
    // gathering spell, a scripted one - is left out rather than made up.
    for(const auto& entry:recipeRows) {
        if(out.recipes.size()>=kLocalMaxImportedRecipes) break;
        const auto row=std::lower_bound(wanted.begin(),wanted.end(),entry.first,
            [](const detail::AbilityRow& a,uint32_t key){return a.spellId<key;});
        LocalRecipe recipe;
        recipe.spellId=entry.first;
        recipe.skillId=uint16_t(std::min<uint32_t>(row->skillId,65535));
        recipe.requiredSkill=row->requiredSkill;
        recipe.trivialHigh=row->trivialHigh;recipe.trivialLow=row->trivialLow;
        const auto u=[&](uint32_t col){return spells->getUInt32(entry.second,col);};
        const auto i=[&](uint32_t col){return spells->getInt32(entry.second,col);};
        for(uint32_t effect=0;effect<3&&!recipe.createdItemId;++effect) {
            if(u(71+effect)!=24) continue;   // SPELL_EFFECT_CREATE_ITEM
            recipe.createdItemId=u(107+effect);
            const auto base=i(80+effect);
            recipe.createdCount=uint16_t(std::clamp(base+1,1,1000));
        }
        if(!recipe.createdItemId) continue;
        for(uint32_t slot=0;slot<8;++slot) {
            const auto item=i(52+slot); const auto count=i(60+slot);
            if(item<=0||count<=0||count>1000) continue;
            recipe.reagents.push_back({uint32_t(item),uint16_t(count)});
        }
        if(recipe.reagents.empty()) continue;   // a recipe that costs nothing is not a recipe
        const auto name=spells->getString(entry.second,136);
        recipe.name=!name.empty()&&name.size()<=96?name:"Recipe #"+std::to_string(recipe.spellId);
        out.recipes.push_back(std::move(recipe));
    }
    std::sort(out.recipes.begin(),out.recipes.end(),[](const auto& a,const auto& b){return a.spellId<b.spellId;});

    const size_t classAbilities=out.spells.size()-(sizeof(starters)/sizeof(starters[0]))-supportedMounts;
    out.diagnostic="Client spells: "+std::to_string(supported)+" of "+
        std::to_string(sizeof(starters)/sizeof(starters[0]))+" starter abilities supported, "+
        std::to_string(classAbilities)+" further class abilities and "+
        std::to_string(out.recipes.size())+" trade recipes read from your client. Unavailable "
        "abilities show their reason in the spell list. Ground mounts supported: "+std::to_string(supportedMounts)+".";
    if(!tables.ready) out.diagnostic="WotLK spell tables missing or incompatible; starter spells unavailable. Existing saved abilities and melee combat remain available.";
    else if(wanted.empty()) out.diagnostic+=" SkillLineAbility/SkillLine were not read, so trainers have nothing beyond the starter set.";
    else if(abilitiesRejected) out.diagnostic+=" "+std::to_string(abilitiesRejected)+
        " class abilities were left out because this ruleset cannot cast them.";
    return out;
}
}
