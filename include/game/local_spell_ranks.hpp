#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_trainer_spells.hpp"
#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>
namespace wowee::game {
// P04 competing and stacking auras : rank identity, spell-specific
// exclusivity, the group tables and the stacking decision, transcribed from
// AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33.
// the source audit sections 5-8 and 10.1 steps 5-7.
//
// The reference's rank identity is the `spell_ranks` table plus Talent.dbc
// (SpellMgr::LoadSpellRanks, SpellMgr.cpp:1279-1388; LoadSpellTalentRanks),
// never the client's SkillLineAbility.SupercededBySpell column, which is
// populated on 1,059 of 10,219 rows. The three SQL tables ship verbatim as
// generated rows (tools/local_realm/import_spell_stacking_tables.py) and are
// consulted at import time to fill LocalSpellDefinition::supercededBySpell and
// ::firstRankSpell; at runtime only those two definition fields are read.

// spell_ranks(first_spell_id, spell_id, rank): 3,502 rows, 598 chains, every
// chain contiguous 1..n (the loader's own validation, SpellMgr.cpp:1329-1358).
struct LocalSpellRankRow { uint32_t first=0,id=0; uint8_t rank=0; };
inline constexpr LocalSpellRankRow kLocalSpellRanks[]={
#include "game/local_spell_ranks_generated.inc"
};
// spell_group(id, spell_id): a negative spell_id names a sub-group whose
// members are included recursively (SpellMgr::GetSetOfSpellsInSpellGroup,
// SpellMgr.cpp:756-775). 532 rows, 113 groups, 75 sub-group references.
struct LocalSpellGroupRow { uint32_t group=0; int32_t spell=0; };
inline constexpr LocalSpellGroupRow kLocalSpellGroups[]={
#include "game/local_spell_groups_generated.inc"
};
// spell_group_stack_rules(group_id, stack_rule): SpellGroupStackRule,
// SpellMgr.h:360-368. 69 rows at the pin (the audit's section 7.1 counted 68;
// the pinned file has 19 EXCLUSIVE rows, not 18).
enum class LocalSpellGroupStackRule : uint8_t {
    Default=0, Exclusive=1, ExclusiveFromSameCaster=2, ExclusiveSameEffect=3, ExclusiveHighest=4 };
struct LocalSpellGroupRuleRow { uint32_t group=0; uint8_t rule=0; };
inline constexpr LocalSpellGroupRuleRow kLocalSpellGroupStackRules[]={
#include "game/local_spell_group_stack_rules_generated.inc"
};

// --- rank identity -----------------------------------------------------------
inline const LocalSpellRankRow* localSpellRankRow(uint32_t id) {
    // The rows are in (first, rank) order; an id appears at most once
    // (`UNIQUE KEY spell_id`). A sorted index is built on first use.
    static const std::vector<const LocalSpellRankRow*> byId=[] {
        std::vector<const LocalSpellRankRow*> v;
        for(const auto& r:kLocalSpellRanks)v.push_back(&r);
        std::sort(v.begin(),v.end(),[](const auto* a,const auto* b){return a->id<b->id;});
        return v;
    }();
    const auto it=std::lower_bound(byId.begin(),byId.end(),id,[](const auto* r,uint32_t key){return r->id<key;});
    return it!=byId.end()&&(*it)->id==id?*it:nullptr;
}
/// The next rank in the reference's chain (ChainEntry->next), or 0 for the
/// top rank and for a spell that is not ranked.
inline uint32_t localSpellRankNext(uint32_t id) {
    const auto* row=localSpellRankRow(id);
    if(!row)return 0;
    // Chains are contiguous in the generated rows, so the next rank, when it
    // exists, is the row that follows.
    const auto index=size_t(row-kLocalSpellRanks);
    if(index+1<sizeof(kLocalSpellRanks)/sizeof(kLocalSpellRanks[0])) {
        const auto& next=kLocalSpellRanks[index+1];
        if(next.first==row->first&&next.rank==row->rank+1)return next.id;
    }
    return 0;
}
/// SpellInfo::GetFirstRankSpell() for a spell the table knows, else 0.
inline uint32_t localSpellRankFirst(uint32_t id) {
    const auto* row=localSpellRankRow(id);return row?row->first:0;
}
/// Does any member of the chain rooted at `first` appear in trainer_spell?
/// (the implementation, the untrained-chain rule of the importer.) The chain's rows are
/// contiguous and start at `first`'s own row.
inline bool localSpellRankChainOfferedByTrainer(uint32_t first) {
    const auto* row=localSpellRankRow(first);
    if(!row||row->first!=first)return false;
    for(auto index=size_t(row-kLocalSpellRanks);index<sizeof(kLocalSpellRanks)/sizeof(kLocalSpellRanks[0])&&kLocalSpellRanks[index].first==first;++index)
        if(localTrainerOffers(kLocalSpellRanks[index].id))return true;
    return false;
}
/// Does `later` follow `first` in the linked rank chain? Walks
/// supercededBySpell, which the importer fills from spell_ranks (the
/// reference's authority) and, where a spell is absent from that table, from
/// the client's SkillLineAbility column 8 or a reviewed profile. The bound
/// makes malformed or cyclic data harmless. This is the ordering test
/// (SpellInfo::IsHighRankOf, SpellInfo.cpp:3019-3025) used by the trainer and
/// the spellbook; the stacking rule uses localSameRankChain (IsRankOf).
inline bool laterSpellRank(const LocalWorldContent& content,uint32_t first,uint32_t later) {
    for(size_t depth=0;depth<2048;++depth) {
        const auto* rank=content.spell(first);
        if(!rank||!rank->supercededBySpell)return false;
        first=rank->supercededBySpell;
        if(first==later)return true;
    }
    return false;
}
/// SpellInfo::IsRankOf (SpellInfo.cpp:3006-3009): the same first rank. A
/// definition without a first rank (a hand-built fixture) falls back to the
/// linked-chain walk in either direction, so a chain expressed only through
/// supercededBySpell still counts as one chain.
inline bool localSameRankChain(const LocalWorldContent& c,const LocalSpellDefinition& a,const LocalSpellDefinition& b) {
    if(a.id==b.id)return true;
    if(a.firstRankSpell&&b.firstRankSpell)return a.firstRankSpell==b.firstRankSpell;
    return laterSpellRank(c,a.id,b.id)||laterSpellRank(c,b.id,a.id);
}

// --- spell groups -------------------------------------------------------------
/// SpellMgr::GetSetOfSpellsInSpellGroup, SpellMgr.cpp:756-775: the members of
/// a group, expanding negative sub-group references once each.
inline void localSpellGroupMembers(uint32_t group,std::set<uint32_t>& found,std::set<uint32_t>& used) {
    if(!used.insert(group).second)return;
    for(const auto& r:kLocalSpellGroups) {
        if(r.group!=group)continue;
        if(r.spell<0)localSpellGroupMembers(uint32_t(-r.spell),found,used);
        else found.insert(uint32_t(r.spell));
    }
}
/// SpellMgr::LoadSpellGroups, SpellMgr.cpp:1700-1779: mSpellSpellGroup maps
/// every FIRST-RANK id to each group whose expanded set holds it (a
/// `spell_group` row naming a non-first rank is dropped by the loader, and
/// every lookup goes through GetFirstRankSpell()->Id). Built once.
struct LocalSpellGroupIndex {
    std::vector<std::pair<uint32_t,uint32_t>> spellGroups; // (first, group), sorted
    std::vector<std::pair<uint32_t,uint32_t>> subGroups;   // (group, sub-group), direct negative rows
    static const LocalSpellGroupIndex& get() {
        static const LocalSpellGroupIndex index=[] {
            LocalSpellGroupIndex i;
            std::set<uint32_t> groups;
            for(const auto& r:kLocalSpellGroups) {
                groups.insert(r.group);
                if(r.spell<0)i.subGroups.emplace_back(r.group,uint32_t(-r.spell));
            }
            for(const auto group:groups) {
                std::set<uint32_t> found,used;localSpellGroupMembers(group,found,used);
                for(const auto spell:found)i.spellGroups.emplace_back(spell,group);
            }
            std::sort(i.spellGroups.begin(),i.spellGroups.end());
            std::sort(i.subGroups.begin(),i.subGroups.end());
            return i;
        }();
        return index;
    }
};
/// SpellMgr::IsSpellMemberOfSpellGroup, through the expanded index.
inline bool localSpellMemberOfGroup(uint32_t first,uint32_t group) {
    const auto& v=LocalSpellGroupIndex::get().spellGroups;
    return std::binary_search(v.begin(),v.end(),std::make_pair(first,group));
}
/// The groups a first-rank id belongs to, ascending (the reference collects
/// the common groups into a std::set<SpellGroup>, which iterates ascending).
inline std::vector<uint32_t> localSpellGroupsOf(uint32_t first) {
    std::vector<uint32_t> groups;
    const auto& v=LocalSpellGroupIndex::get().spellGroups;
    for(auto it=std::lower_bound(v.begin(),v.end(),std::make_pair(first,0u));it!=v.end()&&it->first==first;++it)groups.push_back(it->second);
    return groups;
}
inline LocalSpellGroupStackRule localSpellGroupRule(uint32_t group) {
    for(const auto& r:kLocalSpellGroupStackRules)if(r.group==group)return LocalSpellGroupStackRule(r.rule);
    return LocalSpellGroupStackRule::Default;
}
/// SpellMgr::CheckSpellGroupStackRules, SpellMgr.cpp:811-856: the groups common
/// to both first ranks, minus any group whose negative sub-group holds both,
/// walked ascending; the first non-default rule wins.
inline LocalSpellGroupStackRule localSpellGroupStackRule(uint32_t firstA,uint32_t firstB) {
    if(!firstA||!firstB)return LocalSpellGroupStackRule::Default;
    std::set<uint32_t> groups;
    for(const auto group:localSpellGroupsOf(firstA)) {
        if(!localSpellMemberOfGroup(firstB,group))continue;
        bool add=true;
        const auto& subs=LocalSpellGroupIndex::get().subGroups;
        for(auto it=std::lower_bound(subs.begin(),subs.end(),std::make_pair(group,0u));it!=subs.end()&&it->first==group;++it)
            if(localSpellMemberOfGroup(firstA,it->second)&&localSpellMemberOfGroup(firstB,it->second)){add=false;break;}
        if(add)groups.insert(group);
    }
    auto rule=LocalSpellGroupStackRule::Default;
    for(const auto group:groups) {
        rule=localSpellGroupRule(group);
        if(rule!=LocalSpellGroupStackRule::Default)break;
    }
    return rule;
}

// --- SpellSpecificType ----------------------------------------------------------
/// SpellInfo.h:149-173, the values the reference uses.
enum class LocalSpellSpecific : uint8_t {
    Normal=0, Seal=1, Aura=3, Sting=4, Curse=5, Aspect=6, Tracker=7, WarlockArmor=8, MageArmor=9,
    ElementalShield=10, MagePolymorph=11, Judgement=13, WarlockCorruption=17, Food=19, Drink=20,
    FoodAndDrink=21, Presence=22, Charm=23, Scroll=24, MageArcaneBrilliance=25, PriestDivineSpirit=26, Hand=27 };
/// SpellInfo::LoadSpellSpecific, SpellInfo.cpp:2081-2253, over the columns the
/// definition carries: SpellFamilyName, SpellFamilyFlags, Dispel,
/// AuraInterruptFlags, EffectApplyAuraName (effectAura), Id, the first rank and
/// SPELL_ATTR1_NO_THREAT. Every arm is transcribed, including the ones no
/// accepted definition reaches (food / drink, scrolls, polymorph, charm,
/// trackers, presences: measured zero on the accepted set), so the answer for
/// a future admission is the reference's and not a guess.
/// SpellEffectInfo::IsAura(): a unit-owned aura effect (SPELL_EFFECT_APPLY_AURA
/// or one of the area-aura effects) or SPELL_EFFECT_PERSISTENT_AREA_AURA, with a
/// non-zero ApplyAuraName (SpellInfo.cpp:363-366, :378-388, :404-407).
inline bool localEffectIsAura(uint8_t effect,uint16_t auraName) {
    switch(effect) {
    case 6:case 27:case 35:case 65:case 119:case 128:case 129:case 143:return auraName!=0;
    default:return false;
    }
}
inline LocalSpellSpecific localSpellSpecific(const LocalSpellDefinition& d) {
    constexpr uint32_t kAuraInterruptNotSeated=0x00040000; // AURA_INTERRUPT_FLAG_NOT_SEATED
    const auto first=d.firstRankSpell?d.firstRankSpell:d.id;
    switch(d.spellFamily) {
    case 0: { // SPELLFAMILY_GENERIC
        if(d.auraInterruptFlags&kAuraInterruptNotSeated) {
            bool food=false,drink=false;
            for(unsigned k=0;k<3;++k) {
                if(!localEffectIsAura(d.sourceEffect[k],d.effectAura[k]))continue;
                switch(d.effectAura[k]) {
                case 84:case 20:food=true;break;   // MOD_REGEN, OBS_MOD_HEALTH
                case 85:case 21:drink=true;break;  // MOD_POWER_REGEN, OBS_MOD_POWER
                default:break;
                }
            }
            if(food&&drink)return LocalSpellSpecific::FoodAndDrink;
            if(food)return LocalSpellSpecific::Food;
            if(drink)return LocalSpellSpecific::Drink;
        } else switch(first) {
            case 8118:case 8099:case 8112:case 8096:case 8115:case 8091:return LocalSpellSpecific::Scroll;
            default:break;
        }
        break;
    }
    case 3: // SPELLFAMILY_MAGE
        if(d.spellFamilyFlags[0]&0x12040000u)return LocalSpellSpecific::MageArmor;
        if(d.spellFamilyFlags[0]&0x400u)return LocalSpellSpecific::MageArcaneBrilliance;
        if((d.spellFamilyFlags[0]&0x1000000u)&&d.effectAura[0]==5)return LocalSpellSpecific::MagePolymorph; // MOD_CONFUSE
        break;
    case 5: // SPELLFAMILY_WARLOCK
        if(d.dispelType==2)return LocalSpellSpecific::Curse; // DISPEL_CURSE (SharedDefines.h:1377)
        if((d.spellFamilyFlags[1]&0x20000020u)||(d.spellFamilyFlags[2]&0x00000010u))return LocalSpellSpecific::WarlockArmor;
        if((d.spellFamilyFlags[1]&0x10u)||(d.spellFamilyFlags[0]&0x2u))return LocalSpellSpecific::WarlockCorruption;
        break;
    case 6: // SPELLFAMILY_PRIEST
        if(d.spellFamilyFlags[0]&0x20u)return LocalSpellSpecific::PriestDivineSpirit;
        break;
    case 9: // SPELLFAMILY_HUNTER
        if(d.dispelType==4)return LocalSpellSpecific::Sting; // DISPEL_POISON (SharedDefines.h:1379)
        if((d.spellFamilyFlags[0]&0x00380000u)||(d.spellFamilyFlags[1]&0x00440000u)||(d.spellFamilyFlags[2]&0x00001010u))
            return LocalSpellSpecific::Aspect;
        break;
    case 10: // SPELLFAMILY_PALADIN
        if((d.spellFamilyFlags[1]&0x26000C00u)||(d.spellFamilyFlags[0]&0x0A000000u))return LocalSpellSpecific::Seal;
        if(d.spellFamilyFlags[0]&0x00002190u)return LocalSpellSpecific::Hand;
        if(d.id==20184||d.id==20185||d.id==20186)return LocalSpellSpecific::Judgement;
        if(d.spellFamilyFlags[2]&0x00000020u)return LocalSpellSpecific::Aura;
        if(d.id==41459||d.id==41469)return LocalSpellSpecific::Seal;
        break;
    case 11: // SPELLFAMILY_SHAMAN
        if((d.spellFamilyFlags[1]&0x420u)||((d.spellFamilyFlags[0]&0x00000400u)&&d.sourceNoThreat)||d.id==23552)
            return LocalSpellSpecific::ElementalShield;
        break;
    case 15: // SPELLFAMILY_DEATHKNIGHT
        if(d.id==48266||d.id==48263||d.id==48265)return LocalSpellSpecific::Presence;
        break;
    default:break;
    }
    for(unsigned k=0;k<3;++k) {
        if(d.sourceEffect[k]!=6)continue; // SPELL_EFFECT_APPLY_AURA
        switch(d.effectAura[k]) {
        case 6:case 128:case 2:case 261:return LocalSpellSpecific::Charm; // MOD_CHARM, MOD_POSSESS_PET, MOD_POSSESS, AOE_CHARM
        case 44: // TRACK_CREATURES: Gas Cloud Tracking is the reference's own workaround
            if(d.id==30645)return LocalSpellSpecific::Normal;
            [[fallthrough]];
        case 45:case 151:return LocalSpellSpecific::Tracker; // TRACK_RESOURCES, TRACK_STEALTHED
        default:break;
        }
    }
    return LocalSpellSpecific::Normal;
}
/// SpellInfo::IsAuraExclusiveBySpecificWith, SpellInfo.cpp:1406-1447: exclusive
/// whoever cast them. (The script hook OnIsAuraExclusiveBySpecificWith has no
/// registered script at the pin.)
inline bool localAuraExclusiveBySpecific(LocalSpellSpecific a,LocalSpellSpecific b) {
    using S=LocalSpellSpecific;
    switch(a) {
    case S::Tracker:case S::WarlockArmor:case S::MageArmor:case S::ElementalShield:case S::MagePolymorph:
    case S::Presence:case S::Charm:case S::Scroll:case S::MageArcaneBrilliance:case S::PriestDivineSpirit:
        return a==b;
    case S::Food:return b==S::Food||b==S::FoodAndDrink;
    case S::Drink:return b==S::Drink||b==S::FoodAndDrink;
    case S::FoodAndDrink:return b==S::Food||b==S::Drink||b==S::FoodAndDrink;
    default:return false;
    }
}
/// SpellInfo::IsAuraExclusiveBySpecificPerCasterWith, SpellInfo.cpp:1449-1465:
/// exclusive among one caster's own auras.
inline bool localAuraExclusiveBySpecificPerCaster(LocalSpellSpecific a,LocalSpellSpecific b) {
    using S=LocalSpellSpecific;
    switch(a) {
    case S::Seal:case S::Hand:case S::Aura:case S::Sting:case S::Curse:case S::Aspect:case S::Judgement:case S::WarlockCorruption:
        return a==b;
    default:return false;
    }
}

// --- the stacking decision ----------------------------------------------------------
/// The periodic aura types of Aura::CanStackWith's different-caster arm,
/// SpellAuras.cpp:2098-2120: a DoT or HoT from another caster coexists.
inline bool localPeriodicAuraType(uint16_t aura) {
    switch(aura) {
    case 3:   // SPELL_AURA_PERIODIC_DAMAGE
    case 226: // SPELL_AURA_PERIODIC_DUMMY
    case 8:   // SPELL_AURA_PERIODIC_HEAL
    case 23:  // SPELL_AURA_PERIODIC_TRIGGER_SPELL
    case 24:  // SPELL_AURA_PERIODIC_ENERGIZE
    case 64:  // SPELL_AURA_PERIODIC_MANA_LEECH
    case 53:  // SPELL_AURA_PERIODIC_LEECH
    case 62:  // SPELL_AURA_POWER_BURN
    case 21:  // SPELL_AURA_OBS_MOD_POWER
    case 20:  // SPELL_AURA_OBS_MOD_HEALTH
    case 227: // SPELL_AURA_PERIODIC_TRIGGER_SPELL_WITH_VALUE
        return true;
    default:return false;
    }
}
/// Aura::CanStackWith(existing), SpellAuras.cpp:2027-2180, for the auras this
/// realm's containers hold. `false` means the EXISTING aura is removed when
/// the new one is applied (Unit::_RemoveNoStackAurasDueToAura, Unit.cpp:4662-4683);
/// the same id from the same caster never reaches this - it refreshes in place
/// (Unit::_TryStackingOrRefreshingExistingAura, :4350-4407). Arms not
/// transcribed, each with zero accepted rows (audit section 10.3): the
/// dynamic-object exemption, the passive-rank arm (passives never enter these
/// containers), the trigger parent / child exemption, vehicle seats, the
/// item-keyed exemptions, and SPELL_ATTR0_CU_SINGLE_AURA_STACK.
inline bool localAuraCanStackWith(const LocalWorldContent& c,const LocalSpellDefinition& fresh,uint64_t freshCaster,
                                  const LocalSpellDefinition& existing,uint64_t existingCaster) {
    const bool sameCaster=freshCaster==existingCaster;
    const auto freshSpecific=localSpellSpecific(fresh),existingSpecific=localSpellSpecific(existing);
    // :2065-2067 - spell specific stack rules.
    if(localAuraExclusiveBySpecific(freshSpecific,existingSpecific)||
       (sameCaster&&localAuraExclusiveBySpecificPerCaster(freshSpecific,existingSpecific)))return false;
    // :2070-2084 - spell group stack rules. EXCLUSIVE_HIGHEST reaches here only
    // once the existing aura is known weaker or equal (Unit::IsHighestExclusiveAura
    // ran first, :4673-4677; locally the cast-time bounce below).
    switch(localSpellGroupStackRule(fresh.firstRankSpell?fresh.firstRankSpell:fresh.id,
                                    existing.firstRankSpell?existing.firstRankSpell:existing.id)) {
    case LocalSpellGroupStackRule::Exclusive:
    case LocalSpellGroupStackRule::ExclusiveHighest:return false;
    case LocalSpellGroupStackRule::ExclusiveFromSameCaster:if(sameCaster)return false;break;
    default:break;
    }
    // :2086-2087 - a different family always stacks.
    if(fresh.spellFamily!=existing.spellFamily)return true;
    if(!sameCaster) {
        // :2091-2096 - a channelled existing aura, or SPELL_ATTR3_DOT_STACKING_RULE.
        if(existing.sourceChanneled)return true;
        if(fresh.sourceDotStackingRule)return true;
        // :2098-2120 - a periodic aura that does not target an area. Every
        // imported definition carries its EffectApplyAuraName columns; a
        // hand-built one (effectMask 0, a test fixture) is read through the
        // derived periodic fields the importer would have filled from those
        // same columns (aura 3 / 8 / 20).
        const bool area=fresh.areaRadius>0||existing.areaRadius>0;
        if(!fresh.effectMask) {
            if(!area&&(fresh.periodicDamage||fresh.periodicHeal||fresh.periodicHealMaxHealthPct))return true;
        } else for(unsigned k=0;k<3;++k) {
            if(!((fresh.effectMask>>k)&1u)||!localPeriodicAuraType(fresh.effectAura[k]))continue;
            if(area)continue;
            return true;
        }
    }
    // :2160-2177 - the same rank chain, excluding the hunter flag.
    if(localSameRankChain(c,fresh,existing)&&!(fresh.spellFamily==9&&(fresh.spellFamilyFlags[1]&0x80000000u))) {
        // Each paladin's aura applies independently (Aura::UpdateTargetMap);
        // locally the area-aura path owns that case and never reaches here.
        if(!sameCaster&&freshSpecific==LocalSpellSpecific::Aura)return true;
        return false;
    }
    return true;
}
/// Spell::CheckCast, Spell.cpp:6996-7001, and Unit::IsHighestExclusiveAuraEffect,
/// Unit.cpp:4311-4348: for a pure-aura, non-area spell in a
/// SPELL_GROUP_STACK_RULE_EXCLUSIVE_HIGHEST group with a group-mate on the
/// target, `abs(new) - abs(existing)`, ties broken by the effect count; a
/// negative difference is SPELL_FAILED_AURA_BOUNCED ("A more powerful spell is
/// already active"). This is the reference's ONLY core bounce; every other
/// rank collision replaces. Returns true when the new aura is the weaker one.
inline bool localExclusiveHighestBounce(const LocalSpellDefinition& fresh,int64_t freshAmount,unsigned freshEffects,
                                        const LocalSpellDefinition& existing,int64_t existingAmount,unsigned existingEffects) {
    if(localSpellGroupStackRule(fresh.firstRankSpell?fresh.firstRankSpell:fresh.id,
                                existing.firstRankSpell?existing.firstRankSpell:existing.id)!=LocalSpellGroupStackRule::ExclusiveHighest)
        return false;
    int64_t diff=(freshAmount<0?-freshAmount:freshAmount)-(existingAmount<0?-existingAmount:existingAmount);
    if(!diff)diff=int64_t(freshEffects)-int64_t(existingEffects);
    return diff<0;
}
/// The effect count Unit::IsHighestExclusiveAuraEffect's tie-break reads: the
/// populated effect slots of the definition (its effect mask).
inline unsigned localAuraEffectCount(const LocalSpellDefinition& d) {
    unsigned n=0;for(unsigned k=0;k<3;++k)n+=(d.effectMask>>k)&1u;return n;
}
}
