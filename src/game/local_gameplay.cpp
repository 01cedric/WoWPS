#include "game/local_ward_reflection.hpp"
#include "game/local_ignite.hpp"
#include "game/local_party.hpp"
#include "game/local_gameplay.hpp"
#include "game/local_npc_auras.hpp"
#include "game/local_spell_ranks.hpp"
#include "game/local_diminishing.hpp"
#include "game/local_npc_spell_runtime.hpp"
#include "game/local_pet_spell.hpp"
#include "game/local_npc_spell_profiles.hpp"
#include "game/local_cooldowns.hpp"
#include "game/local_spell_range.hpp"
#include "game/local_spell_target_rules.hpp"
#include "game/local_spell_amount.hpp"
#include "game/local_spell_critical.hpp"
#include "game/local_proc_talents.hpp"
#include "game/local_reactive_talents.hpp"
#include "game/local_arcane.hpp"
#include "game/local_stormstrike.hpp"
#include "game/local_proc_timing.hpp"
#include "game/local_ranged.hpp"
#include "game/local_pet.hpp"
#include "game/local_pet_catalog.hpp"
#include "game/pet_action.hpp"
#include "game/local_area_aura.hpp"
#include "game/local_periodic_critical.hpp"
#include "game/local_aura_identity.hpp"
#include "game/local_proc_chance_modifiers.hpp"
#include "game/local_proc_lifecycle.hpp"
#include "game/local_spell_equipment.hpp"
#include "game/local_armor.hpp"
#include "game/local_resistance.hpp"
#include "game/local_combat_state.hpp"
#include "game/local_threat_talents.hpp"
#include "game/local_spell_threat.hpp"
#include "game/local_regeneration.hpp"
#include "game/local_forms.hpp"
#include "game/local_form_boosts.hpp"
#include "game/local_combo.hpp"
#include "game/local_talents.hpp"
#include "game/local_stat_auras.hpp"
#include "game/local_inventory_layout.hpp"
#include "game/local_mount.hpp"
#include "game/local_line_of_sight.hpp"
#include "game/local_services.hpp"
#include "game/local_scripted_portals.hpp"
#include "game/local_world_catalog.hpp"
#include "game/local_transport_passenger.hpp"
#include "core/logger.hpp"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <random>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace wowee::game {
namespace {
using Json = nlohmann::json;
constexpr float ActiveRadius = 180.0f, RetainRadius = 240.0f, CellSize = 180.0f;
constexpr uint64_t NpcPrefix = 0xf130000000000000ULL;
std::array<float,2> meleePeriods(const LocalRealmPlayer& p,const LocalWorldContent& c) {
    return {p.meleePeriodMain>0?p.meleePeriodMain:localMeleeSpeed(p,c),
            p.meleePeriodOff>0?p.meleePeriodOff:localMeleeSpeed(p,c,true)};
}
int factionRelation(const LocalFactionTemplate& from, const LocalFactionTemplate& to);
uint32_t number(const Json& j, const char* key, uint32_t fallback, uint32_t max = 1000000000) {
    if (!j.contains(key)) return fallback;
    const auto& v = j.at(key);
    if (!v.is_number_integer() || (v.is_number_integer() && v.get<int64_t>() < 0) || v.get<uint64_t>() > max)
        throw std::runtime_error(std::string("Invalid unsigned value: ") + key);
    return v.get<uint32_t>();
}
uint64_t wide(const Json& j, const char* key, uint64_t fallback) {
    if (!j.contains(key)) return fallback;
    const auto& v = j.at(key);
    if (!v.is_number_integer() || (!v.is_number_unsigned() && v.get<int64_t>() < 0))
        throw std::runtime_error(std::string("Invalid unsigned value: ") + key);
    return v.get<uint64_t>();
}
float real(const Json& j, const char* key, float fallback, float minimum, float maximum) {
    if (!j.contains(key)) return fallback;
    if (!j.at(key).is_number()) throw std::runtime_error(std::string("Invalid number: ") + key);
    const float v = j.at(key).get<float>();
    if (!std::isfinite(v) || v < minimum || v > maximum) throw std::runtime_error(std::string("Out-of-range number: ") + key);
    return v;
}
std::string label(const Json& j, const char* key, size_t max, bool required = true) {
    if (!j.contains(key) && !required) return {};
    if (!j.contains(key) || !j.at(key).is_string()) throw std::runtime_error(std::string("Missing string: ") + key);
    const auto s = j.at(key).get<std::string>();
    if ((required && s.empty()) || s.size() > max || s.find('\0') != std::string::npos)
        throw std::runtime_error(std::string("Invalid string: ") + key);
    return s;
}
const Json& array(const Json& j, const char* key, size_t max, bool required = false) {
    static const Json empty = Json::array();
    if (!j.contains(key)) {
        if (required) throw std::runtime_error(std::string("Missing array: ") + key);
        return empty;
    }
    const auto& a = j.at(key);
    if (!a.is_array() || a.size() > max) throw std::runtime_error(std::string("Invalid/big array: ") + key);
    return a;
}
void requiredId(uint32_t id, std::set<uint32_t>& seen, const char* kind) {
    if (!id || !seen.insert(id).second) throw std::runtime_error(std::string("Duplicate/zero ") + kind + " ID");
}
float distance2(float ax, float ay, float az, float bx, float by, float bz) {
    const float dx = ax - bx, dy = ay - by, dz = az - bz;
    return dx * dx + dy * dy + dz * dz;
}
float distance2(const LocalRealmPlayer& p, const LocalRealmNpc& n) {
    return p.mapId == n.mapId && p.instanceId == n.instanceId ? distance2(p.x,p.y,p.z,n.x,n.y,n.z) : std::numeric_limits<float>::max();
}
std::string cell(uint32_t map, int x, int y) { return std::to_string(map)+":"+std::to_string(x)+":"+std::to_string(y); }
uint32_t totalItem(const LocalRealmPlayer& p, uint32_t id) {
    uint32_t total = 0;
    for (const auto& s : p.inventory) if (s.itemId == id) total += s.count;
    return total;
}
bool addItem(LocalRealmPlayer& p, const LocalWorldContent& c, uint32_t id, uint32_t count) {
    if (!count) return true;
    const auto* def = c.item(id);
    if (!def || count > 65535) return false;
    for (const auto& existing : p.inventory) if (existing.itemId == id && existing.count > def->stack) return false;
    uint32_t available = uint32_t(LocalGameplay::MaxInventory - std::min(p.inventory.size(), LocalGameplay::MaxInventory)) * def->stack;
    for (const auto& stack : p.inventory) if (stack.itemId == id) available += def->stack - std::min(stack.count, def->stack);
    if (available < count) return false;
    normalizeLocalInventory(p);
    for (auto& stack : p.inventory) if (stack.itemId == id && count) {
        const auto moved = std::min(count, uint32_t(def->stack - stack.count));
        stack.count += uint16_t(moved); count -= moved;
    }
    while (count) { const uint16_t moved = uint16_t(std::min(count, uint32_t(def->stack))); p.inventory.push_back({id,moved}); count -= moved; }
    normalizeLocalInventory(p);return true;
}
void removeItem(LocalRealmPlayer& p, uint32_t id, uint32_t count) {
    normalizeLocalInventory(p);
    for (auto& stack : p.inventory) if (stack.itemId == id && count) {
        const auto removed = std::min(count, uint32_t(stack.count)); stack.count -= uint16_t(removed); count -= removed;
    }
    p.inventory.erase(std::remove_if(p.inventory.begin(),p.inventory.end(),[](const LocalItemStack& s){return !s.count;}),p.inventory.end());
    uint32_t remaining = totalItem(p, id);
    for (auto& e : p.equipment) if (e == id) {
        if (remaining) --remaining;
        else e = 0;
    }
}
bool questRewarded(const LocalRealmPlayer& p, uint32_t id) {
    return std::binary_search(p.completedQuestIds.begin(), p.completedQuestIds.end(), id);
}
void questStatus(LocalRealmPlayer& p, const LocalWorldContent& c) {
    for (auto& progress : p.quests) {
        if (progress.status == LocalQuestStatus::Rewarded) continue;
        const auto* def = c.quest(progress.id);
        if (!def) continue;
        progress.progress.resize(def->objectives.size(),0);
        bool complete = true;
        for (size_t i=0;i<def->objectives.size();++i) {
            const auto& obj=def->objectives[i];
            if(obj.type==LocalQuestObjective::Type::Collect) progress.progress[i]=uint16_t(std::min(totalItem(p,obj.entry),uint32_t(obj.count)));
            complete = complete && progress.progress[i]>=obj.count;
        }
        progress.status = complete ? LocalQuestStatus::Complete : LocalQuestStatus::Active;
    }
}
void objectiveCredit(LocalRealmPlayer& p,const LocalWorldContent& c,LocalQuestObjective::Type type,uint32_t entry) {
    for(auto& q:p.quests) {
        if(q.status==LocalQuestStatus::Rewarded)continue;
        const auto* def=c.quest(q.id);if(!def)continue;
        q.progress.resize(def->objectives.size(),0);
        for(size_t i=0;i<def->objectives.size();++i)
            if(def->objectives[i].type==type && def->objectives[i].entry==entry && q.progress[i]<def->objectives[i].count) ++q.progress[i];
    }
    questStatus(p,c);
}
uint32_t equipmentValue(const LocalRealmPlayer& p,const LocalWorldContent& c, unsigned kind) {
    uint64_t result = 0;
    for (size_t slot = 0; slot < p.equipment.size(); ++slot) {
        const auto id = p.equipment[slot];
        const auto* item = id ? c.item(id) : nullptr;
        if (!item || !localEquipmentFits(item->inventoryType, item->slot, slot)) continue;
        const auto copies = std::count(p.equipment.begin(), p.equipment.begin() + slot + 1, id);
        if (uint32_t(copies) > totalItem(p, id)) continue;
        result += kind == 0 ? item->maxHealth : kind == 1 ? item->attack : item->armor;
    }
    // Catalog values are untrusted uint32 values; leave headroom for base stats.
    return uint32_t(std::min(result, uint64_t(UINT32_MAX - 100000)));
}

bool validEquipment(const LocalRealmPlayer& p, const LocalWorldContent& c) {
    for (size_t slot = 0; slot < p.equipment.size(); ++slot) if (const auto id = p.equipment[slot]) {
        const auto* item = c.item(id);
        if (!item || !localEquipmentFits(item->inventoryType, item->slot, slot)) return false;
        if (uint32_t(std::count(p.equipment.begin(), p.equipment.end(), id)) > totalItem(p, id)) return false;
    }
    const auto* main = c.item(p.equipment[localEquipmentIndex(LocalEquipmentSlot::MainHand)]);
    return !main || main->inventoryType != 17 || !p.equipment[localEquipmentIndex(LocalEquipmentSlot::OffHand)];
}

// Explicit target is slot+1, zero asks for an empty compatible slot followed
// by deterministic replacement. Build a candidate before changing any stats.
bool equipItem(LocalRealmPlayer& p, const LocalWorldContent& c, uint32_t id, uint64_t target) {
    const auto* item = c.item(id);
    const uint32_t owned = totalItem(p, id);
    if (!item || !owned || target > kLocalEquipmentSlotCount || !validEquipment(p, c)) return false;
    if(const auto* meta=localAuctionMetadata(id)){
        if(p.level<meta->requiredLevel || !p.classId || p.classId>32 || !p.race || p.race>32 ||
           (meta->allowableClasses && !(meta->allowableClasses&(1u<<(p.classId-1)))) ||
           (meta->allowableRaces && !(meta->allowableRaces&(1u<<(p.race-1)))))return false;
    }
    const uint32_t mask = localEquipmentSlotMask(item->inventoryType, item->slot);
    if (!mask) return false;
    size_t slot = target ? size_t(target - 1) : kLocalEquipmentSlotCount;
    const auto mainSlot = localEquipmentIndex(LocalEquipmentSlot::MainHand);
    const auto offSlot = localEquipmentIndex(LocalEquipmentSlot::OffHand);
    const auto* main = c.item(p.equipment[mainSlot]);
    const auto usable = [&](size_t candidate) {
        return (mask & localEquipmentSlotBit(candidate)) &&
            !(candidate == offSlot && main && main->inventoryType == 17);
    };
    if (target && !usable(slot)) return false;
    if (!target) {
        const auto equipped = uint32_t(std::count(p.equipment.begin(), p.equipment.end(), id));
        if (owned <= equipped) return true; // All owned copies already worn.
        for (size_t i = 0; i < p.equipment.size(); ++i) if (usable(i) && !p.equipment[i]) { slot = i; break; }
        if (slot == kLocalEquipmentSlotCount)
            for (size_t i = 0; i < p.equipment.size(); ++i) if (usable(i)) { slot = i; break; }
        if (slot == kLocalEquipmentSlotCount) return false;
    }
    auto candidate = p;
    candidate.equipment[slot] = id;
    // Moving the last available copy between compatible slots is allowed;
    // equipping one inventory copy in both hands/fingers is not.
    uint32_t equipped = uint32_t(std::count(candidate.equipment.begin(), candidate.equipment.end(), id));
    for (size_t i = 0; i < candidate.equipment.size() && equipped > owned; ++i)
        if (i != slot && candidate.equipment[i] == id) { candidate.equipment[i] = 0; --equipped; }
    if (slot == mainSlot && item->inventoryType == 17) candidate.equipment[offSlot] = 0;
    if (!validEquipment(candidate, c)) return false;
    p.equipment = candidate.equipment;
    return true;
}
void stats(LocalRealmPlayer& p,const LocalWorldContent& c,bool heal) {
    p.xpToLevel=uint32_t(p.level)*uint32_t(p.level)*100+300;
    const auto pools=localResourcePools(p,c);
    p.maxHealth=pools.health;p.maxMana=p.resourceType==LocalResourceType::Mana?pools.mana:100;
    p.druidManaCapacity=p.classId==11?pools.mana:0;
    if(p.classId==11&&p.formSpellId){
        p.druidMana=std::min(p.druidMana,p.druidManaCapacity);
        if(p.druidMana==p.druidManaCapacity)p.druidManaRemainder=0;
    }
    p.health=heal?p.maxHealth:std::min(p.health,p.maxHealth);
    p.mana=heal ? (p.resourceType == LocalResourceType::Rage || p.resourceType == LocalResourceType::RunicPower ? 0 : p.maxMana) : std::min(p.mana,p.maxMana);
    if(heal&&p.classId==11&&p.formSpellId){p.druidMana=p.druidManaCapacity;p.druidManaRemainder=0;}
    discardLocalFullRegenerationCredit(p);
}
void experience(LocalRealmPlayer& p,const LocalWorldContent& c,uint32_t reward) {
    p.xp=uint32_t(std::min(uint64_t(p.xp)+reward,uint64_t(1000000000)));
    while(p.level<80 && p.xp>=p.xpToLevel) {p.xp-=p.xpToLevel;++p.level;stats(p,c,true);}
    if(p.level==80)p.xp=0;
}
void clearCast(LocalRealmPlayer& p, LocalCastStatus status) {
    p.castingSpellId=0;p.castTarget=0;p.castRemainingMs=0;p.castTotalMs=0;p.castStatus=status;
    p.castPushbackMs=0;p.castPushbackCount=0;clearLocalPreparedCost(p);
}
void finishLocalTeleport(LocalRealmPlayer& p) {
    // Earth Shield carries CHANGE_MAP: expire immediately on map/instance
    // transfer, while same-map near teleports retain it (source Player.cpp).
    std::erase_if(p.statAuras,[&](const auto& aura){return (aura.spellId==974||aura.spellId==32593||aura.spellId==32594||aura.spellId==49283||aura.spellId==49284)&&
        (aura.mapId!=p.mapId||aura.instanceId!=p.instanceId);});
    if(p.castingSpellId)clearCast(p,LocalCastStatus::Interrupted);
    leaveLocalForm(p);
    clearLocalCombo(p);
    localStopRangedAuto(p);p.attackTarget=0;p.attackTimer=0;p.mountSpellId=0;p.movementState=0;
    p.falling=false;p.fallStartZ=p.z;p.fallRevision=p.positionRevision;
    p.flight={};p.transportEntry=0;p.transportOffsetX=p.transportOffsetY=p.transportOffsetZ=p.transportLastYaw=0;
    LOG_INFO("[LOCAL_TRAVEL_STATE] cleared player=",p.guid," map=",p.mapId," instance=",p.instanceId," revision=",p.positionRevision);
}
// SpellEffectInfo::CalcValue, SpellInfo.cpp:414-431, transcribed whole since
// the reference. The reference clamps the caster's level *down* to MaxLevel (only when
// MaxLevel is non-zero) or else *up* to BaseLevel, and then subtracts
// max(BaseLevel, SpellLevel) - the xinef arm for rows whose BaseLevel exceeds
// their SpellLevel. The result may be negative, which reduces the amount; the
// build's own [0, 1000000] clamp is what stops it going below zero. previously
// d.baseLevel was Spell.dbc column 39, SpellLevel, so the subtrahend happened to
// be max(BaseLevel, SpellLevel) on every accepted definition of this realm and
// no amount moves - measured, not assumed - but the low clamp and the negative
// arm were both missing and the column was the wrong one.
uint32_t scaledSpellAmount(const LocalRealmPlayer& p,const LocalSpellDefinition& d,
                          uint32_t low,uint32_t high,float scale) {
    double amount=(double(low)+std::max(low,high))*0.5;
    if(scale!=0.f) {
        int64_t level=int64_t(p.level);
        if(level>int64_t(d.maxLevel)&&d.maxLevel>0)level=int64_t(d.maxLevel);
        else if(level<int64_t(d.baseLevel))level=int64_t(d.baseLevel);
        level-=int64_t(std::max<uint32_t>(d.baseLevel,d.spellLevel));
        amount+=double(level)*double(scale);
    }
    return uint32_t(std::clamp(amount,0.0,1000000.0));
}
uint32_t spellAmount(const LocalRealmPlayer& p, const LocalSpellDefinition& d, bool heal) {
    const auto low=heal?d.heal:d.damage, high=heal?d.healMax:d.damageMax;
    if(!d.clientSpell) return low + (heal?0:uint32_t(p.level-1)*2);
    return scaledSpellAmount(p,d,low,high,heal?d.healPerLevel:d.damagePerLevel);
}
// laterSpellRank (the linked-chain ordering walk) lives in
// game/local_spell_ranks.hpp since the implementation, beside the rank identity it reads:
// the authority is the reference's spell_ranks table, with the client's
// SkillLineAbility links filling the chains the table does not list.

// The amount Unit::IsHighestExclusiveAuraEffect (Unit.cpp:4311-4348) compares
// for a SPELL_GROUP_STACK_RULE_EXCLUSIVE_HIGHEST group: the existing aura's
// AuraEffect::GetAmount() against the new spell's CalcValue(caster)
// (Spell.cpp:6996-7001). In the stat container that is the proc amount (Thorns:
// the damage-shield value, snapshotted at application), else the armor, health
// or absorb amount. Thorns (group 1113) is the only accepted chain with two
// members in such a group; Mark of the Wild r1 (1089) and Death Wish (1107)
// have no accepted partner.
uint32_t localStatAuraExclusiveAmount(const LocalRealmPlayer& caster,const LocalWorldContent& c,
                                      const LocalSpellDefinition& d,const LocalStatAura* held) {
    if(d.proc.effect!=LocalProcEffect::None) {
        if(held)return held->hasProcAmountSnapshot?held->procAmountSnapshot:d.proc.amount;
        return localProcAmountAtApplication(caster,c,d);
    }
    if(d.buffArmor)return held&&held->buffArmorSnapshot?held->buffArmorSnapshot:d.buffArmor;
    if(d.buffHealth)return d.buffHealth;
    if(d.buffAbsorb)return held?held->absorbRemaining:d.buffAbsorb;
    return 0;
}
LocalItemStack parseStack(const Json& j) {
    LocalItemStack s; s.itemId=number(j,"itemId",0,UINT32_MAX);s.count=uint16_t(number(j,"count",1,65535));
    if(!s.itemId||!s.count)throw std::runtime_error("Invalid item stack");
    return s;
}
}
namespace {
template <class T> const T* definition(const std::vector<T>& entries, uint32_t id) {
    const auto it = std::lower_bound(entries.begin(), entries.end(), id, [](const T& entry, uint32_t key) { return entry.id < key; });
    return it != entries.end() && it->id == id ? &*it : nullptr;
}
}
const LocalItemDefinition* LocalWorldContent::item(uint32_t id) const {
    if (const auto* d = definition(items, id)) return d;
    const auto found = itemCache.find(id); if (found != itemCache.end()) return &found->second;
    if (!catalog || itemCache.size() >= 16384) return nullptr;
    LocalItemDefinition d; if (!catalog->item(id, d, catalogError)) return nullptr;
    return &itemCache.emplace(id, std::move(d)).first->second;
}
const LocalSpellDefinition* LocalWorldContent::spell(uint32_t id) const { return definition(spells, id); }
namespace {
// LocalRecipe is keyed on spellId rather than on an `id` member, so it needs a
// search of its own rather than the shared definition() template.
const LocalRecipe* findRecipe(const std::vector<LocalRecipe>& entries, uint32_t spellId) {
    const auto it = std::lower_bound(entries.begin(), entries.end(), spellId,
        [](const LocalRecipe& entry, uint32_t key) { return entry.spellId < key; });
    return it != entries.end() && it->spellId == spellId ? &*it : nullptr;
}
}
const LocalRecipe* LocalWorldContent::recipe(uint32_t spellId) const { return findRecipe(recipes, spellId); }
const LocalQuestDefinition* LocalWorldContent::quest(uint32_t id) const {
    if (const auto* d = definition(quests, id)) return d;
    const auto found = questCache.find(id); if (found != questCache.end()) return &found->second;
    if (!catalog || questCache.size() >= 16384) return nullptr;
    LocalQuestDefinition d; if (!catalog->quest(id, d, catalogError)) return nullptr;
    return &questCache.emplace(id, std::move(d)).first->second;
}
const LocalNpcDefinition* LocalWorldContent::npc(uint32_t id) const {
    if (const auto* d = definition(npcs, id)) return d;
    const auto found = npcCache.find(id); if (found != npcCache.end()) return &found->second;
    if (!catalog || npcCache.size() >= 16384) return nullptr;
    LocalNpcDefinition d; if (!catalog->npc(id, d, catalogError)) return nullptr;
    return &npcCache.emplace(id, std::move(d)).first->second;
}
std::vector<LocalQuestDefinition> LocalWorldContent::questsForNpc(uint32_t entry) const {
    std::vector<LocalQuestDefinition> result;
    for (const auto& q : quests) if (q.giverEntry == entry || q.turnInEntry == entry) result.push_back(q);
    if (catalog) {
        auto found=npcQuestCache.find(entry);
        if (found==npcQuestCache.end()) {
            std::vector<LocalQuestDefinition> extra;
            if (!catalog->questsForNpc(entry,extra,catalogError)) return result;
            std::vector<uint32_t> ids;ids.reserve(extra.size());
            for (auto& q:extra) {
                ids.push_back(q.id);
                if (questCache.size()<16384) questCache.emplace(q.id,std::move(q));
            }
            if (npcQuestCache.size()>=512) npcQuestCache.clear();
            found=npcQuestCache.emplace(entry,std::move(ids)).first;
        }
        for (const auto id:found->second) if (const auto* q=quest(id)) {
            if (std::none_of(result.begin(),result.end(),[&](const auto& d){return d.id==id;}))
                result.push_back(*q);
        }
    }
    return result;
}

struct LocalGameplay::Impl {
    LocalCombatHistory combatHistory;
    /// P05 line of sight . Empty unless the player installed a pack.
    LocalCollisionData collision;
    std::function<std::vector<LocalRealmPlayer*>()> auraOwnerProvider;
    std::vector<LocalRealmPlayer*> auraOwners(const std::vector<LocalRealmPlayer*>& active) {
        auto result=active;
        if(auraOwnerProvider)for(auto* owner:auraOwnerProvider())if(owner&&
            std::none_of(result.begin(),result.end(),[&](auto* p){return p&&p->guid==owner->guid;}))result.push_back(owner);
        return result;
    }
    std::mt19937 meleeRandom{uint32_t(std::chrono::steady_clock::now().time_since_epoch().count())};
    uint32_t meleeRoll(uint32_t maximum=9999){return std::uniform_int_distribution<uint32_t>(0,maximum)(meleeRandom);}
    uint32_t stormstrikeEventOffsetMs=0; // Sub-frame periodic hit time; inherited by nested damage.
    uint64_t procRandomState=0x9e3779b97f4a7c15ULL;
    // One authority event owns a bounded synchronous chain. An aura may fire
    // once per root event, preventing cycles even when triggered admission is explicit.
    static constexpr uint8_t MaxProcDepth=4;
    static constexpr size_t MaxProcEffects=32;
    uint8_t procDispatchDepth=0;
    size_t procEffects=0;
    std::array<std::pair<uint64_t,uint32_t>,MaxProcEffects> procFired{};
    LocalCombatEvent procParent{};
    uint64_t procAuraOwner=0,procAuraCaster=0;
    uint64_t pendingStatAuraOwner=0; // One cast reserves its new buff slot before callbacks.
    std::shared_ptr<LocalWorldContent> content = std::make_shared<LocalWorldContent>();
    std::vector<LocalRealmNpc> npcs;
    // Owned creatures live outside the catalog-driven NPC roster: the spawn
    // refresher rebuilds that list from world queries and would evict a summon.
    std::vector<LocalRealmPet> pets;
    uint64_t nextPetSerial=0,nextSummonEpoch=1;
    // Area-aura reconciliation. The reference reconciles on every aura update;
    // this ruleset recomputes on a fixed interval and on every emitter change,
    // which is a cadence choice, not a different rule.
    uint64_t nextAreaAuraGeneration=1;
    float areaAuraTimer=0;
    LocalVendorInventory vendorInventory;
    // AuraEffect::CalculatePeriodicData snapshots the periodic critical chance at
    // application; it is never recomputed per tick.
    // No Aura::m_casterLevel snapshot: the periodic resist roll passes it
    // (SpellAuraEffects.cpp:6400) but GetEffectiveResistChance reads it only
    // when the caster is gone (`owner ? owner->GetLevel() : casterLevel`,
    // Unit.cpp:2307), and this sweep drops an aura whose owner is absent
    // before any tick, so the owner's current level is the only level a
    // local tick can ever read.
    struct PeriodicDamage { uint64_t owner=0,target=0;uint32_t spell=0,remaining=0,next=0,interval=0,damage=0,mapId=0,instanceId=0;uint8_t stacks=1;uint64_t targetEpoch=0;uint16_t critChanceBasisPoints=0; };
    std::vector<PeriodicDamage> periodicDamage;
    struct PendingIgnite {uint64_t owner=0,target=0,epoch=0;uint32_t map=0,instance=0,delay=400,damage=0,parent=0;uint64_t sourceSequence=0,rootSequence=0;uint8_t depth=0;};
    std::vector<PendingIgnite> pendingIgnites;
    struct PeriodicHeal {
        uint64_t owner=0,target=0;
        uint32_t spell=0,remaining=0,next=0,interval=0,amount=0,mapId=0,instanceId=0;
        uint8_t stacks=1;
        uint16_t critChanceBasisPoints=0;
    };
    std::vector<PeriodicHeal> periodicHeals;
    void refreshHealingViews(const std::vector<LocalRealmPlayer*>& players){
        for(auto& target:npcs)target.damageAuras.clear();
        for(const auto& a:periodicDamage) {
            auto* target=npc(a.target);auto* owner=player(a.owner,players);
            if(!target||target->dead||!target->health||!a.remaining||target->combatEpoch!=a.targetEpoch||target->mapId!=a.mapId||target->instanceId!=a.instanceId||
               (a.spell!=12654&&(!owner||owner->dead||owner->mapId!=a.mapId||owner->instanceId!=a.instanceId)))continue;
            const auto* d=content->spell(a.spell);if(!d||target->damageAuras.size()>=kLocalMaxNpcDamageAuras)continue;
            target->damageAuras.push_back({a.spell,a.remaining,d->durationMs,a.owner,a.stacks});
        }
        for(auto* p:players)if(p)p->healingAuras.clear();
        for(const auto& a:periodicHeals){
            auto* target=player(a.target,players);auto* owner=player(a.owner,players);
            if(!target||!owner||target->dead||owner->dead||!target->health||!a.remaining||
               target->mapId!=a.mapId||owner->mapId!=a.mapId||target->instanceId!=a.instanceId||owner->instanceId!=a.instanceId)continue;
            const auto* d=content->spell(a.spell);if(!d||target->healingAuras.size()>=kLocalMaxHealingAuraViews)continue;
            target->healingAuras.push_back({a.spell,a.remaining,d->durationMs,a.owner,a.stacks});
        }
    }
    std::unordered_map<std::string,std::vector<size_t>> grid;
    std::unordered_map<uint64_t,double> respawnAt;
    std::vector<LocalAreaTriggerVolume> volumes;
    std::vector<LocalFactionTemplate> factions;
    std::vector<LocalGraveyardSite> graveyards;
    std::array<uint32_t, 12> raceFactions{};
    std::vector<LocalInstanceState> instances;
    uint32_t nextInstanceId = 1;
    std::vector<std::pair<uint64_t,uint32_t>> partyMembership;
    // Session party IDs may restart at one. Allocate a durable owner key from
    // the globally unique instance ID instead, outside the human GUID range.
    std::map<uint32_t,uint64_t> partyInstanceOwners;
    std::map<uint32_t,uint64_t> lastPartyLooter;
    uint32_t partyOf(uint64_t guid) const {
        auto i=std::lower_bound(partyMembership.begin(),partyMembership.end(),std::pair<uint64_t,uint32_t>{guid,0});
        return i!=partyMembership.end() && i->first==guid ? i->second : 0;
    }
    // Client Map.dbc rows, sorted by id, and the client's SkillLine rows once
    // the application has read them. Both are empty until then and everything
    // that reads them copes: without Map.dbc the catalog's own instance flag
    // decides, and without SkillLine.dbc the built-in professions stand.
    std::vector<LocalMapDefinition> maps;
    std::vector<LocalSkillLine> skills;
    // Taxi routes and world transports, from the player's own client DBCs.
    // Empty until the application supplies them, and everything that reads it
    // copes with that: with no client data there are no flights and no ships,
    // rather than invented ones.
    LocalTravelNetwork travel;
    std::vector<LocalTransportState> transports;
    std::map<uint32_t, std::vector<LocalNpcSpawn>> crewSpawns;
    double now=0, transportClock=0;
    double combatMillisecondRemainder=0;
    // Authority millisecond clock, the local stand-in for GameTime::GetGameTimeMS.
    // Advanced once per tick by the same elapsedMs every other timer uses, so a
    // diminishing record's removal stamp and its window are measured on one
    // scale. Never persisted and never replicated.
    // Starts at 1, not 0. The reference's GameTime::GetGameTimeMS is never zero,
    // which makes Unit.cpp:11302's `if (!i->hitTime) return LEVEL_1` dead code
    // there; with a clock that starts at zero a record stamped in the first
    // frame reads level 1 forever and its ladder never applies.
    uint64_t authorityClockMs=1;
    uint64_t nextNpcEpoch=0;
    uint64_t allocateNpcEpoch(){if(!++nextNpcEpoch)++nextNpcEpoch;return nextNpcEpoch;}
    float regionTimer=1;
    // Said once per realm, the first time anything asks what portals exist. See
    // the scan in LocalGameplay::portals().
    bool portalScanLogged=false;
    mutable std::unordered_map<uint64_t,uint32_t> portalExitLatch;
    void rebuild(bool replaceContent = false) {
        grid.clear();npcs.clear();crewSpawns.clear();respawnAt.clear();periodicDamage.clear();pendingIgnites.clear();periodicHeals.clear();regionTimer=1;
        // Travel data can arrive after a realm has loaded its stock ledger.
        // Reclassifying actors must not refill shops; only new content does.
        if (replaceContent) vendorInventory.clear();
        for(size_t i=0;i<content->spawns.size();++i) {
            const auto& s=content->spawns[i];grid[cell(s.mapId,int(std::floor(s.x/CellSize)),int(std::floor(s.y/CellSize)))].push_back(i);
        }
    }
    LocalRealmNpc makeNpc(const LocalNpcSpawn& s, uint32_t instanceId) {
        const auto& d=*content->npc(s.entry);LocalRealmNpc n;
        n.guid=NpcPrefix|(uint64_t(instanceId)<<32)|s.id;n.instanceId=instanceId;n.spawnId=s.id;n.entry=s.entry;n.displayId=d.displayId;n.name=d.name;
        n.mapId=s.mapId;n.x=n.homeX=s.x;n.y=n.homeY=s.y;n.z=n.homeZ=s.z;n.orientation=s.orientation;
        n.level=d.level;n.health=n.maxHealth=d.health;n.hostile=d.hostile;n.questGiver=d.questGiver;
        // What does this NPC do for a living?
        //
        // creature_template.npcflag answers it, and localEffectiveNpcFlags is
        // where the two sources of that field are reconciled: the catalog's own
        // value when it carries one, and otherwise the transcription of the
        // same upstream column that ships beside this code. Everything below
        // reads the reconciled value, so a re-imported catalog changes every
        // service at once rather than half of them.
        const uint32_t flags = localEffectiveNpcFlags(d);
        n.banker = (flags & kLocalNpcFlagBanker) != 0;
        // Is this NPC a flight master, and for which node?
        //
        // Two sources, in order of authority. The npcflag is the server's own
        // answer; for an NPC no source has flags for, one standing on a taxi
        // node is taken to serve it. A taxi node is a physical location and the
        // flight master is the person at it - that is how the two data sets
        // relate, not a guess about names.
        //
        // Either way the node has to exist in the client's data and be a flight
        // node: a boat stop has no flight master, and offering one there would
        // sell a flight with no route behind it.
        if (const LocalTaxiNode* node = travel.flightNodeAt(n.mapId, n.x, n.y, n.z)) {
            if ((flags & LocalTravelNetwork::NpcFlagFlightMaster) != 0 || flags == 0) {
                n.flightMaster = true;
                n.taxiNodeId = node->id;
            }
        }
        // Auctioneers have no client-side data behind them the way flight
        // masters have taxi nodes, so there is only the npcflag. When no source
        // has flags at all the fallback is the subname the server authored,
        // which for every auction house NPC is the word this matches. Nothing
        // is inferred from position - an auction house is a building, not a
        // coordinate the client knows about.
        n.auctioneer = (flags & kLocalNpcFlagAuctioneer) != 0 ||
                       (flags == 0 && d.subname.rfind("Auctioneer", 0) == 0);
        n.vendor = (flags & kLocalNpcFlagAnyVendor) != 0;
        n.vendorCategories = localVendorCategories(flags);
        n.repairer = (flags & kLocalNpcFlagRepair) != 0;
        n.classTrainer = (flags & kLocalNpcFlagTrainerClass) != 0;
        n.professionTrainer = (flags & kLocalNpcFlagTrainerProfession) != 0;
        n.innkeeper = (flags & kLocalNpcFlagInnkeeper) != 0;
        // What a trainer teaches is only in its subname, so unless the catalog
        // states it outright it comes from the transcription that resolved it
        // there. Neither source knowing leaves these zero, and such a trainer
        // teaches nothing rather than teaching the wrong thing.
        const auto* row = localServiceNpcRecord(d.id);
        if (n.professionTrainer) n.trainerSkill = d.trainerSkill ? d.trainerSkill : (row ? row->trainerSkill : 0);
        if (n.classTrainer) n.trainerClass = d.trainerClass ? d.trainerClass : (row ? row->trainerClass : 0);
        n.combatEpoch=allocateNpcEpoch();
        return n;
    }
    void regions(const std::vector<LocalRealmPlayer*>& players) {
        npcs.erase(std::remove_if(npcs.begin(), npcs.end(), [&](const LocalRealmNpc& n) {
            if (n.targetGuid) return false;
            for (const auto* p : players) {
                // The hull crosses the seam before player relocation below.
                // Retain its passengers through that one authority tick.
                if (n.transportEntry && p->transportEntry == n.transportEntry && !p->instanceId)
                    return false;
                if (distance2(*p, n) <= RetainRadius * RetainRadius) return false;
            }
            return true;
        }), npcs.end());
        struct Candidate {
            LocalNpcSpawn spawn; uint32_t instanceId = 0; float distance = 0;
            uint32_t transportEntry = 0;
            LocalNpcSpawn local;
        };
        std::map<uint64_t, Candidate> byGuid;
        for (const auto* p : players) {
            std::vector<LocalNpcSpawn> local;
            if (content->catalog) {
                content->catalog->query3D(p->mapId, p->x, p->y, p->z, ActiveRadius,
                                        LocalGameplay::MaxNpcs, local, content->catalogError);
            } else {
                const int cx = int(std::floor(p->x / CellSize)), cy = int(std::floor(p->y / CellSize));
                for (int y = cy - 1; y <= cy + 1; ++y) for (int x = cx - 1; x <= cx + 1; ++x) {
                    const auto group = grid.find(cell(p->mapId, x, y)); if (group == grid.end()) continue;
                    for (auto index : group->second) local.push_back(content->spawns[index]);
                }
            }
            for (const auto& spawn : local) {
                const float d = distance2(p->x, p->y, p->z, spawn.x, spawn.y, spawn.z);
                if (d > ActiveRadius * ActiveRadius) continue;
                const auto* def = content->npc(spawn.entry); if (!def) continue;
                const auto guid = NpcPrefix | (uint64_t(p->instanceId) << 32) | spawn.id;
                const float priority = d - (def->questGiver ? ActiveRadius * ActiveRadius : 0);
                const auto inserted = byGuid.emplace(guid, Candidate{spawn, p->instanceId, priority});
                if (!inserted.second) inserted.first->second.distance = std::min(inserted.first->second.distance, priority);
            }
        }
        // Static passengers are authored in the hull's map, not in Azeroth or
        // Kalimdor. Query each compact crew once and transform it with that hull.
        for (const auto& hull : transports) {
            bool observed = false;
            for (const auto* p : players) {
                if (p->instanceId || (p->mapId != hull.mapId && p->transportEntry != hull.entry)) continue;
                if (p->transportEntry == hull.entry ||
                    distance2(p->x,p->y,p->z,hull.x,hull.y,hull.z) <= 350.0f*350.0f) {
                    observed = true; break;
                }
            }
            if (!observed) continue;
            const auto routeIt=std::find_if(travel.transportRoutes().begin(),travel.transportRoutes().end(),
                [&](const auto& route){return route.entry==hull.entry;});
            const auto* route=routeIt!=travel.transportRoutes().end()?&*routeIt:nullptr;
            if (!route || !route->crewMapId) continue;
            auto found = crewSpawns.find(route->crewMapId);
            if (found == crewSpawns.end()) {
                std::vector<LocalNpcSpawn> rows;
                if (content->catalog) {
                    if (!content->catalog->query3D(route->crewMapId,0,0,0,128,32,
                                                   rows,content->catalogError)) continue;
                } else {
                    for (const auto& spawn : content->spawns)
                        if (spawn.mapId == route->crewMapId && rows.size()<32) rows.push_back(spawn);
                }
                found=crewSpawns.emplace(route->crewMapId,std::move(rows)).first;
                LOG_INFO("[TRANSPORT_CREW] entry=", hull.entry, " crewMap=", route->crewMapId,
                         " catalogPassengers=", found->second.size());
            }
            for (const auto& row : found->second) {
                if (!content->npc(row.entry)) continue;
                const auto pose=localTransportPassengerPose(hull,row.x,row.y,row.z,row.orientation);
                auto spawn=row;spawn.mapId=hull.mapId;spawn.x=pose.x;spawn.y=pose.y;
                spawn.z=pose.z;spawn.orientation=pose.orientation;
                const auto guid=NpcPrefix|row.id;
                byGuid[guid]=Candidate{spawn,0,-10000000.0f,hull.entry,row};
            }
        }
        // Each independently simulated map/instance receives a fair share of
        // the global actor budget. Global GUID sorting alone can otherwise give
        // all 128 equal-distance slots to the first instance and hide the rest.
        std::set<uint64_t> activeSpaces;
        for (const auto* p : players) activeSpaces.insert((uint64_t(p->mapId) << 32) | p->instanceId);
        for (const auto& n : npcs) if (!n.transportEntry && n.targetGuid && activeSpaces.count((uint64_t(n.mapId) << 32) | n.instanceId)) {
            LocalNpcSpawn spawn; spawn.id=n.spawnId;spawn.entry=n.entry;spawn.mapId=n.mapId;
            spawn.x=n.homeX;spawn.y=n.homeY;spawn.z=n.homeZ;spawn.orientation=n.orientation;
            auto entry=byGuid.emplace(n.guid, Candidate{spawn,n.instanceId,-1000000000.0f});
            entry.first->second.distance=-1000000000.0f;
        }
        std::map<uint64_t, std::vector<std::pair<float, uint64_t>>> spaces;
        std::vector<std::pair<float, uint64_t>> candidates;
        for (const auto& entry : byGuid) {
            const auto guid=entry.first; const auto death=respawnAt.find(guid);
            const bool present=std::any_of(npcs.begin(),npcs.end(),[&](const auto& n){return n.guid==guid;});
            if (!present && death!=respawnAt.end() && death->second>now) continue;
            const auto ranked=std::make_pair(entry.second.distance,guid);
            spaces[(uint64_t(entry.second.spawn.mapId)<<32)|entry.second.instanceId].push_back(ranked);
            candidates.push_back(ranked);
        }
        for (auto& space : spaces) std::sort(space.second.begin(),space.second.end());
        std::sort(candidates.begin(),candidates.end());
        std::set<uint64_t> selected;
        for (size_t round=0;selected.size()<LocalGameplay::MaxNpcs;++round) {
            bool added=false;
            for (const auto& space : spaces) {
                if (selected.size()>=LocalGameplay::MaxNpcs) break;
                if (round<space.second.size()) {selected.insert(space.second[round].second);added=true;}
            }
            if (!added) break;
        }
        npcs.erase(std::remove_if(npcs.begin(), npcs.end(), [&](const LocalRealmNpc& n) {
            return !selected.count(n.guid);
        }), npcs.end());
        for (const auto& candidate : candidates) {
            if (npcs.size() >= LocalGameplay::MaxNpcs) break;
            const auto guid = candidate.second;
            if (!selected.count(guid)) continue;
            if (std::any_of(npcs.begin(), npcs.end(), [&](const LocalRealmNpc& n) { return n.guid == guid; })) continue;
            const auto death = respawnAt.find(guid); if (death != respawnAt.end() && death->second > now) continue;
            const auto& entry = byGuid.at(guid);
            auto npc=makeNpc(entry.spawn, entry.instanceId);
            npc.transportEntry=entry.transportEntry;
            if (entry.transportEntry) {
                npc.transportX=entry.local.x;npc.transportY=entry.local.y;npc.transportZ=entry.local.z;
                npc.transportOrientation=entry.local.orientation;
            }
            npcs.push_back(std::move(npc));
        }
        for (auto it = respawnAt.begin(); it != respawnAt.end();) if (it->second <= now) it = respawnAt.erase(it); else ++it;
    }
    LocalRealmNpc* npc(uint64_t guid){for(auto& n:npcs)if(n.guid==guid)return &n;return nullptr;}
    LocalRealmPlayer* player(uint64_t guid,const std::vector<LocalRealmPlayer*>& players){for(auto* p:players)if(p->guid==guid)return p;return nullptr;}
    LocalRealmPet* pet(uint64_t guid){for(auto& v:pets)if(v.guid==guid)return &v;return nullptr;}
    LocalRealmPet* controlledPetOf(uint64_t ownerGuid){
        for(auto& v:pets)if(v.ownerGuid==ownerGuid&&v.kind==LocalPetKind::Controlled)return &v;
        return nullptr;
    }
    uint64_t allocateSummonEpoch(){if(!++nextSummonEpoch)++nextSummonEpoch;return nextSummonEpoch;}
    uint64_t allocateAreaAuraGeneration(){if(!++nextAreaAuraGeneration)++nextAreaAuraGeneration;return nextAreaAuraGeneration;}
    static float playerDistance2(const LocalRealmPlayer& a,const LocalRealmPlayer& b) {
        return a.mapId==b.mapId&&a.instanceId==b.instanceId?distance2(a.x,a.y,a.z,b.x,b.y,b.z)
                                                           :std::numeric_limits<float>::max();
    }
    /// Aura::UpdateTargetMap, in this ruleset's bounded form: retire emitters
    /// that no longer qualify, then rebuild every recipient's derived
    /// applications from scratch and resolve dominance per exclusivity group.
    /// Derived applications are authority state only; nothing here is saved and
    /// nothing is accepted from a client.
    void reconcileAreaAuras(const std::vector<LocalRealmPlayer*>& players) {
        for(auto* owner:players) {
            if(!owner)continue;
            std::erase_if(owner->areaEmitters,[&](const LocalAreaAuraEmitter& e) {
                const auto* d=content->spell(e.spellId);
                return !d||!d->areaAuraProfile||!d->unsupportedReason.empty()||!validLocalAreaAuraEmitter(e)||
                       owner->dead||!owner->health||owner->mapId!=e.mapId||owner->instanceId!=e.instanceId||
                       std::find(owner->knownSpells.begin(),owner->knownSpells.end(),e.spellId)==owner->knownSpells.end();
            });
        }
        for(auto* recipient:players) {
            if(!recipient)continue;
            std::array<LocalAreaAuraApplication,kLocalMaxAreaAuraApplications> next{};size_t count=0;
            if(!recipient->dead&&recipient->health)for(auto* owner:players) {
                if(!owner)continue;
                // UnitAura::FillTargetMap applies the aura to its own caster
                // unconditionally; every other recipient must qualify.
                const bool self=owner->guid==recipient->guid;
                for(const auto& emitter:owner->areaEmitters) {
                    const auto* d=content->spell(emitter.spellId);
                    if(!d||!d->areaAuraProfile)continue;
                    if(!self) {
                        if(owner->mapId!=recipient->mapId||owner->instanceId!=recipient->instanceId)continue;
                        if(recipient->flight.active||owner->flight.active)continue;
                        const auto distance=playerDistance2(*owner,*recipient);
                        if(!std::isfinite(distance)||distance>d->areaAuraRadius*d->areaAuraRadius)continue;
                        // Membership is this build's party. There is no raid
                        // roster and none is implied by this predicate.
                        const auto group=partyOf(owner->guid);
                        if(!group||partyOf(recipient->guid)!=group)continue;
                    }
                    if(count>=kLocalMaxAreaAuraApplications)continue;
                    next[count++]={emitter.spellId,recipient->mapId,recipient->instanceId,owner->guid,
                                   emitter.generation,emitter.amount,emitter.effectMask,true};
                }
            }
            // IsPaladinAuraDominant: sources of one exclusivity group coexist on
            // a recipient and only the strongest keeps its effects. The loser's
            // application is kept, effect-free, rather than removed.
            for(size_t i=0;i<count;++i) {
                const auto* di=content->spell(next[i].spellId);
                if(!di){next[i].effective=false;continue;}
                const auto group=localAreaAuraGroup(*di);
                for(size_t j=0;j<count&&next[i].effective;++j) {
                    if(i==j)continue;
                    const auto* dj=content->spell(next[j].spellId);
                    if(!dj||!(localAreaAuraGroup(*dj)==group))continue;
                    if(localAreaAuraDominates(next[j],next[i],recipient->guid))next[i].effective=false;
                }
            }
            recipient->areaAuras.assign(next.begin(),next.begin()+count);
        }
    }
    /// Unit::DealDamageShieldDamage. Not a proc: aura 15's handler is
    /// HandleNoImmediateEffect and the retaliation runs from the melee path.
    /// The RECIPIENT is credited as the damage source (Unit.cpp:2181).
    void dealAreaAuraShieldDamage(LocalRealmPlayer& recipient,LocalRealmNpc& attacker,
                                  const std::vector<LocalRealmPlayer*>& players) {
        if(recipient.dead||attacker.dead)return;
        uint32_t best=0,spellId=0;
        for(const auto& a:recipient.areaAuras) {
            if(!a.effective||!(a.effectMask&1)||a.mapId!=recipient.mapId||a.instanceId!=recipient.instanceId)continue;
            if(a.amount>best){best=a.amount;spellId=a.spellId;}
        }
        if(!best||!spellId)return;
        const auto* d=content->spell(spellId);if(!d)return;
        damageNpc(attacker,recipient,best,players,(d->schoolMask&1)!=0,spellId,false,spellId,d,
                  LocalMeleeOutcome::Hit);
    }
    void addThreat(LocalRealmNpc& n,uint64_t guid,uint64_t amount) {
        if(!guid||!amount||n.dead||n.transportEntry)return;
        LocalNpcThreat* empty=nullptr;
        for(auto& entry:n.threat) {
            if(entry.guid==guid){entry.amount=std::min(uint64_t(1000000000000ULL),entry.amount+amount);return;}
            if(!entry.guid&&!empty)empty=&entry;
        }
        if(empty)*empty={guid,std::min(uint64_t(1000000000000ULL),amount)};
    }
    // ThreatManager::ForwardThreatForAssistingMe (ThreatManager.cpp:764-790):
    // a positive act on `recipient` - a heal, or the initial threat of a
    // positive cast - is split evenly over every creature threatened by the
    // recipient that is not UNIT_STATE_CONTROLLED; a controlled creature gets
    // a zero entry, which here is the entry it already holds. `budget` is in
    // thousandths and already carries the caster's modifiers (AddThreat's
    // ignoreModifiers is false on this path); the remainder goes to the lowest
    // guids so two peers split it identically.
    void forwardThreatForAssisting(uint64_t recipient,uint64_t assistant,uint64_t budget,uint32_t mapId,uint32_t instanceId,
                                   const std::vector<LocalRealmPlayer*>& players) {
        if(!recipient||!assistant||!budget)return;
        std::array<LocalRealmNpc*,LocalGameplay::MaxNpcs> engaged{};size_t count=0;
        for(auto& n:npcs) {
            if(n.dead||n.transportEntry||n.mapId!=mapId||n.instanceId!=instanceId)continue;
            if(n.targetGuid!=recipient&&!std::any_of(n.threat.begin(),n.threat.end(),[&](const auto& e){return e.guid==recipient&&e.amount;}))continue;
            // UNIT_STATE_CONTROLLED is confused | stunned | fleeing
            // (UnitDefines.h:218); a stun is the only one of the three this
            // realm's creatures can carry (LocalNpcControlKind::Stun).
            if(std::any_of(n.controls.begin(),n.controls.end(),[](const auto& a){return a.remainingMs&&a.kind==uint8_t(LocalNpcControlKind::Stun);}))continue;
            if(count<engaged.size())engaged[count++]=&n;
        }
        if(!count)return;
        std::sort(engaged.begin(),engaged.begin()+count,[](auto* a,auto* b){return a->guid<b->guid;});
        for(size_t i=0;i<count;++i){addThreat(*engaged[i],assistant,budget/count+(i<budget%count));selectThreatTarget(*engaged[i],players);}
    }
    // An owned creature holds its own threat: the source credits the summon
    // that dealt the damage, not the player it belongs to.
    // `combatReach` is the contender's own UNIT_FIELD_COMBATREACH, which the
    // melee half of ThreatManager::SelectVictim reads through IsWithinMeleeRange.
    struct ThreatActor { float x=0,y=0,z=0; bool valid=false; float combatReach=kLocalDefaultCombatReach; };
    ThreatActor threatActor(uint64_t guid,const LocalRealmNpc& n,const std::vector<LocalRealmPlayer*>& players) {
        if(localPetGuid(guid)) {
            auto* summon=pet(guid);
            if(!summon||summon->dead||!summon->health||summon->mapId!=n.mapId||summon->instanceId!=n.instanceId)return {};
            return {summon->x,summon->y,summon->z,true,localCreatureCombatReach(content->npc(summon->entry))};
        }
        auto* p=player(guid,players);
        if(!p||p->dead||!p->health||p->flight.active||p->transportEntry||p->mapId!=n.mapId||p->instanceId!=n.instanceId)return {};
        return {p->x,p->y,p->z,true,kLocalDefaultCombatReach};
    }
    void selectThreatTarget(LocalRealmNpc& n,const std::vector<LocalRealmPlayer*>& players) {
        uint64_t current=0;
        for(auto& entry:n.threat)if(entry.guid) {
            const auto actor=threatActor(entry.guid,n,players);
            const auto distance=actor.valid?distance2(actor.x,actor.y,actor.z,n.x,n.y,n.z):0;
            if(!actor.valid||!std::isfinite(distance)||distance>70*70){entry={};continue;}
            if(entry.guid==n.targetGuid)current=entry.amount;
        }
        uint64_t bestGuid=current?n.targetGuid:0,bestAmount=current;
        for(const auto& entry:n.threat)if(entry.guid&&entry.guid!=n.targetGuid) {
            const auto actor=threatActor(entry.guid,n,players);if(!actor.valid)continue;
            // ThreatManager::SelectVictim, ThreatManager.cpp:656-679: the 110 %
            // threshold applies to a contender the owner IsWithinMeleeRange of,
            // which is reach-aware (Unit.cpp:782-803); this was a fixed 4.5 yd.
            const uint64_t threshold=localWithinMeleeRange(float(distance2(actor.x,actor.y,actor.z,n.x,n.y,n.z)),
                localCreatureCombatReach(content->npc(n.entry)),actor.combatReach)?110:130;
            if((!current||entry.amount*100>current*threshold)&&
               (entry.amount>bestAmount||(entry.amount==bestAmount&&(!bestGuid||entry.guid<bestGuid)))) {
                bestGuid=entry.guid;bestAmount=entry.amount;
            }
        }
        n.targetGuid=bestGuid;
    }
    // `killer` is the player the source credits with the reward - for an owned
    // creature that is Unit::GetCharmerOrOwnerPlayerOrPlayerItself, not the
    // actor. `actorGuid`/`actorLevel` keep the real killing blow's identity so
    // the KILL observation is not silently reattributed to the owner.
    void kill(LocalRealmNpc& n,LocalRealmPlayer& killer,const std::vector<LocalRealmPlayer*>& players,
              uint64_t actorGuid=0,uint8_t actorLevel=0) {
        const auto* def=content->npc(n.entry);if(!def||n.dead)return;
        localResetNpcSpellState(n);n.health=0;n.dead=true;n.combatEpoch=allocateNpcEpoch();n.targetGuid=0;n.threat={};n.snares.clear();releaseNpcControls(n);n.diminishing.clear();n.damageAuras.clear();n.stormstrikeAuras.clear();n.lootable=!def->loot.empty()||def->money;
        if(!n.lootOwner)n.lootOwner=killer.guid;
        n.respawnTimer=def->respawnSeconds;respawnAt[n.guid]=now+def->respawnSeconds;
        // The first engager owns the kill, even if an unrelated player lands
        // the final blow. Only their actual party can share supported rewards.
        const auto* owner=player(n.lootOwner,players);
        const uint32_t group=owner?partyOf(owner->guid):0;
        std::vector<LocalRealmPlayer*> eligible;
        for(auto& summon:pets)if(summon.targetGuid==n.guid)summon.targetGuid=0;
        for(auto* p:players) {
            if(p->attackTarget==n.guid)p->attackTarget=0;
            if(p->rangedTarget==n.guid)localStopRangedAuto(*p);
            if(p->comboTarget==n.guid)clearLocalCombo(*p);
            if(owner && !p->dead && p->health && distance2(*p,n)<=60*60 &&
               (p->guid==owner->guid || (group && partyOf(p->guid)==group)))eligible.push_back(p);
        }
        std::sort(eligible.begin(),eligible.end(),[](auto* a,auto* b){return a->guid<b->guid;});
        eligible.erase(std::unique(eligible.begin(),eligible.end(),[](auto* a,auto* b){return a->guid==b->guid;}),eligible.end());
        // This local ruleset splits the existing catalog XP budget equally;
        // deterministic remainder allocation conserves it exactly. It does not
        // claim retail level scaling, gray-mob penalties or group multipliers.
        const size_t xpCount=std::count_if(eligible.begin(),eligible.end(),[](auto* p){return p->level<80;});
        uint32_t remainder=xpCount?def->xp%uint32_t(xpCount):0;
        for(auto* p:eligible) {
            uint32_t reward=0;
            if(p->level<80 && xpCount){reward=def->xp/uint32_t(xpCount);if(remainder){++reward;--remainder;}}
            if(reward)experience(*p,*content,reward);
            objectiveCredit(*p,*content,LocalQuestObjective::Type::Kill,n.entry);
            LOG_INFO("[LOCAL_GROUP_REWARD] npc=",n.guid," tag=",owner->guid," party=",group,
                " player=",p->guid," xp=",reward," eligible=",eligible.size());
        }
        n.lootCandidates.fill(0);
        if(n.lootable && group && !eligible.empty()) {
            const uint64_t last=lastPartyLooter[group];
            auto chosen=std::find_if(eligible.begin(),eligible.end(),[&](auto* p){return p->guid>last;});
            if(chosen==eligible.end())chosen=eligible.begin();
            n.lootOwner=(*chosen)->guid;lastPartyLooter[group]=n.lootOwner;
            for(size_t i=0;i<std::min(eligible.size(),n.lootCandidates.size());++i)n.lootCandidates[i]=eligible[i]->guid;
            LOG_INFO("[LOCAL_GROUP_LOOT] npc=",n.guid," party=",group," owner=",n.lootOwner," mode=roundrobin");
        }
        // Source ordering: finish reward distribution, then killing-blow
        // owner KILL/victim KILLED, then victim-only DEATH. Party and loot
        // ownership do not substitute for the actor who dealt the final hit.
        const auto blowGuid=actorGuid?actorGuid:killer.guid;
        const auto blowLevel=actorGuid?actorLevel:killer.level;
        emitCombatEvent(localKillProcEvent(blowGuid,n.guid,n.mapId,n.instanceId,true,blowLevel,
            localNpcExperienceTargetEligible(n.entry,killer.level,n.level)),players);
        emitCombatEvent(localDeathProcEvent(n.guid,n.mapId,n.instanceId,false,n.level),players);
    }
    bool rollSpellCritical(const LocalRealmPlayer& caster,const LocalSpellDefinition& d,const LocalRealmNpc* target=nullptr) {
        if(!localDirectMagicCritEligible(d))return false;
        if(target) {
            // Source overrides return before ordinary target modifiers. They
            // neither consume Flame Shock nor depend on the target's level.
            if(d.spellFamily==10&&d.cooldownCategory==19&&localNpcIsDemonOrUndead(target->entry))return true;
            if(d.spellFamily==11&&(d.spellFamilyFlags[1]&0x1000))
                for(const auto& aura:periodicDamage) {
                    const auto* dot=content->spell(aura.spell);
                    if(aura.owner==caster.guid&&aura.target==target->guid&&aura.targetEpoch==target->combatEpoch&&
                       aura.mapId==target->mapId&&aura.instanceId==target->instanceId&&aura.remaining&&dot&&
                       dot->spellFamily==11&&(dot->spellFamilyFlags[0]&0x10000000))return true;
                }
        }
        const float chance=localSpellCritChance(caster,*content,d.schoolMask);
        return std::isfinite(chance)&&chance>0&&meleeRoll()<std::clamp(chance,0.f,100.f)*100;
    }
    // Unit::DealDamage break-on-damage, rule 1. Binary: no threshold and no
    // chance, one point is enough, and the reference evaluates it before its own
    // zero-damage early-out so fully absorbed damage still breaks the control.
    // An outcome that nullified the damage never reached DealDamage at all and
    // therefore breaks nothing. `except` is the damaging spell's own id, so a
    // spell that both controls and damages does not break itself.
    /// Every control leaves the creature, each lowering its own diminishing
    /// stack, exactly as Unit::RemoveAllAuras does. The records themselves stay:
    /// the reference clears them only in Unit::setDeathState (Unit.cpp:11093),
    /// which is ClearDiminishings' one and only caller, so a creature that
    /// leashed and re-engaged inside fifteen seconds still owes its ladder.
    void releaseNpcControls(LocalRealmNpc& n) {
        for(const auto& a:n.controls)
            if(const auto* d=content->spell(a.spellId))
                localDiminishingApply(n,localDiminishingGroupForSpell(*d,false),false,authorityClockMs);
        n.controls.clear();
    }
    void breakNpcControlsOnDamage(LocalRealmNpc& n,uint32_t exceptSpell,const LocalSpellDefinition* source) {
        if(n.controls.empty()||(source&&source->sourceDamageDoesNotBreakAuras))return;
        const auto before=n.controls.size();
        std::erase_if(n.controls,[&](const auto& a){
            const auto* d=content->spell(a.spellId);
            const bool broken=d&&(d->auraInterruptFlags&kLocalAuraInterruptTakeDamage)&&
                              (!exceptSpell||a.spellId!=exceptSpell);
            // Unit::ApplyDiminishingAura(false). Every path that removes a
            // control lowers the stack; missing one leaves a record that can
            // never decay.
            if(broken)localDiminishingApply(n,localDiminishingGroupForSpell(*d,false),false,authorityClockMs);
            return broken;
        });
        if(n.controls.size()!=before)
            LOG_INFO("[LOCAL_CONTROL] broken npc=",n.guid," by spell=",exceptSpell,
                     " remaining=",n.controls.size());
    }
    void damageNpc(LocalRealmNpc& n,LocalRealmPlayer& attacker,uint32_t damage,const std::vector<LocalRealmPlayer*>& players,bool physical=true,
            uint32_t spell=0,bool periodic=false,uint32_t procAura=0,const LocalSpellDefinition* threatSpell=nullptr,
            LocalMeleeOutcome outcome=LocalMeleeOutcome::Hit,bool offHand=false,uint32_t blockValue=0,uint32_t magicDamage=0,bool weaponModifiersApplied=false,bool rangedAuto=false,
            uint16_t periodicCritBasisPoints=0) {
        if(n.dead)return;
        const auto* def=content->npc(n.entry);if(!def)return;
        const auto* definition=threatSpell?threatSpell:content->spell(spell);
        // Proc amount was snapshotted with its original caster at application;
        // the aura wearer's stance must not apply a second damage modifier.
        const bool igniteTick=periodic&&spell==12654;
        if(!procAura&&!igniteTick)damage=localFormDamage(attacker,damage);
        // Periodic amounts snapshot outgoing bonuses at application. Proc
        // damage already carries its original caster's application snapshot.
        if(spell&&(physical||(definition&&(definition->schoolMask&1)))&&!periodic&&!procAura&&!weaponModifiersApplied)
            damage=localPhysicalDamageAfterTalents(attacker,*content,damage);
        if(!igniteTick)damage=localStormstrikeDamage(n,attacker,definition,damage,stormstrikeEventOffsetMs);
        // P06 : armour penetration is taken off the victim's armour
        // before the curve, exactly as Unit::CalcArmorReducedDamage does
        // (Unit.cpp:2256-2267) - the cap is a function of the VICTIM's level.
        if(physical)damage=localArmorReducedDamage(damage,
            localArmorAfterPenetration(def->armor,uint8_t(std::min<uint32_t>(n.level,255)),
                                       localFormArmorPenetrationPct(attacker,*content)),attacker.level);
        // SpellAuraEffects.cpp:6355-6368 applies the periodic critical
        // multiplier AFTER armor, so the roll lives here rather than at the
        // call site. A zero snapshot can never crit.
        if(periodic&&periodicCritBasisPoints&&meleeRoll()<periodicCritBasisPoints) {
            damage=localSpellCriticalAmount(definition,damage);outcome=LocalMeleeOutcome::Critical;
        }
        damage=uint32_t(std::min(uint64_t(1000000),uint64_t(damage)+(procAura?magicDamage:localFormDamage(attacker,magicDamage))));
        const auto blocked=std::min(damage,blockValue);damage-=blocked;
        if(localOutcomeNullifiesDamage(outcome))damage=0;
        // P04 partial resistance: Unit::CalcAbsorbResist (Unit.cpp:2327-2396),
        // which the reference runs after armor, crit and block and before
        // absorption on every direct hit (:1966), every periodic tick
        // (SpellAuraEffects.cpp:6400) and every ranged school hit. The gate is
        // localPartialResistApplies verbatim; the victim is always a creature
        // here, and the caster level is the attacker's current level, which is
        // what GetEffectiveResistChance reads whenever the attacker is present
        // (Unit.cpp:2307) - on a tick as on a direct hit, since this sweep
        // never ticks an aura whose owner is gone. The roll is rand_norm() in
        // [0, 1) at four decimals. A legacy
        // world.json spell has no school and, like a swing, never resists. The
        // magic-school component of a melee swing (`magicDamage`, which
        // Unit.cpp:1632 resists per damage index) is not rolled: 29 of the
        // 6,312 non-wand weapons in the melee catalog carry one, and that
        // index-level roll is recorded as a gap, not approximated. Wands are
        // not that case - a wand shot is spell 5019 with the wand's school and
        // does reach this roll. Bucket 10 is the reference's HITINFO_FULL_RESIST:
        // the outcome becomes Resist, the amount is zero, and the cast still
        // reached DealDamage, so the take-damage interrupt below still fires.
        uint32_t resisted=0;
        if(damage&&definition&&definition->clientSpell&&
           localPartialResistApplies(definition->schoolMask,true,definition->sourceNoCastLog,definition->sourceBinary)) {
            const auto average=localAverageResist(localResistanceForMask(def->resistances,definition->schoolMask),
                                                  attacker.level,n.level,false);
            const auto bucket=localPartialResistBucket(average,float(meleeRoll())/10000.f);
            resisted=localResistedAmount(damage,bucket);
            damage-=resisted;
            if(resisted&&!damage)outcome=LocalMeleeOutcome::Resist;
            if(resisted)LOG_DEBUG("[LOCAL_RESIST] npc=",n.guid," spell=",spell," school=",definition->schoolMask,
                                 " average=",average," bucket=",bucket," resisted=",resisted," remaining=",damage);
        }
        // Unit::DealDamage is reached by every hit that was not a miss-type
        // result, a full resist included (Unit.cpp:1660 -> :1023-1035), so the
        // interrupt runs on `Resist` from the bucket and not on any other
        // nullifying outcome.
        if(!localOutcomeNullifiesDamage(outcome)||(outcome==LocalMeleeOutcome::Resist&&resisted))breakNpcControlsOnDamage(n,spell,definition);
        if (damage && !spell && !procAura && attacker.resourceType == LocalResourceType::Rage)
            attacker.mana = std::min(attacker.maxMana, attacker.mana + std::min(15U, damage / 3 + 1));
        if(damage&&!n.lootOwner)n.lootOwner=attacker.guid;
        // ThreatManager::AddThreat's first step (ThreatManager.cpp:393-400): a
        // SPELL_ATTR1_NO_THREAT spell adds nothing and engages nothing. No
        // accepted damaging row carries it once the Lightning Overload copies
        // are retired , so this gate has no producer; it is the
        // reference's first line and costs one read. The spell_threat pctMod
        // term rides inside localTalentThreat.
        if(!(definition&&definition->clientSpell&&definition->sourceNoThreat))
            addThreat(n,attacker.guid,std::max(uint64_t(1),localTalentThreat(attacker,*content,threatSpell?threatSpell:content->spell(spell),uint64_t(damage)*1000)));
        selectThreatTarget(n,players);
        const auto effective=std::min(damage,n.health);
        consumeLocalStormstrikeCharge(n,attacker,definition,damage,outcome,procAura,periodic,stormstrikeEventOffsetMs);
        if(damage>=n.health)kill(n,attacker,players);else n.health-=damage;
        LocalCombatEvent event{0,attacker.guid,n.guid,spell,n.mapId,n.instanceId,damage+blocked+resisted,effective,0,
            procAura?LocalCombatEventKind::ProcDamage:(periodic?LocalCombatEventKind::PeriodicDamage:
                (rangedAuto?LocalCombatEventKind::PlayerRanged:spell?LocalCombatEventKind::SpellDamage:LocalCombatEventKind::PlayerMelee)),n.dead,procAura,outcome,blocked,offHand};
        event.resisted=resisted;
        if(definition){event.schoolMask=definition->schoolMask;event.spellFamily=definition->spellFamily;event.spellFamilyFlags=definition->spellFamilyFlags;}
        if(!spell||(definition&&(definition->comboProfile||definition->meleeSpecialProfile||definition->stormstrikeProfile))) {
            event.attackType=LocalCombatAttackType::Melee;
            event.weaponPeriodMs=uint32_t(std::clamp(localWeaponAmounts(attacker,*content,offHand).seconds*1000.f,1.f,60000.f));
        }
        if(rangedAuto) {
            event.attackType=LocalCombatAttackType::Ranged;
            if(const auto* weapon=localMeleeItem(attacker.equipment[17]))event.weaponPeriodMs=weapon->delay;
        }
        emitCombatEvent(event,players);
    }
    // --- Owned creatures ---------------------------------------------------
    // The summon is the source of its own combat events and holds its own
    // threat. Experience, quest credit and the loot tag follow its owner, which
    // is Unit::GetCharmerOrOwnerPlayerOrPlayerItself and never the actor.
    void damageNpcByPet(LocalRealmNpc& n,LocalRealmPet& summon,LocalRealmPlayer& owner,uint32_t raw,
                        const std::vector<LocalRealmPlayer*>& players,LocalMeleeOutcome outcome,uint32_t blockValue) {
        if(n.dead||summon.dead)return;
        const auto* def=content->npc(n.entry);if(!def)return;
        uint32_t damage=localArmorReducedDamage(std::min(raw,1000000u),def->armor,summon.level);
        const auto blocked=std::min(damage,blockValue);damage-=blocked;
        if(localOutcomeNullifiesDamage(outcome))damage=0;
        if(!localOutcomeNullifiesDamage(outcome))breakNpcControlsOnDamage(n,0,nullptr);
        if(damage&&!n.lootOwner)n.lootOwner=owner.guid;
        addThreat(n,summon.guid,std::max<uint64_t>(1,uint64_t(damage)*1000));
        selectThreatTarget(n,players);
        const auto effective=std::min(damage,n.health);
        if(damage>=n.health)kill(n,owner,players,summon.guid,summon.level);else n.health-=damage;
        LocalCombatEvent event{0,summon.guid,n.guid,0,n.mapId,n.instanceId,damage+blocked,effective,0,
            LocalCombatEventKind::PetMelee,n.dead,0,outcome,blocked};
        event.schoolMask=1;event.attackType=LocalCombatAttackType::Melee;event.weaponPeriodMs=summon.attackPeriodMs;
        emitCombatEvent(event,players);
    }
    /// An NPC swing landing on an owned creature. The summon is the target of
    /// a real event; nothing is forwarded to the owner's health or auras.
    void damagePetByNpc(LocalRealmPet& summon,LocalRealmNpc& attacker,uint32_t raw,
                        const std::vector<LocalRealmPlayer*>& players,LocalMeleeOutcome outcome) {
        if(summon.dead)return;
        const auto* def=content->npc(summon.entry);
        uint32_t damage=localArmorReducedDamage(std::min(raw,1000000u),def?def->armor:0,attacker.level);
        if(localOutcomeNullifiesDamage(outcome))damage=0;
        const auto effective=std::min(damage,summon.health);
        const bool lethal=damage>=summon.health;
        summon.health=lethal?0:summon.health-damage;
        LocalCombatEvent event{0,attacker.guid,summon.guid,0,summon.mapId,summon.instanceId,damage,effective,0,
            LocalCombatEventKind::NpcMelee,lethal,0,outcome,0};
        event.schoolMask=1;event.attackType=LocalCombatAttackType::Melee;
        emitCombatEvent(event,players);
        if(!lethal)return;
        summon.dead=true;summon.targetGuid=0;summon.attackTimer=0;
        emitCombatEvent(localKillProcEvent(attacker.guid,summon.guid,summon.mapId,summon.instanceId,false,attacker.level,false),players);
        emitCombatEvent(localDeathProcEvent(summon.guid,summon.mapId,summon.instanceId,false,summon.level),players);
    }
    /// Remove a summon without a death: dismissal, guardian expiry, an owner
    /// that travelled, died or left. Every NPC that was holding threat on it
    /// re-selects immediately so nothing pursues a GUID that no longer exists.
    void retirePet(uint64_t guid,const char* reason,const std::vector<LocalRealmPlayer*>& players) {
        const auto found=std::find_if(pets.begin(),pets.end(),[&](const auto& v){return v.guid==guid;});
        if(found==pets.end())return;
        LOG_INFO("[LOCAL_PET] owner=",found->ownerGuid," pet=",guid," entry=",found->entry," action=retire reason=",reason);
        petCasts.erase(guid);petLastManaUse.erase(guid);
        std::erase_if(petMissiles,[&](const auto& missile){return missile.source==guid;});
        pets.erase(found);
        for(auto& n:npcs) {
            bool held=false;
            for(auto& entry:n.threat)if(entry.guid==guid){entry={};held=true;}
            if(n.targetGuid==guid){n.targetGuid=0;held=true;}
            if(held)selectThreatTarget(n,players);
        }
    }
    /// Guardian::InitStatsForLevel (Pet.cpp:1041-1200), for one summon at one
    /// level. Everything here comes out of the compiled pet_levelstats and
    /// creature_template rows; nothing is derived from the spawnable world
    /// catalog, which has no record for any creature a summon names
    /// (the source audit section 0.1).
    ///
    /// Source order matters and is kept: the flat `petlevel * 50` armour first,
    /// then the row's own armour when it is positive; the template's
    /// BaseAttackTime when it is at least 1000; and - because every pet this
    /// build can create is a SUMMON_PET, never a HUNTER_PET - the row's own
    /// min_dmg/max_dmg rather than the hunter `petlevel +/- petlevel/4` formula.
    void applyPetLevelStats(LocalRealmPet& summon,uint8_t level) {
        summon.level=level;
        const auto* tmpl=localPetTemplate(summon.entry);
        const auto* def=content->npc(summon.entry);
        if(tmpl)summon.attackPeriodMs=tmpl->baseAttackTimeMs;
        const auto* row=localPetLevelStats(summon.entry,level);
        const auto previousMax=summon.maxHealth;
        // GetPetLevelInfo returning null is a real arm of the reference
        // (Pet.cpp:1147-1172): it falls back to CreatureBaseStats and five
        // hard-coded stats. The nearest thing this realm has to those base
        // stats is the world catalog's own derived health, so that is the
        // fallback here. On the shipped content it has ZERO producers - every
        // creature an admitted summon names has a pet_levelstats row and none
        // of them has a world-catalog record at all.
        summon.maxHealth=std::max(1u,row?row->health():def?def->health:1u);
        // Unit::SetMaxHealth keeps the current value proportional rather than
        // refilling, so a pet that levels mid-fight does not get a free heal.
        if(!previousMax||summon.health>summon.maxHealth)summon.health=summon.maxHealth;
        else if(previousMax!=summon.maxHealth)
            summon.health=std::max(1u,uint32_t(uint64_t(summon.health)*summon.maxHealth/previousMax));
        // Pet.cpp:1122-1127: a SUMMON_PET that is not a focus pet takes the
        // row's mana as its pool. A focus pet keeps the fixed pool of 100.
        if(summon.resourceType==0) {
            const auto mana=row?row->mana():0u;
            const auto before=summon.maxPower;
            summon.maxPower=std::max(1u,mana);
            if(!before||summon.power>summon.maxPower)summon.power=summon.maxPower;
        }
    }
    /// Whether this realm can build a pet out of this creature entry at all:
    /// the compiled pet catalog, or - the reference's own no-row arm - a world
    /// catalog record. Before the implementation only the second was consulted, and it
    /// answered null for every creature a summon names.
    bool petCreatureKnown(uint32_t entry)const {
        return localPetTemplate(entry)!=nullptr||content->npc(entry)!=nullptr;
    }
    /// pet_levelstats min_dmg..max_dmg for this summon at its level, as
    /// Guardian::InitStatsForLevel's SUMMON_PET arm sets the base weapon
    /// damage (Pet.cpp:1184-1188). Zero-zero when the row has no damage, which
    /// is the generic hunter row and never a pet this build creates.
    std::pair<uint32_t,uint32_t> petWeaponDamage(const LocalRealmPet& summon)const {
        if(const auto* row=localPetLevelStats(summon.entry,summon.level))
            return {row->minDamage(),row->maxDamage()};
        // No row: the world catalog's single derived damage value, which is
        // what every swing used before the reference. Zero producers on shipped data.
        const auto* def=content->npc(summon.entry);
        const auto flat=def?def->damage:0u;
        return {flat,flat};
    }
    /// Build the caster's controlled summon from the reviewed source profile.
    ///
    /// Spell::EffectSummonPet (SpellEffects.cpp:3368-3480) in source order:
    /// an existing summon of the SAME entry that is alive is moved beside the
    /// owner, refilled and kept - not replaced; one that is DEAD refuses the
    /// cast outright; anything else is removed first. A fresh pet then takes
    /// its owner's level, its stats from pet_levelstats and a generated name.
    bool summonPet(LocalRealmPlayer& owner,const LocalSpellDefinition& d,const std::vector<LocalRealmPlayer*>& players) {
        if(!validLocalSummonPet(d)||!d.summonPetEntry)return false;
        const auto* tmpl=localPetTemplate(d.summonPetEntry);
        const auto* def=content->npc(d.summonPetEntry);
        if(!tmpl&&!def)return false;
        if(auto* existing=controlledPetOf(owner.guid)) {
            if(existing->dead)return false; // "pet in corpse state can't be summoned"
            if(existing->entry==d.summonPetEntry) {
                // The reference teleports the pet to a close point, refills
                // health and power and clears its cooldowns, then returns. The
                // GUID and the summon epoch are deliberately preserved: this is
                // the same creature, so a callback prepared for it stays valid.
                existing->x=owner.x;existing->y=owner.y;existing->z=owner.z;
                existing->orientation=owner.orientation;
                existing->health=existing->maxHealth;
                existing->power=existing->maxPower;
                existing->powerRegenElapsedMs=0;
                LOG_INFO("[LOCAL_PET] owner=",owner.guid," pet=",existing->guid," entry=",existing->entry,
                    " action=refresh spell=",d.id," level=",unsigned(existing->level));
                return true;
            }
            retirePet(existing->guid,"replaced",players);
        }
        if(pets.size()>=kLocalMaxPets)return false;
        LocalRealmPet summon;
        summon.guid=kLocalPetGuidPrefix|(uint64_t(owner.instanceId)<<32)|uint32_t(++nextPetSerial);
        summon.ownerGuid=owner.guid;summon.summonEpoch=allocateSummonEpoch();
        summon.entry=d.summonPetEntry;summon.displayId=tmpl?tmpl->displayId:def->displayId;
        summon.mapId=owner.mapId;summon.instanceId=owner.instanceId;
        summon.summonSpellId=d.id;summon.kind=LocalPetKind::Controlled;
        // A beast summon runs on focus; every other pet this build can create
        // is a SUMMON_PET, which Player::SummonPet gives POWER_MANA
        // (Player.cpp:9230) out of the same pet_levelstats row.
        const auto creatureType=tmpl?tmpl->creatureType:uint8_t(localNpcCreatureType(summon.entry));
        if(creatureType==1) {
            summon.resourceType=2;summon.maxPower=kLocalPetMaxFocus;summon.power=kLocalPetMaxFocus;
        } else if(const auto* row=localPetLevelStats(summon.entry,owner.level);row&&row->mana()) {
            summon.resourceType=0;
        }
        applyPetLevelStats(summon,owner.level);
        // ObjectMgr::GeneratePetName, two independent draws, applied to every
        // summoned pet (SpellEffects.cpp:3475-3477).
        summon.name=localPetName(summon.entry,meleeRoll(0xFFFFFF),meleeRoll(0xFFFFFF));
        if(summon.name.empty()&&def)summon.name=def->name;
        summon.command=kLocalPetDefaultCommand;summon.react=kLocalPetDefaultReact;
        summon.x=owner.x;summon.y=owner.y;summon.z=owner.z;summon.orientation=owner.orientation;
        if(!validLocalPet(summon))return false;
        pets.push_back(std::move(summon));
        LOG_INFO("[LOCAL_PET] owner=",owner.guid," pet=",pets.back().guid," entry=",pets.back().entry,
            " action=summon spell=",d.id," level=",unsigned(pets.back().level),
            " name=",pets.back().name," health=",pets.back().maxHealth,
            " power=",pets.back().power,"/",pets.back().maxPower);
        return true;
    }
    /// Pet::SynchronizeLevelWithOwner's SUMMON_PET arm (Pet.cpp:2394-2397):
    /// always the owner's level. Returns whether anything moved.
    bool synchronizePetLevel(LocalRealmPet& summon,const LocalRealmPlayer& owner) {
        if(summon.kind!=LocalPetKind::Controlled||summon.level==owner.level||!owner.level)return false;
        if(!petCreatureKnown(summon.entry))return false;
        applyPetLevelStats(summon,owner.level);
        LOG_INFO("[LOCAL_PET] owner=",owner.guid," pet=",summon.guid," action=level level=",
            unsigned(summon.level)," health=",summon.health,"/",summon.maxHealth);
        return true;
    }
    #include "game/local_npc_spell_impact.inc"
    #include "game/local_pet_spell_runtime.inc"
    void emitCombatEvent(LocalCombatEvent event,const std::vector<LocalRealmPlayer*>& players) {
        localHydrateProcEventMetadata(event,content->spell(event.spell));
        if(event.kind!=LocalCombatEventKind::Kill&&event.kind!=LocalCombatEventKind::Death) {
            // The actor level identifies the actor. Experience and honor
            // eligibility belong to whoever the source credits: for an owned
            // creature that is its owner, and the two are never merged.
            uint8_t creditLevel=0;
            if(const auto* actor=player(event.source,players)){event.actorIsPlayer=true;event.actorLevel=creditLevel=actor->level;}
            else if(const auto* actor=pet(event.source)) {
                event.actorLevel=actor->level;
                const auto* petOwner=localPetOwner(*actor,players);
                creditLevel=petOwner?petOwner->level:actor->level;
            }
            else if(const auto* actor=npc(event.source))event.actorLevel=creditLevel=actor->level;
            if(const auto* target=player(event.target,players)) {
                event.actionTargetKnown=true;
                event.actionTargetHonorOrXpEligible=target->level>localProcGrayLevel(creditLevel);
            } else if(const auto* target=npc(event.target)) {
                event.actionTargetKnown=true;
                event.actionTargetHonorOrXpEligible=localNpcExperienceTargetEligible(target->entry,creditLevel,target->level);
            } else if(pet(event.target))event.actionTargetKnown=true;
        }
        const bool root=procDispatchDepth==0;
        if(root){
            procEffects=0;procFired={};
            // No nested effect may relocate storage referenced by its caller.
            // Every recipient is drawn from this authority player list, and
            // applications below never grow beyond the same bounded capacity.
            for(auto* owner:players)if(owner)owner->statAuras.reserve(kLocalMaxStatAuras);
        }
        if(!event.schoolMask) {
            if(const auto* d=content->spell(event.spell)) {
                event.schoolMask=d->schoolMask;event.spellFamily=d->spellFamily;event.spellFamilyFlags=d->spellFamilyFlags;
            } else if(event.kind==LocalCombatEventKind::PlayerMelee||event.kind==LocalCombatEventKind::NpcMelee)event.schoolMask=1;
        }
        if(const auto* d=content->spell(event.spell)){event.sourceRawCastTimeMs=d->sourceRawCastTimeMs;event.omenProcEligible=d->omenProcEligible;}
        if(!root) {
            event.parentSequence=procParent.sequence;event.rootSequence=procParent.rootSequence;
            event.auraOwnerGuid=procAuraOwner;event.auraCasterGuid=procAuraCaster;event.procDepth=procDispatchDepth;
        }
        event.sequence=combatHistory.record(event);
        if(root)event.rootSequence=event.sequence;
        if(event.kind==LocalCombatEventKind::ProcAura)return;
        if(event.kind==LocalCombatEventKind::SpellDamage&&(localProcEventHitMask(event)&(LocalProcHitNormal|LocalProcHitCritical)))
            if(const auto* d=content->spell(event.spell);d&&d->sourceDamageClass==1)
                if(auto* caster=player(event.source,players);caster&&consumeLocalArcaneBlast(*caster,*content,*d))
                    LOG_INFO("[LOCAL_ARCANE_BLAST] owner=",caster->guid," action=consume spell=",d->id);
        if(event.kind==LocalCombatEventKind::PlayerMelee||event.kind==LocalCombatEventKind::NpcMelee||event.kind==LocalCombatEventKind::PlayerRanged||
           event.kind==LocalCombatEventKind::PetMelee||
           event.kind==LocalCombatEventKind::SpellDamage||event.kind==LocalCombatEventKind::DirectHeal||
           (event.kind==LocalCombatEventKind::ProcDamage&&event.auraSpell&&content->spell(event.auraSpell)&&content->spell(event.auraSpell)->procCanCrit)){
            // A summon's own swing is shown to its owner with the summon as the
            // source; the owner is still neither source nor target of the event.
            uint64_t petViewer=0;
            if(const auto* actor=pet(event.source))petViewer=actor->ownerGuid;
            else if(const auto* victim=pet(event.target))petViewer=victim->ownerGuid;
            for(auto* p:players)if(p&&(p->guid==event.source||p->guid==event.target||p->guid==event.reflectionSource||(petViewer&&p->guid==petViewer))){
                if(p->meleeViewPositionRevision!=p->positionRevision){p->meleeViews={};p->meleeViewPositionRevision=p->positionRevision;}
                for(size_t i=1;i<p->meleeViews.size();++i)p->meleeViews[i-1]=p->meleeViews[i];
                if(!++p->meleeSerial)++p->meleeSerial;
                p->meleeViews.back()={p->meleeSerial,event.spell,event.effective,event.blocked,event.reflectionSource?event.reflectionSource:event.source,event.target,event.reflectionSource?LocalMeleeOutcome::Reflect:event.outcome,event.offHand,event.kind==LocalCombatEventKind::DirectHeal,event.resisted};
            }
        }
        if(event.kind==LocalCombatEventKind::ProcDamage&&event.auraSpell)if(const auto* shield=content->spell(event.auraSpell);
           shield&&(shield->proc.effect==LocalProcEffect::MeleeDamageShield||shield->areaAuraProfile))return;
        if((event.kind==LocalCombatEventKind::DirectHeal||event.kind==LocalCombatEventKind::PeriodicHeal||event.kind==LocalCombatEventKind::ProcHeal)&&event.effective) {
            // Spell::EffectHeal (Spell.cpp:2862-2866) and
            // AuraEffect::HandlePeriodicHealAurasTick (SpellAuraEffects.cpp:6676-6680):
            // threat = gain x 0.5, halved again for a paladin caster
            // (IsClass(CLASS_PALADIN)), forwarded with the caster's modifiers.
            // Thousandths: effective x 500, or x 250 for a paladin.
            auto* healer=player(event.source,players);
            if(healer&&!healer->dead) {
                const auto* spell=content->spell(event.spell);
                const bool noThreat=spell&&spell->clientSpell&&spell->sourceNoThreat; // ForwardThreatForAssistingMe :766
                const uint64_t budget=noThreat?0:localTalentThreat(*healer,*content,spell,uint64_t(event.effective)*(healer->classId==2?250:500));
                forwardThreatForAssisting(event.target,event.source,budget,event.mapId,event.instanceId,players);
            }
        }
        // Only events produced by authority effects reach this dispatcher. The
        // source and recipient each evaluate their own DBC done/taken filters.
        // OBS_MOD_HEALTH is passive regeneration in the reference: retain its
        // healing observation and assistance threat, but never trigger procs.
        if(event.kind==LocalCombatEventKind::PeriodicHeal)if(const auto* d=content->spell(event.spell);d&&d->periodicHealMaxHealthPct)return;
        if(procDispatchDepth>=MaxProcDepth||procEffects>=MaxProcEffects)return;
        struct DispatchGuard {
            Impl& g;LocalCombatEvent parent;uint64_t owner,caster;
            explicit DispatchGuard(Impl& value):g(value),parent(value.procParent),owner(value.procAuraOwner),caster(value.procAuraCaster){++g.procDispatchDepth;}
            ~DispatchGuard(){--g.procDispatchDepth;g.procParent=parent;g.procAuraOwner=owner;g.procAuraCaster=caster;}
        } guard(*this);
        std::array<LocalRealmPlayer*,2> owners{player(event.source,players),player(event.target,players)};
        if(owners[1]==owners[0])owners[1]=nullptr;
        std::array<size_t,2> activeCounts{};
        std::array<std::array<LocalStatAura,kLocalMaxStatAuras>,2> activeApplications{};
        for(size_t ownerIndex=0;ownerIndex<owners.size();++ownerIndex)if(auto* owner=owners[ownerIndex]) {
            activeCounts[ownerIndex]=std::min(owner->statAuras.size(),kLocalMaxStatAuras);
            for(size_t i=0;i<activeCounts[ownerIndex];++i)
                if(owner->statAuras[i].remainingMs){ensureLocalAuraApplication(owner->statAuras[i]);activeApplications[ownerIndex][i]=owner->statAuras[i];}
        }
        // Source preparation spends an existing Flurry charge and starts its
        // ICD before ANY proc callback. A subsequent critical proc refreshes
        // three charges while retaining that ICD; a new child was not present
        // in the prepared set and cannot spend its initiating hit's charge.
        for(size_t ownerIndex=0;ownerIndex<owners.size();++ownerIndex) {
            auto* owner=owners[ownerIndex];
            if(!owner||owner->dead||!owner->health||owner->mapId!=event.mapId||owner->instanceId!=event.instanceId)continue;
            for(size_t i=0;i<activeCounts[ownerIndex];++i) {
                auto* live=localFindAuraApplication(*owner,activeApplications[ownerIndex][i]);if(!live)continue;
                auto& aura=*live;const auto* d=content->spell(aura.spellId);
                if(!d||(event.auraSpell&&event.auraSpell==d->id)||aura.spellId!=d->id||!aura.remainingMs||!d->unsupportedReason.empty()||!validLocalProc(*d)||
                   (d->proc.effect!=LocalProcEffect::ConsumeOwnerAuraCharge&&d->proc.effect!=LocalProcEffect::ConsumeSpellCostCharge)||!aura.procCharges||aura.procCooldownMs||
                   aura.casterGuid!=owner->guid||aura.mapId!=owner->mapId||aura.instanceId!=owner->instanceId||
                   !localProcChildTalentReady(*owner,*content,*d)||!localProcMatches(d->proc,event,owner->guid)||
                   !localProcAttributeEligible(d->proc,event,{d->id,aura.costModGeneration,bool(d->proc.charges),aura.stacks,owner->guid}))continue;
                const bool chargedCost=d->proc.effect==LocalProcEffect::ConsumeSpellCostCharge;
                if(chargedCost&&(event.kind!=LocalCombatEventKind::SpellCast||event.source!=owner->guid||
                   event.appliedCostAuraSpell!=d->id||!event.appliedCostAuraGeneration||
                   event.appliedCostAuraGeneration!=aura.costModGeneration))continue;
                if(chargedCost&&d->id==12536&&(event.spellFamilyFlags[0]&0x800)&&
                   std::any_of(owner->statAuras.begin(),owner->statAuras.end(),[](const auto& a){return a.spellId==44401&&a.remainingMs;}))continue;
                const auto key=std::make_pair(owner->guid,aura.spellId);
                if(procEffects>=MaxProcEffects||std::find(procFired.begin(),procFired.begin()+procEffects,key)!=procFired.begin()+procEffects)continue;
                const auto periods=meleePeriods(*owner,*content);
                procFired[procEffects++]=key;
                localPrepareProcConsumption(d->proc,event,aura);
                localFinalizeProcConsumption(d->proc,aura);
                localRescaleMeleeTimers(*owner,periods[0],periods[1],*content);
                procParent=event;procAuraOwner=procAuraCaster=owner->guid;
                LocalCombatEvent child{0,owner->guid,owner->guid,d->id,owner->mapId,owner->instanceId,
                    0,0,0,LocalCombatEventKind::ProcAura,false,d->id};
                child.attackType=LocalCombatAttackType::None;child.auraCharges=aura.procCharges;child.auraDurationMs=aura.remainingMs;
                emitCombatEvent(child,players);
                LOG_INFO("[LOCAL_PROC_AURA] root=",event.rootSequence," parent=",event.sequence," owner=",owner->guid,
                    " aura=",d->id," action=consume charges=",unsigned(aura.procCharges)," icd=",aura.procCooldownMs);
            }
        }
        // Source reference Unit::TriggerAurasProcOnEvent prepares BOTH owners
        // before invoking source callbacks followed by target callbacks. Reserve
        // root budget and charge/ICD state now so nested callbacks cannot steal
        // an already selected parent activation.
        struct PreparedProc {
            uint64_t ownerGuid=0;
            LocalStatAura identity{};
            LocalProcDefinition proc{};
            uint32_t auraId=0,amount=0,appliedAuraId=0,criticalLeafId=0;
            size_t auraSlot=0;
            bool passive=false;
        };
        std::array<PreparedProc,MaxProcEffects> prepared{};size_t preparedCount=0;
        // Charged effects retain precedence over retaliatory shields within
        // each owner's prepared list; all source effects precede target effects.
        for(size_t ownerIndex=0;ownerIndex<owners.size();++ownerIndex)for(unsigned phase=0;phase<2;++phase) {
            auto* owner=owners[ownerIndex];
            if(!owner||owner->dead||!owner->health||owner->mapId!=event.mapId||owner->instanceId!=event.instanceId)continue;
            const size_t activeAuras=activeCounts[ownerIndex];
            const size_t passiveBranches=validLocalTalents(*owner)?owner->talents.size()*2:0;
            for(size_t index=0;index<activeAuras+passiveBranches;++index) {
                LocalStatAura passiveAura{};LocalStatAura* auraPtr=nullptr;
                const LocalSpellDefinition* d=nullptr;const LocalProcDefinition* branch=nullptr;
                if(index<activeAuras){
                    auraPtr=localFindAuraApplication(*owner,activeApplications[ownerIndex][index]);
                    if(!auraPtr)continue;
                    d=content->spell(auraPtr->spellId);if(d)branch=&d->proc;
                }
                else {
                    const size_t talentIndex=(index-activeAuras)/2;
                    const auto [talentId,rank]=owner->talents[talentIndex];d=localTalentSpell(*content,talentId,rank);
                    if(!d||!localPassiveProcPrerequisites(*owner,*content,*d))continue;
                    branch=(index-activeAuras)%2?&d->secondaryProc:&d->proc;
                    if(!d->passive||(branch->effect!=LocalProcEffect::RestorePower&&branch->effect!=LocalProcEffect::AddComboPoints&&branch->effect!=LocalProcEffect::ApplyOwnerAura&&branch->effect!=LocalProcEffect::Ignite&&branch->effect!=LocalProcEffect::RestorePetPower&&!(d->stormstrikeProfile==4&&branch->effect==LocalProcEffect::RestoreMana))||branch->cooldownMs||branch->charges||
                       owner->classId<1||owner->classId>11||!(d->allowableClasses&(1u<<(owner->classId-1))))continue;
                    if(d->requiredItemClass>=0) {
                        const auto* weapon=localMeleeItem(owner->equipment[event.attackType==LocalCombatAttackType::Ranged?localEquipmentIndex(LocalEquipmentSlot::Ranged):(event.offHand?16:15)]);
                        if(!weapon||weapon->itemClass!=uint32_t(d->requiredItemClass)||weapon->subclass>=32||
                           (d->requiredItemSubclasses&&!(d->requiredItemSubclasses&(1u<<weapon->subclass)))||
                           (d->requiredInventoryTypes&&(weapon->inventoryType>=32||!(d->requiredInventoryTypes&(1u<<weapon->inventoryType)))))continue;
                    }
                    // Passive procs derive from the currently allocated rank;
                    // they create no saved pseudo-aura and reset immediately on respec.
                    passiveAura={d->id,1,owner->mapId,owner->instanceId,owner->guid};
                    passiveAura.procAmountSnapshot=localProcAmountAtApplication(*owner,*content,*d,branch);passiveAura.hasProcAmountSnapshot=true;
                    auraPtr=&passiveAura;
                }
                auto& aura=*auraPtr;
                if(!d||(event.auraSpell&&event.auraSpell==d->id)||!branch||!d->unsupportedReason.empty()||!validLocalProc(*d)||branch->effect==LocalProcEffect::None||(branch->effect==LocalProcEffect::ConsumeOwnerAuraCharge||branch->effect==LocalProcEffect::ConsumeSpellCostCharge)||
                   unsigned(branch->effect==LocalProcEffect::MeleeDamageShield)!=phase||!aura.remainingMs||
                   (branch->charges&&!aura.procCharges)||aura.procCooldownMs||
                   aura.mapId!=owner->mapId||aura.instanceId!=owner->instanceId||
                   !localProcMatches(*branch,event,owner->guid)||
                   !localProcAttributeEligible(*branch,event,{d->id,aura.costModGeneration,bool(branch->charges),aura.stacks,owner->guid}))continue;
                const auto* form=localActiveForm(*owner);
                if(branch->requiredForms&&(!form||form->power!=owner->resourceType||form->form<1||form->form>64||!(branch->requiredForms&(uint64_t(1)<<(form->form-1)))))continue;
                const auto key=std::make_pair(owner->guid,aura.spellId);
                if(procEffects>=MaxProcEffects||std::find(procFired.begin(),procFired.begin()+procEffects,key)!=procFired.begin()+procEffects)continue;
                const auto proc=*branch;const auto auraId=aura.spellId;
                const LocalSpellDefinition* appliedAura=nullptr;
                if(proc.effect==LocalProcEffect::ApplyOwnerAura) {
                    appliedAura=content->spell(proc.spellId);
                    if(!appliedAura||!appliedAura->unsupportedReason.empty()||!validLocalProc(*appliedAura)||
                       (appliedAura->proc.effect!=LocalProcEffect::ConsumeOwnerAuraCharge&&appliedAura->proc.effect!=LocalProcEffect::ConsumeSpellCostCharge&&!appliedAura->periodicHealMaxHealthPct)||appliedAura->procParentTalentId!=d->talentId||
                       !localProcChildTalentReady(*owner,*content,*appliedAura))continue;
                    // Capacity belongs to execution: an earlier prepared
                    // callback can retire its final charge and free a slot.
                }
                const auto* criticalLeaf=d->procCanCrit?content->spell(proc.spellId):nullptr;
                if(d->procCanCrit&&(!criticalLeaf||!criticalLeaf->triggeredOnly||
                   !criticalLeaf->unsupportedReason.empty()||!validLocalProc(*criticalLeaf)||
                   !localDirectMagicCritEligible(*criticalLeaf)||criticalLeaf->damage!=proc.amount||
                   criticalLeaf->schoolMask!=proc.schoolMask||criticalLeaf->spellFamily!=proc.spellFamily||
                   criticalLeaf->spellFamilyFlags!=proc.spellFamilyFlags||
                   owner->classId!=8||aura.casterGuid!=owner->guid||owner->guid!=event.target||
                   !(localProcEventFlags(event,owner->guid)&139944u)))continue;
                const bool ignite=proc.effect==LocalProcEffect::Ignite;
                const bool combo=proc.effect==LocalProcEffect::AddComboPoints;
                const bool damage=proc.effect==LocalProcEffect::DamageAttacker||proc.effect==LocalProcEffect::MeleeDamageShield;
                auto* enemy=npc(owner->guid==event.source?event.target:event.source);
                // Unit::GetSpellModOwner: an aura cast by an owned creature
                // resolves its modifier owner through that creature's owner.
                // The owner supplies modifiers; the summon remains the caster.
                // The summon stays a distinct unit: only its modifier lookup is
                // forwarded. Its own swing period still drives PPM below.
                const LocalRealmPet* summonCaster=nullptr;
                auto* caster=player(aura.casterGuid?aura.casterGuid:owner->guid,players);
                if(!caster&&aura.casterGuid&&localPetGuid(aura.casterGuid))
                    if(const auto* summon=pet(aura.casterGuid)) {
                        summonCaster=summon;caster=localPetOwner(*summon,players);
                    }
                if(ignite&&(!localIgniteScriptMatches(event)||owner->guid!=event.source||!enemy||enemy->dead||!enemy->health||enemy->transportEntry||enemy->mapId!=owner->mapId||enemy->instanceId!=owner->instanceId||!content->spell(12654)||pendingIgnites.size()>=MaxNpcs*8))continue;
                if(d->procCanCrit&&!localRollProcBasisPoints(localMoltenArmorEventChancePct(*owner,*content,event)*100u,procRandomState))continue;
                if(combo&&(owner->guid!=event.source||event.kind!=LocalCombatEventKind::SpellDamage||!event.effective||
                    !enemy||enemy->dead||!enemy->health||enemy->transportEntry||enemy->mapId!=owner->mapId||
                    enemy->instanceId!=owner->instanceId||owner->flight.active||owner->transportEntry||
                    (owner->classId!=4&&owner->classId!=11)))continue;
                if(damage&&(!enemy||enemy->dead||!enemy->health||enemy->mapId!=owner->mapId||enemy->instanceId!=owner->instanceId||
                    enemy->transportEntry||!std::isfinite(distance2(*owner,*enemy))||distance2(*owner,*enemy)>proc.range*proc.range))continue;
                const bool hiddenMana=proc.effect==LocalProcEffect::RestoreMana&&owner->classId==11&&owner->formSpellId&&owner->resourceType!=LocalResourceType::Mana;
                if(proc.effect==LocalProcEffect::RestoreMana&&owner->resourceType!=LocalResourceType::Mana&&!hiddenMana)continue;
                if(proc.effect==LocalProcEffect::RestorePower&&uint8_t(owner->resourceType)!=proc.resourceType)continue;
                // Source CheckProc requires a live owned creature with the same
                // power system. Without one the proc does not fire at all; it
                // never falls back to the owner's own bar.
                if(proc.effect==LocalProcEffect::RestorePetPower) {
                    const auto* recipient=controlledPetOf(owner->guid);
                    if(!recipient||recipient->dead||recipient->resourceType!=proc.resourceType||!recipient->maxPower||
                       recipient->mapId!=owner->mapId||recipient->instanceId!=owner->instanceId)continue;
                }
                // Earth Shield retains its application snapshot and original
                // caster GUID even when that caster is offline or has departed.
                hydrateLegacyLocalProcAmount(aura,caster?*caster:*owner,*d);
                const auto amount=proc.effect==LocalProcEffect::HealOwnerPctMaxHealth?
                    uint32_t(uint64_t(owner->maxHealth)*proc.amount/100):aura.procAmountSnapshot;
                const auto* timingCaster=caster&&caster->mapId==owner->mapId&&caster->instanceId==owner->instanceId?caster:nullptr;
                // Unit::GetAttackTime reads the casting unit's own timer. The
                // owner was resolved for modifiers only, so an owned creature's
                // PPM keeps the creature's swing period.
                auto timing=localProcTimingForCaster(timingCaster,*content,event);
                if(summonCaster&&timing.available)timing.weaponPeriodMs=summonCaster->attackPeriodMs;
                if((!amount&&proc.effect!=LocalProcEffect::HealOwnerPctMaxHealth)||
                   !localRollProcBasisPoints(localProcChanceBasisPoints(proc,event,timing,localProcChanceModifiersForCaster(timingCaster,*content,*d)),procRandomState))continue;
                // Source preparation debits charges but expires the aura only
                // after its callback. Zero-charge prepared auras cannot proc in
                // nested dispatch, and explicit removal remains distinguishable.
                procFired[procEffects++]=key;
                localPrepareProcConsumption(proc,event,aura);
                prepared[preparedCount++]={owner->guid,aura,proc,auraId,amount,
                    appliedAura?appliedAura->id:0,criticalLeaf?criticalLeaf->id:0,index,index>=activeAuras};
            }
        }
        for(size_t preparedIndex=0;preparedIndex<preparedCount;++preparedIndex) {
                const auto& record=prepared[preparedIndex];
                auto* owner=player(record.ownerGuid,players);
                struct PreparedChargeGuard {
                    LocalRealmPlayer* owner;const PreparedProc& record;
                    ~PreparedChargeGuard() {
                        // Source ConsumeProcCharges runs after callback. Also
                        // retire a spent application when its local leaf is
                        // no longer executable, without touching replacements.
                        if(owner&&!record.passive)
                            if(auto* current=localFindAuraApplication(*owner,record.identity))
                                localFinalizeProcConsumption(record.proc,*current);
                    }
                } chargeGuard{owner,record};
                const auto* d=content->spell(record.auraId);
                if(!owner||owner->dead||!owner->health||owner->mapId!=event.mapId||owner->instanceId!=event.instanceId||
                   !d||!d->unsupportedReason.empty()||!validLocalProc(*d))continue;
                const auto& proc=record.proc;const auto auraId=record.auraId,amount=record.amount;
                if(record.passive) {
                    // A rank prepared earlier does not survive respec, rank
                    // replacement, form departure or weapon incompatibility.
                    if(!validLocalTalents(*owner)||!localPassiveProcPrerequisites(*owner,*content,*d)||
                       std::find(owner->talents.begin(),owner->talents.end(),std::make_pair(d->talentId,d->talentRank))==owner->talents.end())continue;
                    if(d->requiredItemClass>=0) {
                        const auto* weapon=localMeleeItem(owner->equipment[event.attackType==LocalCombatAttackType::Ranged?localEquipmentIndex(LocalEquipmentSlot::Ranged):(event.offHand?16:15)]);
                        if(!weapon||weapon->itemClass!=uint32_t(d->requiredItemClass)||weapon->subclass>=32||
                           (d->requiredItemSubclasses&&!(d->requiredItemSubclasses&(1u<<weapon->subclass)))||
                           (d->requiredInventoryTypes&&(weapon->inventoryType>=32||!(d->requiredInventoryTypes&(1u<<weapon->inventoryType)))))continue;
                    }
                } else {
                    const auto* current=localFindAuraApplication(*owner,record.identity);
                    if(!current||!current->remainingMs)continue;
                }
                const auto* form=localActiveForm(*owner);
                if(proc.requiredForms&&(!form||form->power!=owner->resourceType||form->form<1||form->form>64||
                   !(proc.requiredForms&(uint64_t(1)<<(form->form-1)))))continue;
                const bool ignite=proc.effect==LocalProcEffect::Ignite;
                const bool combo=proc.effect==LocalProcEffect::AddComboPoints;
                const bool damage=proc.effect==LocalProcEffect::DamageAttacker||proc.effect==LocalProcEffect::MeleeDamageShield;
                auto* enemy=npc(owner->guid==event.source?event.target:event.source);
                if((damage||combo||ignite)&&(!enemy||enemy->dead||!enemy->health||enemy->transportEntry||
                   enemy->mapId!=owner->mapId||enemy->instanceId!=owner->instanceId))continue;
                if(damage&&(!std::isfinite(distance2(*owner,*enemy))||distance2(*owner,*enemy)>proc.range*proc.range))continue;
                if(combo&&(owner->flight.active||owner->transportEntry))continue;
                const bool hiddenMana=proc.effect==LocalProcEffect::RestoreMana&&owner->classId==11&&owner->formSpellId&&owner->resourceType!=LocalResourceType::Mana;
                if(proc.effect==LocalProcEffect::RestoreMana&&owner->resourceType!=LocalResourceType::Mana&&!hiddenMana)continue;
                if(proc.effect==LocalProcEffect::RestorePower&&uint8_t(owner->resourceType)!=proc.resourceType)continue;
                LocalRealmPet* petRecipient=nullptr;
                if(proc.effect==LocalProcEffect::RestorePetPower) {
                    petRecipient=controlledPetOf(owner->guid);
                    if(!petRecipient||petRecipient->dead||petRecipient->resourceType!=proc.resourceType||!petRecipient->maxPower||
                       petRecipient->mapId!=owner->mapId||petRecipient->instanceId!=owner->instanceId)continue;
                }
                const auto* appliedAura=content->spell(record.appliedAuraId);
                size_t appliedSlot=owner->statAuras.size();uint32_t retainedAuraCooldown=0;
                if(record.appliedAuraId) {
                    if(!appliedAura||!localProcChildTalentReady(*owner,*content,*appliedAura))continue;
                    // Earlier source callbacks may have appended or replaced
                    // a child: select capacity and identity at execution time.
                    for(size_t i=0;i<owner->statAuras.size();++i) {
                        const auto* existing=content->spell(owner->statAuras[i].spellId);
                        if(existing&&existing->procParentTalentId==appliedAura->procParentTalentId){
                            appliedSlot=i;retainedAuraCooldown=std::min(owner->statAuras[i].procCooldownMs,appliedAura->proc.cooldownMs);break;
                        }
                    }
                    if(appliedSlot==owner->statAuras.size())for(size_t i=0;i<owner->statAuras.size();++i)
                        if(!owner->statAuras[i].remainingMs){appliedSlot=i;break;}
                    if(pendingStatAuraOwner==owner->guid&&
                       (appliedSlot==owner->statAuras.size()||!owner->statAuras[appliedSlot].remainingMs)&&
                       std::count_if(owner->statAuras.begin(),owner->statAuras.end(),[](const auto& a){return a.remainingMs!=0;})>=kLocalMaxStatAuras-1)continue;
                    if(appliedSlot==owner->statAuras.size()&&owner->statAuras.size()>=kLocalMaxStatAuras-(pendingStatAuraOwner==owner->guid?1u:0u))continue;
                }
                const auto* criticalLeaf=content->spell(record.criticalLeafId);
                const auto charges=record.identity.procCharges;
                procParent=event;procAuraOwner=owner->guid;
                procAuraCaster=record.identity.casterGuid?record.identity.casterGuid:owner->guid;
                if(appliedAura) {
                    const auto periods=meleePeriods(*owner,*content);
                    LocalStatAura fresh{appliedAura->id,appliedAura->durationMs,owner->mapId,owner->instanceId,owner->guid};
                    if(localClearcastingChild(*appliedAura))fresh.costModGeneration=nextLocalCostModGeneration(*owner);
                    fresh.procCharges=appliedAura->proc.charges;fresh.procAmountSnapshot=appliedAura->proc.amount;fresh.hasProcAmountSnapshot=fresh.procAmountSnapshot!=0;
                    localPrepareAuraApplication(*owner,fresh,appliedSlot<owner->statAuras.size()?&owner->statAuras[appliedSlot]:nullptr);
                    if(appliedSlot<owner->statAuras.size()) {
                        fresh.procCooldownMs=retainedAuraCooldown;
                        owner->statAuras[appliedSlot]=fresh;
                    } else owner->statAuras.push_back(fresh);
                    localRescaleMeleeTimers(*owner,periods[0],periods[1],*content);
                    LocalCombatEvent child{0,owner->guid,owner->guid,appliedAura->id,owner->mapId,owner->instanceId,
                        0,0,0,LocalCombatEventKind::ProcAura,false,auraId};
                    child.attackType=LocalCombatAttackType::None;child.auraCharges=fresh.procCharges;
                    child.auraDurationMs=fresh.remainingMs;child.auraApplied=true;
                    emitCombatEvent(child,players);
                    LOG_INFO("[LOCAL_PROC_AURA] root=",event.rootSequence," parent=",event.sequence," owner=",owner->guid,
                        " aura=",appliedAura->id," action=apply charges=",unsigned(fresh.procCharges)," icd=",fresh.procCooldownMs,
                        " haste=",unsigned(appliedAura->meleeHastePct));
                } else if(ignite) {
                    uint32_t oldAmount=0,ticks=0;
                    for(const auto& old:periodicDamage)if(old.owner==owner->guid&&old.target==enemy->guid&&old.targetEpoch==enemy->combatEpoch&&old.spell==12654&&old.remaining){oldAmount=old.damage;ticks=(old.remaining+1999)/2000;break;}
                    const auto tickAmount=localIgniteTickAmount(event.attempted-uint32_t(std::min(uint64_t(event.attempted),uint64_t(event.absorbed)+event.blocked)),proc.amount,oldAmount,ticks);
                    if(pendingIgnites.size()<MaxNpcs*8)pendingIgnites.push_back({owner->guid,enemy->guid,enemy->combatEpoch,owner->mapId,owner->instanceId,400,tickAmount,auraId,event.sequence,event.rootSequence,uint8_t(event.procDepth+1)});
                } else if(damage) {
                    LocalSpellDefinition leaf;leaf.id=proc.spellId;leaf.spellFamily=proc.spellFamily;leaf.spellFamilyFlags=proc.spellFamilyFlags;leaf.schoolMask=proc.schoolMask;
                    const bool critical=criticalLeaf&&rollSpellCritical(*owner,*criticalLeaf,enemy);
                    damageNpc(*enemy,*owner,critical?localMagicCriticalAmount(amount):amount,players,
                        (proc.schoolMask&1)!=0,proc.spellId,false,auraId,criticalLeaf?criticalLeaf:&leaf,
                        critical?LocalMeleeOutcome::Critical:LocalMeleeOutcome::Hit);
                } else if(petRecipient) {
                    // The recipient is a different unit from the aura owner:
                    // the energize lands on the summon's own bar and the
                    // observation names it as the target.
                    const auto granted=localPetRestorePower(*petRecipient,proc.resourceType,amount);
                    LocalCombatEvent child{0,owner->guid,petRecipient->guid,proc.spellId,owner->mapId,owner->instanceId,
                        amount,granted,0,LocalCombatEventKind::ProcPower,false,auraId};
                    child.schoolMask=proc.schoolMask;child.attackType=LocalCombatAttackType::None;
                    emitCombatEvent(child,players);
                    LOG_INFO("[LOCAL_PET_POWER] owner=",owner->guid," pet=",petRecipient->guid," aura=",auraId,
                        " child=",proc.spellId," power=",unsigned(proc.resourceType)," amount=",amount,
                        " granted=",granted," value=",petRecipient->power,"/",petRecipient->maxPower);
                } else if(combo) {
                    const auto before=localComboTargetValid(*owner,enemy)?owner->comboPoints:0;
                    addLocalCombo(*owner,*enemy,uint8_t(std::min(amount,5u)));
                    LocalCombatEvent child{0,owner->guid,enemy->guid,proc.spellId,owner->mapId,owner->instanceId,
                        amount,uint32_t(owner->comboPoints-before),0,LocalCombatEventKind::ProcCombo,false,auraId};
                    child.schoolMask=proc.schoolMask;child.spellFamily=proc.spellFamily;child.spellFamilyFlags=proc.spellFamilyFlags;
                    emitCombatEvent(child,players);
                } else {
                    const bool heal=proc.effect==LocalProcEffect::HealOwner||proc.effect==LocalProcEffect::HealOwnerPctMaxHealth;
                    auto& resource=heal?owner->health:hiddenMana?owner->druidMana:owner->mana;const auto before=resource;
                    const auto capacity=heal?owner->maxHealth:hiddenMana?localManaCapacity(*owner):owner->maxMana;
                    resource=uint32_t(std::min(uint64_t(capacity),uint64_t(before)+amount));
                    LocalCombatEvent child{0,heal?procAuraCaster:owner->guid,owner->guid,proc.spellId,owner->mapId,owner->instanceId,
                        amount,resource-before,0,heal?LocalCombatEventKind::ProcHeal:proc.effect==LocalProcEffect::RestorePower?LocalCombatEventKind::ProcPower:LocalCombatEventKind::ProcMana,false,auraId};
                    child.schoolMask=proc.schoolMask;child.spellFamily=proc.spellFamily;child.spellFamilyFlags=proc.spellFamilyFlags;
                    if(proc.effect==LocalProcEffect::HealOwnerPctMaxHealth)child.attackType=LocalCombatAttackType::None;
                    emitCombatEvent(child,players);
                }
                LOG_INFO("[LOCAL_PROC] root=",event.rootSequence," parent=",event.sequence," depth=",unsigned(procDispatchDepth),
                    " owner=",owner->guid," caster=",procAuraCaster," aura=",auraId," child=",proc.spellId,
                    " target=",(damage||combo)?enemy->guid:petRecipient?petRecipient->guid:owner->guid,
                    " amount=",amount," charges=",unsigned(charges)," icd=",proc.cooldownMs);
        }
        // Nested dispatch never erases aura storage referenced by its parent.
        if(root)for(auto* owner:players)if(owner)std::erase_if(owner->statAuras,[](const auto& a){return !a.remainingMs;});
    }
};

LocalGameplay::LocalGameplay():impl_(std::make_unique<Impl>()){}
LocalGameplay::~LocalGameplay()=default;
void LocalGameplay::setAuraOwnerProvider(std::function<std::vector<LocalRealmPlayer*>()> provider){impl_->auraOwnerProvider=std::move(provider);}
LocalGameplay::LocalGameplay(LocalGameplay&&) noexcept=default;
LocalGameplay& LocalGameplay::operator=(LocalGameplay&&) noexcept=default;
const LocalWorldContent& LocalGameplay::content()const{return *impl_->content;}
std::shared_ptr<LocalWorldContent> LocalGameplay::sharedContent()const{return impl_->content;}
void LocalGameplay::useContent(std::shared_ptr<LocalWorldContent> c){impl_->content=std::move(c);impl_->rebuild(true);impl_->combatHistory.clear();}
const std::vector<LocalRealmNpc>& LocalGameplay::npcs()const{return impl_->npcs;}
void LocalGameplay::setRemoteNpcs(std::vector<LocalRealmNpc> n){impl_->npcs=std::move(n);}
const std::vector<LocalRealmPet>& LocalGameplay::pets()const{return impl_->pets;}
void LocalGameplay::setRemotePets(std::vector<LocalRealmPet> v){if(validLocalPets(v))impl_->pets=std::move(v);}
bool LocalGameplay::restorePets(std::vector<LocalRealmPet> v,std::string& error) {
    if(!validLocalPets(v)){error="Invalid owned-creature state";return false;}
    for(auto& summon:v)if(!summon.summonEpoch)summon.summonEpoch=impl_->allocateSummonEpoch();
    impl_->pets=std::move(v);return true;
}
const LocalRealmPet* LocalGameplay::controlledPet(uint64_t ownerGuid)const{return impl_->controlledPetOf(ownerGuid);}
std::vector<LocalCombatEvent> LocalGameplay::combatEvents()const{return impl_->combatHistory.snapshot();}
uint64_t LocalGameplay::overwrittenCombatEvents()const{return impl_->combatHistory.overwritten();}
bool LocalGameplay::setPartyMembership(const std::vector<LocalParty>& parties) {
    if(parties.size()>LocalPartyDirector::MaxParties)return false;
    std::vector<std::pair<uint64_t,uint32_t>> next;std::set<uint32_t> ids;
    for(const auto& party:parties) {
        if(!party.id || !ids.insert(party.id).second || party.members.size()<2 || party.members.size()>5)return false;
        for(auto guid:party.members) {
            if(!guid || guid>0x0000ffffffffffffULL)return false;
            next.emplace_back(guid,party.id);
        }
    }
    std::sort(next.begin(),next.end());
    for(size_t i=1;i<next.size();++i)if(next[i-1].first==next[i].first)return false;
    impl_->partyMembership=std::move(next);
    for(auto i=impl_->lastPartyLooter.begin();i!=impl_->lastPartyLooter.end();)
        if(!ids.count(i->first))i=impl_->lastPartyLooter.erase(i);else ++i;
    for(auto i=impl_->partyInstanceOwners.begin();i!=impl_->partyInstanceOwners.end();)
        if(!ids.count(i->first))i=impl_->partyInstanceOwners.erase(i);else ++i;
    return true;
}
void LocalGameplay::restoreLootable(uint64_t guid,bool lootable) {
    if(auto* n=impl_->npc(guid);n && n->dead)n->lootable=lootable;
}


bool LocalGameplay::validCharacterOptions(uint8_t race, uint8_t cls, uint8_t gender) {
    if (gender > 1 || cls > 11) return false;
    // WotLK combinations, matching the pinned playercreateinfo rows. A catalog
    // must also supply an actual starting location before new profiles start.
    constexpr uint16_t classes[] = {0x0, 0x376, 0x2da, 0x7e, 0x87a, 0x372, 0x8ca, 0x352, 0x1fa, 0x0, 0x37c, 0x1ee};
    return race < sizeof(classes) / sizeof(classes[0]) && (classes[race] & (1u << cls));
}
bool LocalGameplay::loadCatalog(const std::string& directory, std::string& error) {
    auto c = std::make_shared<LocalWorldCatalog>();
    if (!c->load(directory, error)) return false;
    if (impl_->content->catalog) { error = "A world catalog is already loaded"; return false; }
    impl_->content->catalog = std::move(c);
    impl_->content->classResources = true;
    impl_->content->fingerprint = (impl_->content->fingerprint ^ impl_->content->catalog->fingerprint()) * 16777619U;
    impl_->rebuild(true);
    return true;
}
/// P05 line of sight . A missing or unreadable pack is not an error the
/// realm fails on: it is the shipped state, and the gates stay inert.
bool LocalGameplay::loadCollision(const std::string& directory, std::string& error) {
    LocalCollisionData data;
    if (!data.load(directory, error)) return false;
    if (data.empty()) { error = "Collision pack lists no tiles"; return false; }
    impl_->collision = std::move(data);
    impl_->content->fingerprint = (impl_->content->fingerprint ^ impl_->collision.fingerprint()) * 16777619U;
    LOG_INFO("[LOCAL_COLLISION] loaded tiles=", impl_->collision.tileCount(),
             " triangles=", impl_->collision.triangleCount(), " fingerprint=", impl_->collision.fingerprint());
    error.clear();
    return true;
}
const LocalCollisionData* LocalGameplay::collision() const {
    return impl_->collision.empty() ? nullptr : &impl_->collision;
}
void LocalGameplay::adoptCollisionTile(LocalCollisionTile tile) {
    impl_->collision.adopt(std::move(tile));
    impl_->content->fingerprint = (impl_->content->fingerprint ^ impl_->collision.fingerprint()) * 16777619U;
}
bool LocalGameplay::setStarterSpells(const std::vector<LocalSpellDefinition>& spells,
        const std::string& diagnostic, std::string& error) {
    // Was twenty hand-picked starters; it is now the whole class progression
    // the client's SkillLineAbility.dbc describes, bounded by the import's own
    // documented cap (kLocalMaxImportedClassAbilities) plus the starters.
    if(spells.size()>8192) {error="Too many client spells";return false;}
    std::set<uint32_t> ids;
    // A class mask is what makes a spell learnable by somebody, so it is
    // required of every PLAYER definition. A creature spell has no class and
    // carries allowableClasses 0 on purpose (local_npc_spell_import.hpp:26);
    // every other consumer already tests npcOnly before reading the mask
    // (:2188, :2572, :2608, :2613, :3586) and this gate alone did not, which
    // rejected the whole imported set the moment importLocalNpcSpells appended
    // Lizard Bolt 5401 and Fireball 11985 - and with it every entry into the
    // world, under the message "Invalid client starter spell".
    for(const auto& d:spells) if(!d.id||!ids.insert(d.id).second||!d.clientSpell||
            (!d.allowableClasses&&!d.npcOnly)||
            d.name.size()>96||d.unsupportedReason.size()>256) {error="Invalid client starter spell";return false;}
    auto& c=*impl_->content;
    // Hash only deterministic simulation metadata; locale text/icon paths and
    // diagnostic wording cannot split otherwise compatible LAN clients.
    const auto hash=[&](uint32_t value) {for(unsigned b=0;b<4;++b)c.fingerprint=(c.fingerprint^uint8_t(value>>(b*8)))*16777619U;};
    auto sorted=spells;std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    hash(0x42313153);hash(uint32_t(sorted.size()));
    for(const auto& d:sorted) {
        const uint32_t values[]={uint32_t(d.requiredForms),uint32_t(d.requiredForms>>32),uint32_t(d.excludedForms),uint32_t(d.excludedForms>>32),d.formId,uint32_t(d.notShapeshifted),uint32_t(d.allowWithoutForm),d.id,d.allowableClasses,d.schoolMask,uint32_t(d.passiveSchoolThreatPercent),d.passiveSchoolThreatMask,uint32_t(d.directIgnoresArmor),uint32_t(d.periodicIgnoresArmor),uint32_t(d.requiredItemClass),d.requiredItemSubclasses,d.requiredInventoryTypes,uint32_t(d.requiresMainHand),uint32_t(d.requiresOffHand),d.chainTargets,d.chainMultiplierPermille,d.resourceType,d.mana,d.manaPercent,d.cooldownMs,d.cooldownCategory,d.categoryCooldownMs,uint32_t(d.noCategoryCooldownMods),
            d.castTimeMs,d.interruptFlags,uint32_t(d.noPushback),d.globalCooldownMs,d.durationMs,d.baseLevel,d.maxLevel,d.damage,d.damageMax,
            d.heal,d.healMax,d.snarePercent,uint32_t(d.snareZeroHealingMarker),d.periodicDamage,d.periodicDamageMax,d.periodicIntervalMs,d.periodicHeal,d.periodicHealMax,
            uint32_t(d.healingSelfOnly),uint32_t(d.unsupportedReason.empty()),
            d.supercededBySpell,d.mountCreatureId,d.mountDisplayId,d.mountSpeedPercent,
            d.maxAuraStacks,d.buffHealth,d.buffArmor,d.buffAbsorb,d.absorbSchoolMask,uint32_t(d.buffSelfOnly),d.talentId,d.talentTab,d.talentRank,d.talentRow,d.passiveHealth,d.passiveArmor,uint32_t(d.passive),
            d.talentPrerequisites[0],d.talentPrerequisites[1],d.talentPrerequisites[2],d.talentPrerequisiteRanks[0],d.talentPrerequisiteRanks[1],d.talentPrerequisiteRanks[2],
            d.runeCost[0],d.runeCost[1],d.runeCost[2],d.runicPowerGain,
            uint32_t(d.proc.effect),d.proc.spellId,d.proc.flags,d.proc.cooldownMs,d.proc.amount,d.proc.baseLevel,
            d.proc.maxLevel,d.proc.schoolMask,d.proc.spellFamily,d.proc.spellFamilyFlags[0],d.proc.spellFamilyFlags[1],d.proc.spellFamilyFlags[2],d.proc.chance,d.proc.charges,d.manaPer5,d.manaPerAbsorbMilli,
            d.spellFamily,d.spellFamilyFlags[0],d.spellFamilyFlags[1],d.spellFamilyFlags[2],
            d.passivePushbackPct,d.pushbackSpellMask[0],d.pushbackSpellMask[1],d.pushbackSpellMask[2]};
        for(auto value:values) hash(value);
        hash(d.meleeSpecialProfile);hash(d.triggeredAuraSpellId);hash(uint32_t(d.triggeredOnly));
        hash(d.meleeHastePct);hash(d.procParentTalentId);
        hash(d.clearcastingProfile);hash(uint32_t(d.chargedCostPct));for(auto mask:d.chargedCostMask)hash(mask);
        hash(uint32_t(d.omenProcEligible));hash(d.sourceRawCastTimeMs);
        for(auto percent:d.passiveTotalStatPct)hash(percent);hash(d.passiveSpellCritPct);
        hash(d.passiveArmorAttackPowerDivisor);hash(d.passiveOffhandDamagePct);hash(d.passiveWeaponHitPct);
        hash(d.passivePhysicalDamagePct);hash(d.physicalDamageDonePct);hash(d.damageTakenPct);
        hash(d.passiveEquipmentArmorPct);hash(d.passiveFeralCritPct);hash(d.passiveFeralDodgePct);hash(d.passiveCatRunPct);
        hash(d.directEffectSlot);hash(d.periodicEffectSlot);hash(d.stormstrikeProfile);hash(d.stormstrikeManaChancePct);
        hash(d.passiveIntellectAttackPowerPct);hash(d.passiveDualWieldHitPct);hash(uint32_t(d.passiveCanParry));hash(uint32_t(d.passiveCanDualWield));
        hash(d.periodicHealMaxHealthPct);hash(d.arcaneBlastProfile);
        hash(d.sourceDamageClass);hash(uint32_t(d.sourceNotAProc));hash(uint32_t(d.sourceDoNotConsumeResources));hash(uint32_t(d.sourceIgnoreCasterModifiers));hash(d.rangedAutoProfile);hash(d.wardProfile);hash(d.moltenShieldsChancePct);hash(uint32_t(d.npcOnly));hash(uint32_t(d.sourceCantReflect));hash(uint32_t(d.sourceAlwaysHit));hash(uint32_t(d.sourceProjectileSpeed*1000));hash(uint32_t(d.sourceCantCrit));hash(uint32_t(d.procCanCrit));
        hash(d.spiritCritRatingPct);hash(d.incomingCritReductionPct);hash(d.mageArmorGroup);
        hash(d.summonPetEntry);hash(d.summonPetDurationMs);hash(d.summonPetKind);hash(d.summonPetEffectSlot);
        hash(d.periodicCritFamily);for(auto mask:d.periodicCritMask)hash(mask);
        hash(d.areaAuraProfile);hash(d.areaAuraEffectMask);hash(uint32_t(d.areaAuraRadius*1000));hash(uint32_t(d.indefiniteDuration));
        for(auto type:d.areaAuraTypes)hash(type);
        for(auto amount:d.areaAuraAmounts)hash(uint32_t(amount));
        for(auto misc:d.areaAuraMiscValues)hash(misc);
        hash(d.proc.recipient);hash(d.secondaryProc.recipient);
        // P04 control metadata. Two peers whose importers disagree about a
        // control must not think they share a ruleset.
        hash(d.mechanic);for(auto m:d.effectMechanic)hash(m);
        hash(d.auraInterruptFlags);hash(d.preventionType);
        hash(uint32_t(d.sourceDamageDoesNotBreakAuras));
        hash(d.controlProfile);hash(d.controlEffectSlot);
        // P04 immunity, dispel and resistance inputs: two peers must agree on
        // what a creature is immune to and what a dispel beside damage does.
        hash(d.effectMask);hash(d.dispelType);hash(uint32_t(d.sourceNoImmunities));
        hash(uint32_t(d.sourceNoSchoolImmunities));hash(uint32_t(d.sourceNoCastLog));
        hash(uint32_t(d.sourceBinary));
        hash(d.dispelProfile);hash(d.dispelAttempts);
        // P04 stacking identity : two peers must agree on which rank
        // replaces which and on what a spell's exclusivity class is.
        hash(d.firstRankSpell);for(auto e:d.sourceEffect)hash(e);for(auto a:d.effectAura)hash(a);
        hash(uint32_t(d.sourceNoThreat));hash(uint32_t(d.sourceChanneled));hash(uint32_t(d.sourceDotStackingRule));
        // P05 shared combat inputs : the hit roll, the block and the
        // initial threat of a cast read these on both peers.
        hash(d.spellLevel);hash(uint32_t(d.sourceNoActiveDefense));hash(uint32_t(d.sourceCompletelyBlocked));
        hash(uint32_t(d.sourceSuppressTargetProcs));hash(uint32_t(d.sourceDirectDamage));hash(uint32_t(d.sourceNoInitialThreat));
        // P05 range, facing and target rules : two peers must agree on
        // which range type a spell has, which facings it accepts and which
        // creature types and combat states it may be cast at.
        hash(d.sourceRangeFlags);hash(d.sourceFacingFlags);hash(d.targetCreatureType);
        hash(uint32_t(d.sourceOnlyPeacefulTargets));
        // P05 line of sight : two peers must agree on which casts are
        // exempt from the test before they can agree on the test's answer.
        hash(uint32_t(d.sourceIgnoreLineOfSight));
        hash(uint32_t(d.sourceOnlyOutdoors));
        hash(d.proc.triggerSchoolMask);hash(d.proc.triggerSpellFamily);
        for(auto mask:d.proc.triggerSpellFamilyFlags)hash(mask);
        hash(d.proc.hitMask);hash(d.proc.phaseMask);hash(uint32_t(d.proc.allowTriggered));hash(d.proc.pushbackPercent);hash(d.proc.resourceType);hash(d.proc.spellTypeMask);
        hash(d.passiveManaRegenInterruptPct);
        hash(d.passiveMeleeCritPct);
        // P06 : the imported form-boost amounts and the two entry-resource
        // talent amounts. A peer whose importer produced different numbers for
        // these must not pass the content join check.
        hash(d.passiveAttackPower);hash(d.passiveArmorPenetrationPct);
        hash(d.furorChancePct);hash(d.retainedRage);
        hash(uint32_t(d.proc.requiredForms));hash(uint32_t(d.proc.requiredForms>>32));
        const auto& secondary=d.secondaryProc;
        const uint32_t secondaryValues[]={uint32_t(secondary.effect),secondary.spellId,secondary.flags,secondary.cooldownMs,
            secondary.amount,secondary.baseLevel,secondary.maxLevel,secondary.schoolMask,secondary.spellFamily,
            secondary.spellFamilyFlags[0],secondary.spellFamilyFlags[1],secondary.spellFamilyFlags[2],
            secondary.chance,secondary.charges,secondary.triggerSchoolMask,secondary.triggerSpellFamily,
            secondary.triggerSpellFamilyFlags[0],secondary.triggerSpellFamilyFlags[1],secondary.triggerSpellFamilyFlags[2],
            secondary.hitMask,secondary.phaseMask,secondary.spellTypeMask,uint32_t(secondary.allowTriggered),
            secondary.pushbackPercent,secondary.resourceType,uint32_t(secondary.requiredForms),uint32_t(secondary.requiredForms>>32)};
        for(auto value:secondaryValues)hash(value);
        for(const auto* proc:{&d.proc,&d.secondaryProc}){hash(proc->attributesMask);hash(proc->disableEffectsMask);hash(proc->sourceEffectMask);hash(uint32_t(proc->hasUnsupportedConditions));hash(uint32_t(proc->hasUnsupportedScript));}
        for(float value:{secondary.amountPerLevel,secondary.range,secondary.ppm}) {uint32_t bits;std::memcpy(&bits,&value,4);hash(bits);}
        for(auto percent:d.passiveManaRegenStatPct)hash(percent);
        for(const auto& mod:d.passiveCastModifiers) {
            hash(uint32_t(mod.active));hash(mod.operation);hash(uint32_t(mod.percentage));hash(uint32_t(mod.amount));for(auto word:mod.mask)hash(word);
        }
        for(float value:{d.range,d.minRange,d.chainRadius,d.areaRadius,d.periodicDamagePerLevel,d.damagePerLevel,d.healPerLevel,d.periodicHealPerLevel,d.proc.amountPerLevel,d.proc.range,d.proc.ppm,d.passiveAttackPowerPerLevel}) {uint32_t bits;std::memcpy(&bits,&value,4);hash(bits);}
        const auto existing=std::find_if(c.spells.begin(),c.spells.end(),[&](const auto& old){return old.id==d.id;});
        if(existing==c.spells.end())c.spells.push_back(d);else *existing=d;
    }
    std::sort(c.spells.begin(),c.spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    c.talentSpellIndex.clear();
    for(const auto& d:c.spells)if(d.talentId)c.talentSpellIndex.push_back({d.talentId,d.talentRank,d.id});
    std::sort(c.talentSpellIndex.begin(),c.talentSpellIndex.end());c.talentIndexReady=true;
    c.clientStarterSpells=true;c.classResources=true;c.spellDiagnostic=diagnostic;
    error.clear();return true;
}
bool LocalGameplay::setAreaTriggers(const std::vector<LocalAreaTriggerVolume>& volumes, std::string& error) {
    if (volumes.size() > 16384) { error = "Too many area trigger volumes"; return false; }
    std::set<uint32_t> ids;
    for (const auto& v : volumes) {
        if (!v.id || !ids.insert(v.id).second || v.mapId > 10000 ||
            !std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) ||
            !std::isfinite(v.radius) || v.radius < 0 || v.radius > 200000 ||
            !std::isfinite(v.boxLength) || v.boxLength < 0 || v.boxLength > 200000 ||
            !std::isfinite(v.boxWidth) || v.boxWidth < 0 || v.boxWidth > 200000 ||
            !std::isfinite(v.boxHeight) || v.boxHeight < 0 || v.boxHeight > 200000 ||
            !std::isfinite(v.boxYaw) || std::abs(v.x) > 100000 || std::abs(v.y) > 100000 || std::abs(v.z) > 100000) {
            error = "Invalid area trigger geometry (id=" + std::to_string(v.id) + ", map=" + std::to_string(v.mapId) + ")"; return false;
        }
    }
    impl_->volumes = volumes; error.clear(); return true;
}
const std::vector<LocalAreaTriggerVolume>& LocalGameplay::areaTriggers() const { return impl_->volumes; }

bool LocalGameplay::setTravelNetwork(std::vector<LocalTaxiNode> nodes,
                                     std::vector<LocalTaxiPath> paths,
                                     std::vector<LocalTaxiWaypoint> waypoints,
                                     std::string& error) {
    auto& g = *impl_;
    if (!g.travel.setClientData(std::move(nodes), std::move(paths), std::move(waypoints), error)) {
        return false;
    }
    // The catalog is the authority on which transports exist; the built-in
    // table only fills in for a catalog that carries none, and every entry in
    // it is checked against the client data that was just installed.
    g.travel.setTransportRoutes(LocalTravelNetwork::builtinTransportRoutes());
    g.transports.clear();
    // Already-spawned NPCs were classified against the previous (empty)
    // network, so they carry no flight-master mark. Rebuilding is what gives
    // them one without a special case for "the network arrived late".
    g.rebuild();
    error.clear();
    return true;
}

void LocalGameplay::useTravelNetwork(const LocalTravelNetwork& network) {
    if (!network.loaded()) return;
    impl_->travel = network;
    impl_->transports.clear();
    // Same reason setTravelNetwork rebuilds: any NPC already spawned was
    // classified against the previous network and carries no flight-master mark.
    impl_->rebuild();
}
const LocalTravelNetwork& LocalGameplay::travel() const { return impl_->travel; }

const std::vector<LocalTransportState>& LocalGameplay::transports() const {
    return impl_->transports;
}

bool LocalGameplay::discoverTaxiNode(LocalRealmPlayer& player, uint32_t nodeId) {
    if (!nodeId) return false;
    if (std::find(player.knownTaxiNodes.begin(), player.knownTaxiNodes.end(), nodeId) !=
        player.knownTaxiNodes.end()) {
        return false;
    }
    // Bounded like every other per-character list here. 3.3.5a has fewer than
    // three hundred nodes, so this is a corruption guard rather than a limit a
    // player can reach by travelling.
    if (player.knownTaxiNodes.size() >= 512) return false;
    player.knownTaxiNodes.push_back(nodeId);
    return true;
}

std::vector<uint32_t> LocalGameplay::flightDestinations(const LocalRealmPlayer& player,
                                                        uint32_t fromNode) const {
    auto result=impl_->travel.destinationsFrom(fromNode, player.knownTaxiNodes);
    const bool horde=player.race==2 || player.race==5 || player.race==6 || player.race==8 || player.race==10;
    result.erase(std::remove_if(result.begin(),result.end(),[&](auto id){const auto* n=impl_->travel.node(id);return !n || !(horde?n->mountHorde:n->mountAlliance);}),result.end());return result;
}
const std::vector<LocalInstanceState>& LocalGameplay::instances() const { return impl_->instances; }
bool LocalGameplay::restoreInstances(const std::vector<LocalInstanceState>& instances, std::string& error) {
    if (instances.size() > MaxInstances) { error = "Saved instance binding count exceeds 128"; return false; }
    std::set<uint32_t> ids; std::set<std::pair<uint32_t, uint64_t>> groups;
    uint32_t next = 1;
    for (const auto& instance : instances) {
        if (!instance.id || instance.id > 65535 || instance.mapId > 10000 ||
            !ids.insert(instance.id).second || !groups.emplace(instance.mapId, instance.groupId).second) {
            error = "Invalid saved instance identity"; return false;
        }
        // Either source may vouch for the map. A binding whose map the catalog
        // never carried but the client's own Map.dbc calls a dungeon is a
        // binding this build created legitimately, and discarding it would
        // strand the character inside it.
        if (content().catalog && !instanceMap(instance.mapId)) {
            error = "Saved instance map is missing from the world catalog and the client's Map.dbc"; return false;
        }
        next = std::max(next, instance.id + 1);
    }
    impl_->instances = instances; impl_->nextInstanceId = next;
    // Also used by portal transaction rollback: forget a newly allocated owner
    // if its first binding was not committed, retaining existing party bindings.
    for(auto i=impl_->partyInstanceOwners.begin();i!=impl_->partyInstanceOwners.end();) {
        if(std::none_of(instances.begin(),instances.end(),[&](const auto& v){return v.groupId==i->second;}))
            i=impl_->partyInstanceOwners.erase(i);
        else ++i;
    }
    error.clear(); return true;
}
bool LocalGameplay::setClientMaps(std::vector<LocalMapDefinition> maps, std::string& error) {
    if (maps.size() > 8192) { error = "Too many client map rows"; return false; }
    std::sort(maps.begin(), maps.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    for (size_t i = 1; i < maps.size(); ++i) if (maps[i].id == maps[i - 1].id) {
        error = "Duplicate client map id " + std::to_string(maps[i].id); return false;
    }
    for (const auto& map : maps) if (map.id > 10000 || map.instanceType > 4 || map.maxPlayers > 100) {
        error = "Invalid client map row " + std::to_string(map.id); return false;
    }
    impl_->maps = std::move(maps); error.clear(); return true;
}
const std::vector<LocalMapDefinition>& LocalGameplay::clientMaps() const { return impl_->maps; }
const LocalMapDefinition* LocalGameplay::clientMap(uint32_t mapId) const {
    return definition(impl_->maps, mapId);
}
bool LocalGameplay::instanceMap(uint32_t mapId) const {
    if (const auto* map = clientMap(mapId)) { if (map->instance()) return true; }
    if (content().catalog) for (const auto& m : content().catalog->maps()) if (m.id == mapId) return m.instanceMap;
    return false;
}
std::vector<LocalRealmPortal> LocalGameplay::portals() const {
    std::vector<LocalRealmPortal> result;
    for (const auto& pad:LocalAcherusPortals)
        result.push_back({pad.id,pad.mapId,pad.mapId,pad.name,false,0,0});
    if (!content().catalog) return result;
    // The trigger volumes are the expensive side: a client has thousands, and
    // this used to be a linear scan of all of them per destination on a path
    // the interface walks every frame. Index them once by id instead.
    std::vector<std::pair<uint32_t, const LocalAreaTriggerVolume*>> byId;
    byId.reserve(impl_->volumes.size());
    for (const auto& volume : impl_->volumes) byId.emplace_back(volume.id, &volume);
    std::sort(byId.begin(), byId.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& destination : content().catalog->destinations()) {
        const auto found = std::lower_bound(byId.begin(), byId.end(), destination.id,
            [](const auto& entry, uint32_t key) { return entry.first < key; });
        if (found == byId.end() || found->first != destination.id) continue;
        LocalRealmPortal portal{destination.id, found->second->mapId, destination.mapId,
                                destination.name, destination.instanceMap, 0, 0};
        // The client's own Map.dbc has the final say on what the target map is.
        // A dungeon or raid the catalog's instance_template dump never carried
        // is still a dungeon, and this is what makes its entrance usable.
        if (const auto* map = clientMap(destination.mapId)) {
            portal.instanceType = map->instanceType;
            portal.maxPlayers = map->maxPlayers;
            portal.instanceMap = portal.instanceMap || map->instance();
            if (portal.name.empty()) portal.name = map->name;
        }
        result.push_back(std::move(portal));
    }
    // Which teleports this console can actually offer, said once.
    //
    // A portal needs two halves that come from two different places: the
    // destination, which is a row of the server's areatrigger_teleport carried
    // through by the catalog importer, and the volume that triggers it, which is
    // a row of the player's own AreaTrigger.dbc. Either alone is nothing, and
    // until now a destination with no volume was indistinguishable from a
    // destination that was never imported.
    //
    // It was asked for by name over the Death Knight start. The Ebon Hold pads
    // are a good example of the split: the upstream dump has twelve "Ebon Hold
    // (E.K.)" rows (ids 5127-5138), every one of them a teleport within Eastern
    // Kingdoms, and the importer already carries them because it carries that
    // whole table - so whether they work here is entirely a question about the
    // player's own DBC, which this answers. What the dump has none of is any row
    // targeting map 609, the Scarlet Enclave: the starting zone's own pads are
    // server scripts, they are not in that table, and nothing here invents them.
    if (!impl_->portalScanLogged) {
        impl_->portalScanLogged = true;
        const auto& destinations = content().catalog->destinations();
        LOG_INFO("[LOCAL_PORTAL_SCAN] client trigger volumes=", impl_->volumes.size(),
                 " catalog destinations=", destinations.size(),
                 " usable portals=", result.size());
        unsigned named = 0;
        for (const auto& destination : destinations) {
            if (std::any_of(result.begin(), result.end(),
                            [&](const LocalRealmPortal& p) { return p.id == destination.id; })) continue;
            // Bounded: a client missing a whole expansion's triggers would
            // otherwise write hundreds of lines into a console log.
            if (++named > 16) break;
            LOG_INFO("[LOCAL_PORTAL_SCAN] destination ", destination.id, " \"", destination.name,
                     "\" -> map ", destination.mapId,
                     " has no AreaTrigger.dbc volume in this client");
        }
        // And the other direction: a map whose triggers all lead nowhere. Map
        // 609 is expected to be one of them for the reason written above.
        std::map<uint32_t, std::pair<size_t, size_t>> byMap;
        for (const auto& volume : impl_->volumes) ++byMap[volume.mapId].first;
        for (const auto& portal : result) ++byMap[portal.sourceMapId].second;
        unsigned reported = 0;
        for (const auto& [mapId, counts] : byMap) {
            if (counts.second || counts.first < 4 || ++reported > 8) continue;
            LOG_INFO("[LOCAL_PORTAL_SCAN] map ", mapId, " carries ", counts.first,
                     " trigger volumes and none of them teleports anywhere");
        }
    }
    return result;
}
bool LocalGameplay::setSkillLines(const std::vector<LocalSkillLine>& lines, std::string& error) {
    if (lines.size() > 4096) { error = "Too many client skill lines"; return false; }
    std::vector<LocalSkillLine> kept;
    std::set<uint32_t> ids;
    for (const auto& line : lines) {
        // Only what a profession trainer can teach. A client's SkillLine.dbc is
        // mostly class abilities, weapon skills and languages, none of which
        // this realm models, and keeping them would put "Common" and "Daggers"
        // in a trainer's list.
        if (line.category != kLocalSkillCategoryProfession && line.category != kLocalSkillCategorySecondary) continue;
        if (!line.id || line.name.empty() || line.name.size() > 96) continue;
        if (!ids.insert(line.id).second) { error = "Duplicate client skill line"; return false; }
        kept.push_back(line);
    }
    std::sort(kept.begin(), kept.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    impl_->skills = std::move(kept); error.clear(); return true;
}
const std::vector<LocalSkillLine>& LocalGameplay::skillLines() const {
    // The client's own rows whenever it has supplied them; the documented
    // fourteen otherwise, so a realm started before the MPQs are read still
    // knows what a Blacksmithing trainer is offering.
    return impl_->skills.empty() ? localBuiltinProfessions() : impl_->skills;
}
const LocalRealmNpc* LocalGameplay::serviceNpc(const LocalRealmPlayer& p, uint32_t npcFlag, uint64_t npcGuid) const {
    const LocalRealmNpc* best = nullptr;
    float bestDistance = ServiceRange * ServiceRange;
    for (const auto& npc : impl_->npcs) {
        if (npcGuid && npc.guid != npcGuid) continue;
        if (npc.dead || npc.mapId != p.mapId || npc.instanceId != p.instanceId) continue;
        const bool offers =
            (npcFlag == kLocalNpcFlagAnyVendor && npc.vendor) ||
            (npcFlag == kLocalNpcFlagRepair && npc.repairer) ||
            (npcFlag == LocalTravelNetwork::NpcFlagFlightMaster && npc.flightMaster) ||
            (npcFlag == kLocalNpcFlagTrainerClass && npc.classTrainer) ||
            (npcFlag == kLocalNpcFlagTrainerProfession && npc.professionTrainer) ||
            (npcFlag == kLocalNpcFlagInnkeeper && npc.innkeeper) ||
            (npcFlag == kLocalNpcFlagAuctioneer && npc.auctioneer) ||
            (npcFlag == kLocalNpcFlagBanker && npc.banker);
        if (!offers) continue;
        // A merchant that is hostile to this character is not open for
        // business, the same way a hostile quest contact refuses to talk.
        const auto* def = content().npc(npc.entry);
        const auto* npcFaction = def ? definition(impl_->factions, def->faction) : nullptr;
        const auto* playerFaction = p.race < impl_->raceFactions.size() ? definition(impl_->factions, impl_->raceFactions[p.race]) : nullptr;
        if (npcFaction && playerFaction) {
            // Passive/nonattackable enemies still refuse commerce. Aggro and
            // attackability are combat rules, not a service eligibility test.
            if (factionRelation(*npcFaction, *playerFaction) > 0 || factionRelation(*playerFaction, *npcFaction) > 0) continue;
        } else if (npc.hostile) continue;
        const float d = distance2(p, npc);
        if (d <= bestDistance) { bestDistance = d; best = &npc; }
    }
    return best;
}
std::vector<uint32_t> LocalGameplay::vendorStock(const LocalRealmPlayer& p, uint64_t npcGuid) const {
    const auto* merchant = serviceNpc(p, kLocalNpcFlagAnyVendor, npcGuid);
    if (!merchant) return {};
    // The NPC entry identifies its actual upstream stock; a general vendor
    // flag cannot distinguish a blacksmith from a food seller.
    const auto* def = content().npc(merchant->entry);
    static const std::vector<uint32_t> none;
    return localVendorStockForNpc(merchant->entry, def ? def->vendorItems : none, content());
}
int32_t LocalGameplay::vendorRemaining(const LocalRealmPlayer& p, uint32_t itemId, uint64_t npcGuid) const {
    const auto* merchant = serviceNpc(p, kLocalNpcFlagAnyVendor, npcGuid);
    if (!merchant) return 0;
    const auto* definition = content().npc(merchant->entry);
    if (definition && !definition->vendorItems.empty())
        return std::find(definition->vendorItems.begin(), definition->vendorItems.end(), itemId) != definition->vendorItems.end() ? -1 : 0;
    const auto* offer = localVendorOffer(merchant->entry, itemId);
    if (!offer) return 0;
    if (!offer->maxCount) return -1;
    return int32_t(std::min<uint32_t>(INT32_MAX,
        impl_->vendorInventory.available(merchant->guid, *offer, localVendorBuyCount(itemId), impl_->now)));
}
LocalVendorInventory LocalGameplay::vendorInventorySnapshot() const { return impl_->vendorInventory; }
void LocalGameplay::restoreVendorInventory(LocalVendorInventory snapshot) { impl_->vendorInventory = std::move(snapshot); }
std::vector<LocalVendorStockRecord> LocalGameplay::savedVendorStock() const { return impl_->vendorInventory.snapshot(impl_->now); }
bool LocalGameplay::restoreVendorStock(const std::vector<LocalVendorStockRecord>& records) { return impl_->vendorInventory.restore(records, impl_->now); }
namespace {
/// The level at which the client's own data makes an ability available.
/// One definition, shared with the trainer price and the trainer window since
/// the implementation (game/local_services.hpp); the comment that used to stand here named
/// spellLevel while the code read baseLevel, and after the implementation's column
/// correction baseLevel is the column that is actually meant. The reference's
/// own authority is trainer_spell.ReqLevel, which this realm does not import.
uint8_t spellUnlockLevel(const LocalSpellDefinition& d) {
    return localSpellUnlockLevel(d);
}
/// Is a higher rank of this ability already within this character's reach?
/// Retail replaces a rank rather than stacking it; keeping both would double
/// the ability in the spellbook and let a trainer sell one already outgrown.
bool supercededHere(const LocalWorldContent& c, const LocalSpellDefinition& d, uint8_t level) {
    const auto* next = d.supercededBySpell ? c.spell(d.supercededBySpell) : nullptr;
    return next && next->clientSpell && next->unsupportedReason.empty() && spellUnlockLevel(*next) <= level;
}
}
std::vector<uint32_t> LocalGameplay::trainableSpells(const LocalRealmPlayer& p, uint64_t npcGuid) const {
    std::vector<uint32_t> result;
    const auto* trainer = serviceNpc(p, kLocalNpcFlagTrainerClass, npcGuid);
    if (!trainer || !trainer->trainerClass || trainer->trainerClass != p.classId) return result;
    for (const auto& spell : content().spells) {
        // Only abilities imported from the player's own Spell.dbc, only for
        // this class, and only ones the ruleset can actually cast: offering an
        // ability whose import failed would sell a spell that refuses to fire.
        if (!spell.clientSpell || spell.npcOnly || spell.triggeredOnly || spell.mountDisplayId || !spell.unsupportedReason.empty()) continue;
        if (!spell.allowableClasses || !(spell.allowableClasses & (1u << (p.classId - 1)))) continue;
        if (spellUnlockLevel(spell) > p.level) continue;
        if(spell.talentId||!localProcTalentPrerequisite(p,content(),spell))continue;
        if (std::find(p.knownSpells.begin(), p.knownSpells.end(), spell.id) != p.knownSpells.end()) continue;
        // Only the highest rank this character has reached. Retail replaces a
        // rank rather than stacking it, so offering rank three to a character
        // who could have rank five would sell them the wrong ability.
        if (supercededHere(content(), spell, p.level)) continue;
        if (std::any_of(p.knownSpells.begin(), p.knownSpells.end(), [&](uint32_t id) {
            return laterSpellRank(content(), spell.id, id);
        })) continue;
        result.push_back(spell.id);
    }
    return result;
}
bool LocalGameplay::setRecipes(std::vector<LocalRecipe> recipes, std::string& error) {
    if (recipes.size() > 8192) { error = "Too many client recipes"; return false; }
    std::sort(recipes.begin(), recipes.end(),
              [](const auto& a, const auto& b) { return a.spellId < b.spellId; });
    std::set<uint32_t> seen;
    for (const auto& r : recipes) {
        if (!r.spellId || !r.skillId || !r.createdItemId || !r.createdCount || r.reagents.empty() ||
            r.reagents.size() > 8 || r.access.size() > 16 || r.unsupportedReason.size() > 256 || r.name.empty() || r.name.size() > 96 || !seen.insert(r.spellId).second) {
            error = "Invalid client recipe " + std::to_string(r.spellId); return false;
        }
        std::set<uint32_t> reagentIds;
        for (const auto& reagent : r.reagents)
            if (!reagent.itemId || !reagent.count || !reagentIds.insert(reagent.itemId).second) {
                error = "Invalid or duplicate recipe reagent"; return false;
            }
    }
    auto& c = *impl_->content;
    // Recipes are simulation input like the spell set, so they take part in the
    // content fingerprint: two consoles whose clients produced different
    // recipes must not share a realm and silently disagree about a craft.
    const auto hash = [&](uint32_t value) {
        for (unsigned b = 0; b < 4; ++b) c.fingerprint = (c.fingerprint ^ uint8_t(value >> (b * 8))) * 16777619U;
    };
    hash(0x52435032); hash(uint32_t(recipes.size()));
    for (const auto& r : recipes) {
        for (auto value : {r.spellId, uint32_t(r.skillId), uint32_t(r.requiredSkill), uint32_t(r.trivialHigh),
                           uint32_t(r.trivialLow), r.createdItemId, uint32_t(r.createdCount),
                           uint32_t(r.reagents.size())}) hash(value);
        for (const auto& reagent : r.reagents) { hash(reagent.itemId); hash(reagent.count); }
        for (auto tool : r.tools) hash(tool);
        hash(uint32_t(r.access.size()));
        for (const auto& a : r.access) { hash(a.races); hash(a.classes); hash(a.excludedRaces); hash(a.excludedClasses); }
        hash(uint32_t(r.unsupportedReason.size()));
        for (unsigned char ch : r.unsupportedReason) hash(ch);
    }
    c.recipes = std::move(recipes);
    error.clear(); return true;
}
std::vector<uint32_t> LocalGameplay::trainableRecipes(const LocalRealmPlayer& p, uint64_t npcGuid) const {
    std::vector<uint32_t> result;
    const auto* trainer = serviceNpc(p, kLocalNpcFlagTrainerProfession, npcGuid);
    if (!trainer || !trainer->trainerSkill) return result;
    const auto known = std::find_if(p.professions.begin(), p.professions.end(),
        [&](const LocalProfessionSkill& s) { return s.skillId == trainer->trainerSkill; });
    if (known == p.professions.end()) return result;
    for (const auto& recipe : content().recipes) {
        if (recipe.skillId != trainer->trainerSkill || !localRecipeAllows(recipe, p)) continue;
        // What the character's own skill has reached, and no further: a recipe
        // beyond it is what the next rank of training is for.
        if (recipe.requiredSkill > known->current) continue;
        if (std::find(p.knownRecipes.begin(), p.knownRecipes.end(), recipe.spellId) != p.knownRecipes.end()) continue;
        // A recipe whose product this world cannot name is one the trainer
        // would be selling a blank for.
        if (!content().item(recipe.createdItemId)) continue;
        result.push_back(recipe.spellId);
    }
    return result;
}
std::vector<uint32_t> LocalGameplay::craftableRecipes(const LocalRealmPlayer& p) const {
    std::vector<uint32_t> result;
    for (auto spellId : p.knownRecipes) {
        const auto* recipe = content().recipe(spellId);
        if (!recipe || !localRecipeAllows(*recipe,p) || !localRecipeHasTools(*recipe,p)) continue;
        const auto skill=std::find_if(p.professions.begin(),p.professions.end(),[&](const auto& row){return row.skillId==recipe->skillId;});
        if(skill==p.professions.end() || skill->current<recipe->requiredSkill)continue;
        bool haveAll = true;
        for (const auto& reagent : recipe->reagents)
            haveAll = haveAll && localRecipeReagentCount(*recipe,p,reagent.itemId) >= reagent.count;
        if (haveAll) result.push_back(spellId);
    }
    return result;
}
namespace {
bool insideVolume(const LocalAreaTriggerVolume& v, const LocalRealmPlayer& p) {
    if (v.mapId != p.mapId) return false;
    const float dx = p.x - v.x, dy = p.y - v.y, dz = p.z - v.z;
    if (v.radius > 0) return dx * dx + dy * dy + dz * dz <= v.radius * v.radius;
    if (v.boxLength <= 0 || v.boxWidth <= 0 || v.boxHeight <= 0) return false;
    const float co = std::cos(v.boxYaw), si = std::sin(v.boxYaw);
    return std::abs(co * dx + si * dy) <= v.boxLength * 0.5f &&
           std::abs(-si * dx + co * dy) <= v.boxWidth * 0.5f && std::abs(dz) <= v.boxHeight * 0.5f;
}
LocalResourceType classResource(uint8_t cls) {
    if (cls == 1) return LocalResourceType::Rage;
    if (cls == 4) return LocalResourceType::Energy;
    if (cls == 6) return LocalResourceType::RunicPower;
    return LocalResourceType::Mana;
}
}

bool LocalGameplay::insidePortal(uint32_t id, const LocalRealmPlayer& p) const {
    if (const auto* pad=localScriptedPortal(id)) {
        if (auto latch=impl_->portalExitLatch.find(p.guid);latch!=impl_->portalExitLatch.end()) {
            const auto* exit=localScriptedPortal(latch->second);
            if (!exit || !exit->contains(p.mapId,p.x,p.y,p.z)) impl_->portalExitLatch.erase(latch);
            else if (latch->second==id) return false;
        }
        return p.classId==6 && !p.instanceId && pad->contains(p.mapId,p.x,p.y,p.z);
    }
    for (const auto& v : impl_->volumes) if (v.id == id) return insideVolume(v, p);
    return false;
}
bool LocalGameplay::setGraveyards(const std::vector<LocalGraveyardSite>& sites,std::string& error) {
    if(sites.size()>16384){error="Too many graveyards";return false;}
    for(const auto& site:sites)if(!site.id || site.mapId>10000 || site.zoneId>100000 ||
        !std::isfinite(site.x)||!std::isfinite(site.y)||!std::isfinite(site.z)||!std::isfinite(site.orientation) ||
        std::abs(site.x)>50000 || std::abs(site.y)>50000 || std::abs(site.z)>50000){error="Invalid graveyard site";return false;}
    impl_->graveyards=sites;error.clear();return true;
}
const std::vector<LocalGraveyardSite>& LocalGameplay::graveyards()const{return impl_->graveyards;}
bool LocalGameplay::setFactionTemplates(const std::vector<LocalFactionTemplate>& rows,
                                       const std::array<uint32_t, 12>& races, std::string& error) {
    if (rows.size() > 16384) { error = "Faction template limit exceeded"; return false; }
    std::set<uint32_t> ids;
    for (const auto& r : rows) if (!r.id || !ids.insert(r.id).second) { error = "Duplicate/zero faction template"; return false; }
    for (auto id : races) if (id && !ids.count(id)) { error = "Race references missing faction template"; return false; }
    impl_->factions = rows;
    std::sort(impl_->factions.begin(), impl_->factions.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    impl_->raceFactions = races; error.clear(); return true;
}
const std::vector<LocalFactionTemplate>& LocalGameplay::factionTemplates() const { return impl_->factions; }
const std::array<uint32_t, 12>& LocalGameplay::raceFactionTemplates() const { return impl_->raceFactions; }
namespace {
int factionRelation(const LocalFactionTemplate& from, const LocalFactionTemplate& to) {
    if (from.id == to.id) return -1;
    if (to.faction) {
        for (auto enemy : from.enemies) if (enemy == to.faction) return 1;
        for (auto friendId : from.friends) if (friendId == to.faction) return -1;
    }
    if (from.enemyGroup & to.factionGroup) return 1;
    if ((from.friendGroup & to.factionGroup) || (from.factionGroup & to.friendGroup)) return -1;
    return 0;
}
}
LocalGameplay::NpcDisposition LocalGameplay::npcDisposition(
    const LocalRealmPlayer& p, const LocalRealmNpc& n) const {
    const auto* d = content().npc(n.entry);
    if (!d || (d->unitFlags & (0x2U | 0x100U | 0x02000000U))) return {};
    const auto* npcFaction = definition(impl_->factions, d->faction);
    const auto* playerFaction = p.race < impl_->raceFactions.size() ? definition(impl_->factions, impl_->raceFactions[p.race]) : nullptr;
    if (!npcFaction || !playerFaction) {
        const bool attackable = n.hostile && !n.questGiver;
        return {attackable, attackable && d->aggroRadius > 0};
    }
    const int relation = factionRelation(*npcFaction, *playerFaction);
    if (relation > 0) return {true, true};
    if (relation < 0 || factionRelation(*playerFaction, *npcFaction) < 0) return {};
    // Neutral mobs can be attacked; quest contacts remain protected unless
    // their actual faction is hostile to this race.
    return {!n.questGiver, false};
}
bool LocalGameplay::canAttack(const LocalRealmPlayer& p, const LocalRealmNpc& n) const {
    return npcDisposition(p, n).attackable;
}
bool LocalGameplay::isAggressive(const LocalRealmPlayer& p, const LocalRealmNpc& n) const {
    return npcDisposition(p, n).aggressive;
}

bool LocalGameplay::loadContent(const std::string& path,std::string& error) {
    try {
        std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("Cannot open "+path);
        const auto length=f.tellg();if(length<=0||length>32*1024*1024)throw std::runtime_error("World content must be 1 byte to 32 MiB");
        std::string text(size_t(length),'\0');f.seekg(0);if(!f.read(text.data(),length))throw std::runtime_error("Cannot read world content");
        const Json j=Json::parse(text);if(!j.is_object()||number(j,"schemaVersion",0)!=1)throw std::runtime_error("Unsupported local world schemaVersion");
        auto c=std::make_shared<LocalWorldContent>();c->sourcePath=path;c->classResources=j.value("classResources",false);c->fingerprint=2166136261U;
        for(unsigned char b:text)c->fingerprint=(c->fingerprint^b)*16777619U;
        std::set<uint32_t> seen;
        for(const auto& v:array(j,"items",16384)) {
            LocalItemDefinition d;d.id=number(v,"id",0,UINT32_MAX);requiredId(d.id,seen,"item");d.name=label(v,"name",96);
            d.displayId=number(v,"displayId",0);d.slot=uint8_t(number(v,"slot",0,4));d.inventoryType=uint8_t(number(v,"inventoryType",0,28));d.stack=uint16_t(number(v,"stack",1,65535));if(!d.stack)throw std::runtime_error("Zero item stack size");
            d.maxHealth=number(v,"maxHealth",0,10000);d.attack=number(v,"attack",0,10000);d.armor=number(v,"armor",0,10000);d.heal=number(v,"heal",0,100000);d.mana=number(v,"mana",0,100000);d.value=number(v,"value",0);c->items.push_back(std::move(d));
        }
        seen.clear();for(const auto& v:array(j,"spells",4096)) {
            LocalSpellDefinition d;d.id=number(v,"id",0,UINT32_MAX);requiredId(d.id,seen,"spell");d.name=label(v,"name",96);
            d.resourceType=uint8_t(number(v,"resourceType",255,255));d.allowableClasses=number(v,"allowableClasses",0,UINT32_MAX);
            d.mana=number(v,"mana",0,100000);d.cooldownMs=number(v,"cooldownMs",1000,3600000);d.range=real(v,"range",30,0,60);d.damage=number(v,"damage",0,100000);d.heal=number(v,"heal",0,100000);
            if(!d.damage&&!d.heal)throw std::runtime_error("Spell needs damage or heal");
            c->spells.push_back(std::move(d));
        }
        seen.clear();for(const auto& v:array(j,"npcs",16384)) {
            LocalNpcDefinition d;d.id=number(v,"id",0,UINT32_MAX);requiredId(d.id,seen,"NPC");d.name=label(v,"name",96);d.displayId=number(v,"displayId",0);
            d.unitFlags=number(v,"unitFlags",0,UINT32_MAX);d.faction=number(v,"faction",0,UINT32_MAX);d.level=uint8_t(number(v,"level",1,83));d.health=number(v,"health",40,1000000000);if(!d.health||!d.level)throw std::runtime_error("Invalid NPC health/level");
            d.damage=number(v,"damage",4,100000);d.armor=number(v,"armor",0,10000);d.xp=number(v,"xp",50,1000000);d.money=number(v,"money",0,1000000);
            d.gossipText=label(v,"gossipText",4096,false);d.subname=label(v,"subname",128,false);
            // Same two optional server fields the catalog reader takes; see the
            // note there about the fallback these supersede.
            d.npcFlags=number(v,"npcFlags",0,UINT32_MAX);
            d.trainerSkill=uint16_t(number(v,"trainerSkill",0,65535));d.trainerClass=uint8_t(number(v,"trainerClass",0,11));
            // P04: the same two optional immunity fields and the six-school
            // resistance row the catalog reader takes, zero when absent.
            d.immuneSchoolMask=uint8_t(number(v,"immuneSchoolMask",0,127));
            d.immuneMechanicsMask=wide(v,"immuneMechanicsMask",0);
            if(v.contains("resistances")) {
                const auto& r=array(v,"resistances",6);
                if(r.size()!=6)throw std::runtime_error("NPC resistances need six schools");
                for(size_t i=0;i<6;++i) {
                    if(!r[i].is_number_integer()||r[i].get<int64_t>()<0||r[i].get<uint64_t>()>65535)throw std::runtime_error("Invalid NPC resistance");
                    d.resistances[i]=uint16_t(r[i].get<uint32_t>());
                }
            }
            // P05 : the same two optional reach fields the catalog
            // reader takes, zero when absent.
            d.combatReach=real(v,"combatReach",0,0,1000);
            d.boundingRadius=real(v,"boundingRadius",0,0,1000);
            for(const auto& id:array(v,"vendor",256)) {
                if(!id.is_number_integer()||id.get<int64_t>()<1||id.get<uint64_t>()>UINT32_MAX)throw std::runtime_error("Invalid vendor item");
                d.vendorItems.push_back(id.get<uint32_t>());
            }
            d.hostile=v.value("hostile",false);d.questGiver=v.value("questGiver",false);d.respawnSeconds=real(v,"respawnSeconds",30,1,86400);d.aggroRadius=real(v,"aggroRadius",0,0,60);
            for(const auto& stack:array(v,"loot",8))d.loot.push_back(parseStack(stack));
            c->npcs.push_back(std::move(d));
        }
        seen.clear();for(const auto& v:array(j,"quests",16384)) {
            LocalQuestDefinition d;d.id=number(v,"id",0,UINT32_MAX);requiredId(d.id,seen,"quest");d.title=label(v,"title",96);d.description=label(v,"description",1024,false);
            d.allowableRaces=number(v,"allowableRaces",0,UINT32_MAX);d.allowableClasses=number(v,"allowableClasses",0,UINT32_MAX);d.requiredSkill=number(v,"requiredSkill",0,UINT32_MAX);
            d.giverEntry=number(v,"giverEntry",0,UINT32_MAX);d.turnInEntry=number(v,"turnInEntry",d.giverEntry,UINT32_MAX);d.minLevel=uint8_t(number(v,"minLevel",1,80));d.prerequisite=number(v,"prerequisite",0,UINT32_MAX);
            d.xp=number(v,"xp",0,1000000);d.money=number(v,"money",0,1000000);d.rewardItem=number(v,"rewardItem",0,UINT32_MAX);d.rewardCount=uint16_t(number(v,"rewardCount",d.rewardItem?1:0,65535));
            for(const auto& r:array(v,"additionalRewards",3))d.additionalRewards.push_back(parseStack(r));
            for(const auto& r:array(v,"rewardChoices",6))d.rewardChoices.push_back(parseStack(r));
            if(!validLocalQuestRewards(d))throw std::runtime_error("Invalid quest reward bundle");
            for(const auto& objective:array(v,"objectives",4)) {
                LocalQuestObjective o;const auto type=label(objective,"type",16);
                if(type=="kill")o.type=LocalQuestObjective::Type::Kill;else if(type=="collect")o.type=LocalQuestObjective::Type::Collect;else if(type=="talk")o.type=LocalQuestObjective::Type::Talk;else throw std::runtime_error("Unknown quest objective type");
                o.entry=number(objective,"entry",0,UINT32_MAX);o.count=uint16_t(number(objective,"count",1,65535));if(!o.count)throw std::runtime_error("Zero objective count");d.objectives.push_back(o);
            }
            c->quests.push_back(std::move(d));
        }
        seen.clear();for(const auto& v:array(j,"spawns",65536)) {
            LocalNpcSpawn d;d.id=number(v,"id",0,UINT32_MAX);requiredId(d.id,seen,"spawn");d.entry=number(v,"entry",0,UINT32_MAX);d.mapId=number(v,"mapId",0,10000);
            d.x=real(v,"x",0,-100000,100000);d.y=real(v,"y",0,-100000,100000);d.z=real(v,"z",0,-20000,20000);d.orientation=real(v,"orientation",0,-100000,100000);c->spawns.push_back(d);
        }
        if(j.contains("start")) {
            const auto& v=j.at("start");c->start.mapId=number(v,"mapId",0,10000);c->start.x=real(v,"x",c->start.x,-100000,100000);c->start.y=real(v,"y",c->start.y,-100000,100000);c->start.z=real(v,"z",c->start.z,-20000,20000);c->start.orientation=real(v,"orientation",0,-100000,100000);
            for(const auto& stack:array(v,"items",MaxInventory))c->start.inventory.push_back(parseStack(stack));
            for (const auto& spell : array(v, "spells", MaxSpells)) {
                if (!spell.is_number_integer() || spell.get<int64_t>() <= 0 || spell.get<uint64_t>() > UINT32_MAX)
                    throw std::runtime_error("Invalid starter spell");
                c->start.knownSpells.push_back(spell.get<uint32_t>());
            }
        }
        const auto byId = [](const auto& a, const auto& b) { return a.id < b.id; };
        std::sort(c->items.begin(), c->items.end(), byId);
        std::sort(c->spells.begin(), c->spells.end(), byId);
        std::sort(c->quests.begin(), c->quests.end(), byId);
        std::sort(c->npcs.begin(), c->npcs.end(), byId);
        for(const auto& d:c->npcs) {
            for(const auto& s:d.loot)if(!c->item(s.itemId))throw std::runtime_error("Loot references missing item");
            for(auto id:d.vendorItems)if(!c->item(id))throw std::runtime_error("Vendor list references missing item");
        }
        for(const auto& s:c->spawns)if(!c->npc(s.entry))throw std::runtime_error("Spawn references missing NPC");
        for(const auto& d:c->quests) {
            if(!c->npc(d.giverEntry)||!c->npc(d.turnInEntry)||(d.prerequisite&&!c->quest(d.prerequisite)))throw std::runtime_error("Quest references missing NPC/prerequisite");
            if((d.rewardItem&&!c->item(d.rewardItem))||(!d.rewardItem&&d.rewardCount))throw std::runtime_error("Quest reward item missing");
            for(const auto& r:d.additionalRewards)if(!c->item(r.itemId))throw std::runtime_error("Additional quest reward item missing");
            for(const auto& r:d.rewardChoices)if(!c->item(r.itemId))throw std::runtime_error("Quest choice item missing");
            std::set<std::pair<unsigned,uint32_t>> objectives;
            for(const auto& o:d.objectives) {
                if(o.type==LocalQuestObjective::Type::Collect?!c->item(o.entry):!c->npc(o.entry))throw std::runtime_error("Quest objective reference missing");
                if(!objectives.emplace(unsigned(o.type),o.entry).second)throw std::runtime_error("Duplicate quest objective");
            }
            std::set<uint32_t> chain;const LocalQuestDefinition* q=&d;
            while(q&&q->prerequisite){if(!chain.insert(q->id).second)throw std::runtime_error("Quest prerequisite cycle");q=c->quest(q->prerequisite);}
        }
        for(const auto& s:c->start.inventory)if(!c->item(s.itemId)||s.count>c->item(s.itemId)->stack)throw std::runtime_error("Invalid starter inventory");
        std::set<uint32_t> learned;for(auto id:c->start.knownSpells)if(!c->spell(id)||!learned.insert(id).second)throw std::runtime_error("Invalid starter spells");
        useContent(std::move(c));
        const auto catalogDirectory = std::filesystem::path(path).parent_path() / "catalog";
        std::error_code ec;
        if (std::filesystem::is_directory(catalogDirectory, ec) && !loadCatalog(catalogDirectory.string(), error)) return false;
        // P05 line of sight . Optional, and deliberately not fatal: a
        // console with no collision pack installed runs exactly as the implementation did,
        // with every line-of-sight test answering "visible". The reason is
        // logged rather than returned so a missing pack cannot stop a boot.
        const auto collisionDirectory = std::filesystem::path(path).parent_path() / "collision";
        if (std::filesystem::is_directory(collisionDirectory, ec)) {
            std::string collisionError;
            if (!loadCollision(collisionDirectory.string(), collisionError))
                LOG_INFO("[LOCAL_COLLISION] not installed: ", collisionError);
        } else {
            LOG_INFO("[LOCAL_COLLISION] no collision pack at ", collisionDirectory.string(),
                     "; every line-of-sight test answers visible");
        }
        error.clear();return true;
    } catch(const std::exception& e){error="Local world content: "+std::string(e.what());return false;}
}

bool LocalGameplay::validatePlayer(const LocalRealmPlayer& p, std::string& error) const {
    if(!validLocalFormState(p)){error="Invalid shapeshift state";return false;}
    if(p.manaRegenDelayMs>5000||p.resourceRegenRemainder>=1000){error="Invalid resource regeneration state";return false;}
    if(p.ridingSkill!=0 && p.ridingSkill!=75 && p.ridingSkill!=150){error="Invalid riding skill";return false;}
    if (!validCharacterOptions(p.race, p.classId, p.gender)) { error = "Invalid saved race/class profile"; return false; }
    if (p.instanceId) {
        bool found = false;
        for (const auto& i : impl_->instances) if (i.id == p.instanceId && i.mapId == p.mapId) found = true;
        if (!found) { error = "Saved character references a missing instance binding; save preserved"; return false; }
    }
    if(p.hasInstanceReturn && p.returnInstanceId &&
        (p.returnInstanceId==p.instanceId || std::none_of(impl_->instances.begin(),impl_->instances.end(),
            [&](const auto& i){return i.id==p.returnInstanceId && i.mapId==p.returnMapId;}))) {
        error="Saved character references an invalid instance return binding; save preserved";return false;
    }
    if (!p.gameplayInitialized) return true; // B1 is migrated with its original position/identity.
    const auto& c = content();
    const auto invalid = [&](const std::string& reason) { error = "Saved character " + p.name + " is incompatible with world content: " + reason + ". Original save preserved."; return false; };
    if (p.inventory.size() > MaxInventory || p.quests.size() > MaxQuests || p.knownSpells.size() > MaxSpells ||
        p.knownRecipes.size() > MaxRecipes || p.cooldowns.size() > MaxCooldowns || !validLocalCategoryCooldowns(p)) return invalid("collection limit exceeded");
    for (const auto& s : p.inventory) {
        const auto* d = c.item(s.itemId);
        if (!d || !s.count || s.count > d->stack) return invalid("unknown item or changed stack limit for item " + std::to_string(s.itemId));
    }
    for (const auto& s : p.bank) {
        if (!s.itemId && !s.count) continue;
        const auto* item = c.item(s.itemId);
        if (!item || !s.count || s.count > item->stack) return invalid("invalid bank stack");
    }
    if (p.buyback.size() > kLocalMaxBuyback) return invalid("buyback limit exceeded");
    uint64_t previousBuyback = uint64_t(p.buybackSerial) + 1;
    for (const auto& row : p.buyback) {
        if (!row.id || row.id >= previousBuyback || !row.count || row.price > 1000000000 || !c.item(row.itemId))
            return invalid("invalid buyback entry");
        previousBuyback = row.id;
    }
    if (!validEquipment(p, c)) return invalid("equipment slot, owned copy count, or two-handed weapon conflict");
    if (p.professions.size() > MaxProfessions) return invalid("profession limit exceeded");
    std::set<uint16_t> professionIds;
    for (const auto& skill : p.professions) {
        if (!skill.skillId || !professionIds.insert(skill.skillId).second) return invalid("duplicate or zero profession");
        // A cap the rank ladder never issues, or points beyond it, is a
        // corrupted or hand-edited save rather than a rank this realm sold.
        const auto& ranks = localProfessionRanks();
        if (std::none_of(ranks.begin(), ranks.end(), [&](const auto& r) { return r.cap == skill.max; }))
            return invalid("profession rank cap " + std::to_string(skill.max) + " was never trainable");
        if (!skill.current || skill.current > skill.max) return invalid("profession skill exceeds its rank cap");
        if (skill.progress >= 1000) return invalid("profession skill progress out of range");
    }
    uint32_t previousRecipe = 0;
    for (const auto id : p.knownRecipes) {
        // Sorted and unique, so a corrupt list cannot hide a duplicate that
        // would let one recipe be crafted from two entries.
        if (!id || id <= previousRecipe) return invalid("recipe ids must be nonzero, sorted and unique");
        previousRecipe = id;
        const auto* recipe = c.recipe(id);
        if (!recipe) return invalid("learned recipe missing from client data: " + std::to_string(id));
        if (std::none_of(p.professions.begin(), p.professions.end(),
                         [&](const LocalProfessionSkill& s) { return s.skillId == recipe->skillId; }))
            return invalid("recipe " + std::to_string(id) + " belongs to a profession this character does not have");
    }
    if (p.hasHome && (p.homeMapId > 10000 || !std::isfinite(p.homeX) || !std::isfinite(p.homeY) ||
                      !std::isfinite(p.homeZ) || !std::isfinite(p.homeOrientation)))
        return invalid("home binding position");
    if (!std::isfinite(p.hearthCooldown) || p.hearthCooldown < 0 || p.hearthCooldown > HearthCooldownSeconds)
        return invalid("home cooldown out of range");
    if(!validLocalRunes(p.runeCooldownMs))return invalid("Rune recharge exceeds ten seconds");
    for (const auto id : p.knownSpells) if (!c.spell(id)) return invalid("learned spell missing: " + std::to_string(id));
    if (p.completedQuestIds.size() > MaxCompletedQuests) return invalid("completed quest history limit exceeded");
    uint32_t previousQuest = 0;
    for (const auto id : p.completedQuestIds) {
        if (!id || id <= previousQuest) return invalid("completed quest IDs must be nonzero, sorted and unique");
        previousQuest = id;
    }
    std::set<uint32_t> activeQuestIds;
    for (const auto& q : p.quests) {
        if (!q.id || !activeQuestIds.insert(q.id).second || questRewarded(p, q.id)) return invalid("duplicate active or already rewarded quest");
        if (q.status != LocalQuestStatus::Active && q.status != LocalQuestStatus::Complete) return invalid("invalid active quest status");
        const auto* d = c.quest(q.id);
        if (!d || d->objectives.size() != q.progress.size()) return invalid("quest/objectives changed: " + std::to_string(q.id));
        for (size_t index = 0; index < q.progress.size(); ++index) if (q.progress[index] > d->objectives[index].count) return invalid("quest target count reduced");
    }
    return true;
}

void LocalGameplay::initializePlayer(LocalRealmPlayer& p, bool fresh, uint8_t forcedLevel) {
    const auto previousPeriods=meleePeriods(p,content());
    p.healingAuras.clear();
    // These fractions/cadence are authority-session state, unlike the saved
    // thousandths and five-second-rule delay. Reinitializing a reused player
    // must behave like loading the same player into a fresh authority object.
    p.regenerationTickMs=p.manaRegenSubMilli=0;
    const auto& c = content();
    migrateLocalCategoryCooldowns(p,c);
    std::erase_if(p.knownSpells,[&](auto id){const auto* spell=c.spell(id);return spell&&(spell->triggeredOnly||spell->npcOnly);});
    if(const auto dropped=normalizeLocalKnownRanks(p,c))
        LOG_INFO("[LOCAL_RANKS] player=",p.guid," dropped ",dropped," outranked spellbook entries (spell_ranks chains, 2.00)");
    if(p.migrateLegacyRiding){
        for(auto id:p.knownSpells)if(const auto* spell=c.spell(id);spell && spell->mountDisplayId && spell->unsupportedReason.empty())
            p.ridingSkill=std::max(p.ridingSkill,localMountRidingRequirement(*spell));
        p.migrateLegacyRiding=false;
        LOG_INFO("[LOCAL_RIDING] migrated player=",p.guid," skill=",p.ridingSkill);
    }
    p.druidManaCapacity=p.classId==11?localResourcePools(p,c).mana:0;
    const auto oldResource = p.resourceType;
    if(p.formSpellId) {
        const auto* f=localFormProfile(p.formSpellId);
        const auto* formSpell=c.spell(p.formSpellId);
        if(!f||f->clazz!=p.classId||!formSpell||!formSpell->unsupportedReason.empty()||formSpell->formId!=f->form||!localFormEnvironmentReady(p,*formSpell)||p.dead||std::find(p.knownSpells.begin(),p.knownSpells.end(),p.formSpellId)==p.knownSpells.end())leaveLocalForm(p);
    }
    if (c.classResources) p.resourceType = localActiveForm(p)?localActiveForm(p)->power:classResource(p.classId);
    if (!p.gameplayInitialized) {
        if (fresh) {
            p.mapId = c.start.mapId; p.x = c.start.x; p.y = c.start.y; p.z = c.start.z; p.orientation = c.start.orientation;
            if (c.catalog) for (const auto& start : c.catalog->starts()) if (start.race == p.race && start.classId == p.classId) {
                p.mapId = start.mapId; p.x = start.x; p.y = start.y; p.z = start.z;
                p.orientation = start.orientation; p.level = start.level;
                break;
            }
            // A forced level belongs here, above the spellbook, the resource
            // type and the pools, because every one of them is derived from
            // p.level a few lines further down: the class progression loop
            // below tests spellUnlockLevel() <= p.level, stats() reads the
            // level-indexed base health/mana rows, and the experience needed
            // for the next level is p.level squared. Setting the level after
            // initialization would leave all of that at the catalog's level.
            if (forcedLevel) p.level = forcedLevel;
        }
        p.inventory = c.start.inventory;
        // The base pack declares shared local starter supplies; it is not a
        // CharStartOutfit.dbc or a claim of original gear for every class.
        p.knownSpells.clear();
        if(c.clientStarterSpells) {
            // Only what this character has already reached, and only the
            // highest rank of it. Handing out an ability the client's own
            // Spell.dbc dates to a later level was invisible while every
            // imported spell was a level-one starter; it is also exactly what a
            // class trainer exists to sell.
            for(const auto& spell:c.spells) if(spell.clientSpell && !spell.npcOnly && !spell.triggeredOnly && !spell.mountDisplayId && !spell.talentId && localProcTalentPrerequisite(p,c,spell) &&
                (spell.allowableClasses & (1u << (p.classId-1))) && spellUnlockLevel(spell) <= p.level &&
                !supercededHere(c, spell, p.level) && p.knownSpells.size()<MaxSpells)
                p.knownSpells.push_back(spell.id);
        } else for (auto id : c.start.knownSpells) if (const auto* spell = c.spell(id))
            if (!spell->npcOnly && !spell->triggeredOnly && (!spell->allowableClasses || (spell->allowableClasses & (1u << (p.classId - 1))))) p.knownSpells.push_back(id);
        p.equipment.fill(0);
        for (const auto& stack : p.inventory) if (const auto* d = c.item(stack.itemId)) {
            const uint32_t mask = localEquipmentSlotMask(d->inventoryType, d->slot);
            for (size_t slot = 0; slot < p.equipment.size(); ++slot)
                if ((mask & localEquipmentSlotBit(slot)) && !p.equipment[slot]) {
                    equipItem(p, c, d->id, slot + 1);
                    break;
                }
        }
        p.dead = false; p.gameplayInitialized = true; stats(p, c, true);
    } else {
        // Older imports confused proc chance with stack count. Normalize saved
        // aura state before publishing owner icons or deriving login stats.
        for(auto& aura:p.statAuras) {
            const auto* d=c.spell(aura.spellId);
            if(p.dead||!d||!d->unsupportedReason.empty()||d->passive||!d->maxAuraStacks||
               !localHasTimedAura(*d)||!validLocalProc(*d)||aura.mapId!=p.mapId||aura.instanceId!=p.instanceId||
               !localProcChildTalentReady(p,c,*d)||!localTimedDamageTalentReady(p,c,*d)||(d->procParentTalentId&&aura.casterGuid&&aura.casterGuid!=p.guid)||
               (d->arcaneBlastProfile==2&&(p.classId!=8||aura.casterGuid!=p.guid)))
                aura.remainingMs=0;
            else {
                // Internal temporary chains cannot outlive their source
                // duration after loading; never refill a shorter saved timer.
                if(d->triggeredOnly||d->physicalDamageDonePct||d->damageTakenPct)aura.remainingMs=std::min(aura.remainingMs,d->durationMs);
                if(d->procParentTalentId){
                    aura.casterGuid=p.guid;aura.procAmountSnapshot=d->proc.amount;aura.hasProcAmountSnapshot=true;
                }
                hydrateLegacyLocalProcAmount(aura,p,*d);
                aura.stacks=std::min(aura.stacks,d->maxAuraStacks);
                aura.absorbRemaining=std::min(aura.absorbRemaining,d->wardProfile?1000000u:localStackedAuraAmount(d->buffAbsorb,aura.stacks));
                if(d->buffAbsorb&&!aura.absorbRemaining)aura.remainingMs=0;
                aura.procCharges=std::min(aura.procCharges,d->proc.charges);
                aura.procCooldownMs=std::min(aura.procCooldownMs,d->proc.cooldownMs);
                if(d->proc.charges&&!aura.procCharges)aura.remainingMs=0;
                if(d->periodicHealMaxHealthPct)aura.manaRegenRemainder%=d->periodicIntervalMs;
                else if(!d->manaPer5)aura.manaRegenRemainder=0;
                aura.buffArmorSnapshot=d->buffArmor?std::min(aura.buffArmorSnapshot,uint32_t(std::min(uint64_t(1000000),uint64_t(d->buffArmor)*11))):0;
            }
        }
        std::erase_if(p.statAuras,[](const auto& aura){return !aura.remainingMs;});
        normalizeLocalElementalShields(p,c);
        normalizeLocalMageArmors(p,c);
        normalizeLocalCompetingAuras(p,c);
        stats(p, c, false);
        if(p.resourceType!=oldResource&&p.resourceType!=LocalResourceType::Mana){
            p.mana=0;p.resourceRegenRemainder=0;
        }
        // Returning from an invalid/retired saved form keeps restored caster mana;
        // login migration is not a free refill of the new maximum.
    }
    clearCast(p,LocalCastStatus::None);p.globalCooldownMs=0;clearLocalCombo(p);
    if(p.dead||!p.health)p.manaRegenDelayMs=p.resourceRegenRemainder=p.druidManaRemainder=0;
    p.castRevision=0;p.lastCastSpellId=0;p.lastCastTarget=0;
    localStopRangedAuto(p);p.rangedRemainingMs=0;p.attackTarget = 0; p.attackTimer = 0; p.deadTimer = 0;p.offHandTimer=0;p.meleeWeaponMain=p.meleeWeaponOff=p.meleeForm=0;p.meleeViews={};p.meleeSerial=0;
    // A character that logs in, is created or is migrated is not mid-fall,
    // whatever the last thing to touch these was.
    p.mountSpellId = 0;
    p.movementState = 0; p.falling = false; p.fallStartZ = p.z; p.fallRevision = p.positionRevision;
    questStatus(p, c);
    localRescaleMeleeTimers(p,previousPeriods[0],previousPeriods[1],c);
}

bool LocalGameplay::execute(LocalRealmPlayer& p,const LocalRealmCommand& cmd,const std::vector<LocalRealmPlayer*>& players,std::string& result) {
    auto& g=*impl_;const auto& c=content();
    // Takes a string rather than a literal: the merchant and trainer refusals
    // below have to name the item, profession or rank they are refusing, and a
    // refusal a player cannot act on is barely better than none.
    const auto reject=[&](const std::string& reason){result=reason;return false;};
    if(cmd.action==LocalAction::CancelStatAura){
        if(cmd.target||cmd.bid||!cmd.id)return reject("Invalid buff cancellation");
        const auto periods=meleePeriods(p,c);
        const auto statsRemoved=std::erase_if(p.statAuras,[&](const auto& a){return a.spellId==cmd.id;});
        const auto healsRemoved=std::erase_if(g.periodicHeals,[&](const auto& a){return a.target==p.guid&&a.spell==cmd.id;});
        // An area aura is cancelled at its emitter. Removing a derived
        // application does not cancel anything: the reference never removes an
        // area aura from its original owner, because that cancels the whole
        // aura (Unit.cpp:4329). The next pass rebuilds every recipient.
        const auto emittersRemoved=std::erase_if(p.areaEmitters,[&](const auto& e){return e.spellId==cmd.id;});
        if(emittersRemoved)g.reconcileAreaAuras(players);
        if(!statsRemoved&&!healsRemoved&&!emittersRemoved)return reject("Buff is not active");
        g.refreshHealingViews(players);
        stats(p,c,false);localRescaleMeleeTimers(p,periods[0],periods[1],c);result="Buff removed";return true;
    }
    if(cmd.action==LocalAction::LearnTalent || cmd.action==LocalAction::ResetTalents){
        if(p.dead || p.flight.active || p.transportEntry || p.castingSpellId || p.attackTarget || cmd.target)return reject("Talents require a living character outside combat and travel");
        if(localCombatActive(p,g.npcs))return reject("Cannot change talents in combat");
        if(cmd.action==LocalAction::LearnTalent){
            const auto periods=meleePeriods(p,c);
            if(!learnLocalTalent(p,c,cmd.id,cmd.bid,result))return false;
            std::erase_if(p.statAuras,[&](const auto& a){const auto* s=c.spell(a.spellId);return s&&s->procParentTalentId&&!localProcChildTalentReady(p,c,*s);});
            localRescaleMeleeTimers(p,periods[0],periods[1],c);
        }else{
            if(cmd.id || cmd.bid || p.talents.empty())return reject("No talents to reset");
            std::erase_if(p.knownSpells,[&](auto id){const auto* s=c.spell(id);return s && (s->talentId||localEarthShield(*s)||s->meleeSpecialProfile==1);});
            for(auto* recipient:g.auraOwners(players))if(recipient) {
                const auto periods=meleePeriods(*recipient,c);
                std::erase_if(recipient->statAuras,[&](const auto& a){const auto* s=c.spell(a.spellId);return s&&(localEarthShield(*s)||s->proc.effect==LocalProcEffect::HealOwnerPctMaxHealth||s->procParentTalentId||s->physicalDamageDonePct||s->damageTakenPct)&&(a.casterGuid?a.casterGuid:recipient->guid)==p.guid;});
                localRescaleMeleeTimers(*recipient,periods[0],periods[1],c);
            }
            p.talents.clear();result="Talents reset (local testing: no gold charge)";
        }
        stats(p,c,false);return true;
    }
    if (cmd.action == LocalAction::CompleteIntro) {
        if (cmd.target || cmd.id) return reject("Intro completion applies only to your character");
        p.introSeen = true;
        result.clear();
        return true;
    }
    if(cmd.action==LocalAction::Dismount) {
        if(cmd.target || cmd.id)return reject("Invalid dismount request");
        p.mountSpellId=0;result="Dismounted";return true;
    }
    if(cmd.action==LocalAction::CancelForm){
        if(cmd.id&&cmd.id!=p.formSpellId)return reject("Form changed; refresh the stance bar");
        if((p.classId!=11&&p.classId!=7)||!p.formSpellId)return reject("No cancellable form");
        leaveLocalForm(p);clearCast(p,LocalCastStatus::Interrupted);stats(p,c,false);result="Returned to caster form";return true;
    }
    if(cmd.action==LocalAction::DismissPet){
        auto* summon=g.controlledPetOf(p.guid);
        if(!summon)return reject("No active summon");
        if(cmd.target&&cmd.target!=summon->guid)return reject("Summon changed; refresh the pet bar");
        g.retirePet(summon->guid,"dismissed",players);result="Summon dismissed";return true;
    }
    // P07  CMSG_PET_ACTION. WorldSession::HandlePetAction/HandlePetActionHelper
    // (PetHandler.cpp:60-330): the pet has to be the caster's own, the packed
    // word's high byte says what kind of action it is, and an id outside the
    // enumeration falls into the reference's `default:` and is refused.
    if(cmd.action==LocalAction::PetSpellAutocast){
        auto* summon=g.controlledPetOf(p.guid);
        if(!summon||cmd.target!=summon->guid)return reject("Summon changed; refresh the pet bar");
        if(!localPetOwnerEligible(*summon,&p))return reject("Your summon cannot act while its owner is unavailable");
        if(summon->dead||!summon->health)return reject("Your summon is dead");
        if(cmd.bid>1||cmd.id!=localPetFireboltSpell(summon->entry,summon->level)||!cmd.id||!c.spell(cmd.id))
            return reject("Invalid summon autocast action");
        summon->fireboltAutocast=cmd.bid!=0;
        result=summon->fireboltAutocast?"Firebolt autocast enabled":"Firebolt autocast disabled";
        return true;
    }
    if(cmd.action==LocalAction::PetAction){
        auto* summon=g.controlledPetOf(p.guid);
        if(!summon)return reject("No active summon");
        // CMSG_PET_ACTION carries TWO guids: guid1 names the pet and guid2 is
        // the action's own target, which only COMMAND_ATTACK reads
        // (PetHandler.cpp:60-104, :219). LocalRealmCommand's `target` is guid1,
        // exactly as DismissPet uses it, and `serviceNpcGuid` carries guid2.
        // HandlePetAction checks guid1 against the caster's own pet before it
        // looks at the action at all.
        if(cmd.target&&cmd.target!=summon->guid)return reject("Summon changed; refresh the pet bar");
        const auto type=pet::petActionType(cmd.id);
        const auto action=pet::petActionId(cmd.id);
        if(!pet::validPetActionId(type,action))return reject("Unknown pet action");
        // "if ((flag != ACT_COMMAND || spellId != COMMAND_ABANDON) && !ALLOW_CAST_WHILE_DEAD)"
        // - a dead pet accepts nothing but its own dismissal.
        const bool abandon=type==pet::ActionType::Command&&action==pet::kAbandon;
        if(summon->dead&&!abandon)return reject("Your summon is dead");
        if(type!=pet::ActionType::Command&&type!=pet::ActionType::Reaction){
            if(cmd.target!=summon->guid)return reject("Summon changed; refresh the pet bar");
            if(type!=pet::ActionType::Enabled&&type!=pet::ActionType::Disabled)return reject("Unknown summon spell action");
            auto* victim=g.npc(cmd.serviceNpcGuid?cmd.serviceNpcGuid:p.attackTarget);
            if(!victim||!canAttack(p,*victim))return reject("Nothing for your summon to attack");
            if(!g.startPetSpell(*summon,p,*victim,action,result))return reject(result);
            return true;
        }
        if(type==pet::ActionType::Command){
            switch(action){
                case pet::kStay:
                    // StopMovingOnCurrentPos + MoveIdle + SetCommandState(STAY)
                    // + SetIsCommandAttack(false) + SaveStayPosition: the pet
                    // stops where it is and that point becomes its anchor.
                    g.petCasts.erase(summon->guid);
                    summon->command=LocalPetCommand::Stay;summon->commandAttack=false;
                    summon->stayX=summon->x;summon->stayY=summon->y;summon->stayZ=summon->z;
                    result="Summon holding position";break;
                case pet::kFollow:
                    // AttackStop + ClearInPetCombat + MoveFollow +
                    // SetCommandState(FOLLOW) + SetIsCommandAttack(false) +
                    // RemoveStayPosition.
                    g.petCasts.erase(summon->guid);
                    summon->command=LocalPetCommand::Follow;
                    summon->targetGuid=0;summon->commandAttack=false;
                    summon->stayX=summon->stayY=summon->stayZ=0;
                    result="Summon following";break;
                case pet::kAttack:{
                    // Needs a real, attackable target the OWNER could attack:
                    // "if (Unit* owner = pet->GetOwner()) if
                    // (!owner->IsValidAttackTarget(TargetUnit)) return;"
                    // guid2, else the owner's own victim - which is what the
                    // pet attack button sends when nothing else is named.
                    auto* victim=g.npc(cmd.serviceNpcGuid);
                    if(!victim)victim=g.npc(p.attackTarget);
                    if(!victim||victim->dead||!victim->health||victim->transportEntry||
                       victim->mapId!=summon->mapId||victim->instanceId!=summon->instanceId||
                       !canAttack(p,*victim))return reject("Nothing for your summon to attack");
                    // COMMAND_ATTACK is NOT a command state: the reference sets
                    // SetIsCommandAttack(true) and leaves SetCommandState alone
                    // (PetHandler.cpp:209-275), which is why a staying pet told
                    // to attack goes back to its stay point afterwards.
                    if(const auto cast=g.petCasts.find(summon->guid);cast!=g.petCasts.end()&&cast->second.target!=victim->guid)g.petCasts.erase(cast);
                    summon->commandAttack=true;summon->targetGuid=victim->guid;
                    result="Summon attacking";break;
                }
                default:
                    // COMMAND_ABANDON. On a SUMMON_PET the reference does NOT
                    // unsummon: it plays the dismiss sound and calls
                    // setDeathState(Corpse) - "dismissing a summoned pet is
                    // like killing them" - and Pet::Update then removes a
                    // non-hunter corpse on its very next pass. Retiring the
                    // summon here is that same two-step collapsed into one, and
                    // it is what LocalAction::DismissPet has always done.
                    g.retirePet(summon->guid,"abandoned",players);
                    result="Summon dismissed";return true;
            }
        } else {
            // ACT_REACTION: Creature::SetReactState. REACT_PASSIVE additionally
            // stops the current attack (PetHandler.cpp:308-313).
            summon->react=LocalPetReact(action);
            if(summon->react==LocalPetReact::Passive){localPetStopAttack(*summon);g.petCasts.erase(summon->guid);}
            static constexpr const char* kStanceNames[]={"passive","defensive","aggressive"};
            result=std::string("Summon is now ")+kStanceNames[action];
        }
        LOG_INFO("[LOCAL_PET] owner=",p.guid," pet=",summon->guid," action=petaction type=",
            unsigned(uint8_t(type))," id=",action," command=",unsigned(uint8_t(summon->command)),
            " attack=",summon->commandAttack?1:0," react=",unsigned(uint8_t(summon->react)));
        return true;
    }
    auto* n=g.npc(cmd.target);
    if(cmd.action==LocalAction::ReclaimCorpse) {
        if(cmd.target || cmd.id || cmd.bid || cmd.buyout || cmd.durationMinutes || cmd.serviceNpcGuid)
            return reject("Reclaiming your corpse takes no arguments");
        if(!localCanReclaimCorpse(p))return reject("Move within 10 yards of your corpse");
        p.x=p.corpseX;p.y=p.corpseY;p.z=p.corpseZ;p.orientation=p.corpseOrientation;
        p.dead=false;p.ghost=false;p.corpseValid=false;p.deadTimer=0;
        p.attackTarget=0;p.portalCooldown=2;++p.positionRevision;finishLocalTeleport(p);
        stats(p,c,true);p.health=std::max(1u,p.maxHealth/2);p.mana=p.maxMana/2;
        g.regionTimer=1;result="Returned to your body";return true;
    }
    if(cmd.action==LocalAction::Respawn) {
        if(!p.dead)return reject("You are alive");
        if(p.ghost)return reject("Return to your corpse and reclaim your body");
        if(p.deadTimer<3)return reject("Your spirit is being released");
        localCaptureCorpse(p);
        // Use the entrance's continent for an instanced corpse, preserving
        // the original corpse map/instance and return binding for runback.
        const bool atEntrance=p.instanceId && p.hasInstanceReturn;
        const uint32_t sanctuaryMap=atEntrance?p.returnMapId:p.corpseMapId;
        const float originX=atEntrance?p.returnX:p.corpseX, originY=atEntrance?p.returnY:p.corpseY;
        const LocalGraveyardSite* best=nullptr;float bestDistance=std::numeric_limits<float>::max();
        const uint32_t raceBit=p.race>0&&p.race<=32 ? 1u<<(p.race-1) : 0;
        const bool haveZone = !atEntrance && p.corpseZoneId && std::any_of(g.graveyards.begin(),g.graveyards.end(),[&](const auto& site){
            return site.mapId==sanctuaryMap && site.zoneId==p.corpseZoneId && (!site.raceMask || (site.raceMask&raceBit));
        });
        for(const auto& site:g.graveyards) {
            if(haveZone && site.zoneId!=p.corpseZoneId)continue;
            if(site.mapId!=sanctuaryMap || (site.raceMask && !(site.raceMask&raceBit)))continue;
            const float dx=site.x-originX,dy=site.y-originY,distance=dx*dx+dy*dy;
            if(distance<bestDistance){best=&site;bestDistance=distance;}
        }
        if(best){p.mapId=best->mapId;p.instanceId=0;p.x=best->x;p.y=best->y;p.z=best->z;p.orientation=best->orientation;}
        else if(atEntrance){p.mapId=p.returnMapId;p.instanceId=p.returnInstanceId;p.x=p.returnX;p.y=p.returnY;p.z=p.returnZ;p.orientation=p.returnOrientation;}
        else if(p.hasHome && p.homeMapId==sanctuaryMap){p.mapId=p.homeMapId;p.instanceId=0;p.x=p.homeX;p.y=p.homeY;p.z=p.homeZ;p.orientation=p.homeOrientation;}
        else {
            p.instanceId=0;p.mapId=c.start.mapId;p.x=c.start.x;p.y=c.start.y;p.z=c.start.z;p.orientation=c.start.orientation;
            if(c.catalog)for(const auto& start:c.catalog->starts())if(start.race==p.race&&start.classId==p.classId){p.mapId=start.mapId;p.x=start.x;p.y=start.y;p.z=start.z;p.orientation=start.orientation;break;}
        }
        p.zoneId=0;p.ghost=true;p.health=0;p.mountSpellId=0;localStopRangedAuto(p);p.attackTarget=0;
        p.portalCooldown=2;++p.positionRevision;finishLocalTeleport(p);
        g.regionTimer=1;result="Return to your corpse and press Square to reclaim your body";return true;
    }
    if (cmd.action == LocalAction::AbandonQuest) {
        const auto progress = std::find_if(p.quests.begin(), p.quests.end(), [&](const LocalQuestProgress& q) { return q.id == cmd.id; });
        if (progress == p.quests.end() || questRewarded(p, cmd.id)) return reject("Quest is not in your active log");
        const auto* def = c.quest(cmd.id);
        p.quests.erase(progress);
        // Generic inventory items remain owned; abandoning only resets this
        // quest's tracked objective credit and never erases rewarded history.
        result = "Abandoned: " + (def ? def->title : std::to_string(cmd.id));
        return true;
    }
    if(p.dead && !(p.ghost && (cmd.action==LocalAction::EnterPortal || cmd.action==LocalAction::LeaveInstance)))return reject("Return to your corpse");
    if (cmd.action == LocalAction::LeaveInstance) {
        if(cmd.target || cmd.id)return reject("Leaving an instance takes no argument");
        if (!p.instanceId || !p.hasInstanceReturn) return reject("You are not inside a local instance");
        if(p.ghost && (!p.corpseValid || p.mapId!=p.corpseMapId || p.instanceId!=p.corpseInstanceId))
            return reject("This is not your corpse instance");
        if(p.flight.active || p.transportEntry)return reject("Leave your current transport before using an instance exit");
        if(p.returnInstanceId && (p.returnInstanceId==p.instanceId ||
            std::none_of(g.instances.begin(),g.instances.end(),[&](const auto& i){return i.id==p.returnInstanceId && i.mapId==p.returnMapId;})))
            return reject("The instance return binding is unavailable");
        p.mapId=p.returnMapId;p.instanceId=p.returnInstanceId;p.x=p.returnX;p.y=p.returnY;p.z=p.returnZ;p.orientation=p.returnOrientation;
        if(!p.ghost)p.hasInstanceReturn=false;
        p.attackTarget=0;p.portalCooldown=2;++p.positionRevision;
        finishLocalTeleport(p);g.regionTimer=1;
        result="Returned to the instance entrance";return true;
    }
    if (cmd.action == LocalAction::EnterPortal) {
        if (const auto* pad=localScriptedPortal(cmd.id)) {
            const auto* destination=localScriptedPortal(pad->destination);
            if (!destination || !insidePortal(cmd.id,p)) return reject("Stand on the Acherus teleport pad");
            if (p.portalCooldown>0 || p.flight.active || p.transportEntry)
                return reject("Teleport is not available while travelling");
            p.x=destination->x;p.y=destination->y;p.z=destination->arrivalZ();
            p.orientation=destination->orientation;p.attackTarget=0;
            p.portalCooldown=2;++p.positionRevision;g.regionTimer=1;
            finishLocalTeleport(p);
            g.portalExitLatch[p.guid]=destination->id;
            result=std::string("Travelled to ")+pad->name;
            LOG_INFO("[LOCAL_ACHERUS] pad=",pad->helperEntry," destination=",destination->helperEntry,
                " map=",p.mapId," xyz=",p.x,",",p.y,",",p.z);
            return true;
        }
        if (!c.catalog || !insidePortal(cmd.id, p)) return reject("Stand inside the actual entrance trigger");
        if (p.portalCooldown > 0) return reject("Wait before using another portal");
        if(p.flight.active || p.transportEntry)return reject("Leave your current transport before using a portal");
        if (cmd.target > 1) return reject("Invalid instance entry mode");
        const LocalCatalogDestination* destination=nullptr;
        for(const auto& d:c.catalog->destinations())if(d.id==cmd.id){destination=&d;break;}
        if(!destination)return reject("This trigger has no supported teleport destination");
        if(p.ghost) {
            if(destination->mapId!=p.corpseMapId || !p.corpseValid || !p.corpseInstanceId ||
                std::none_of(g.instances.begin(),g.instances.end(),[&](const auto& row){return row.id==p.corpseInstanceId&&row.mapId==p.corpseMapId;}))
                return reject("This entrance does not lead to your corpse");
            p.mapId=p.corpseMapId;p.instanceId=p.corpseInstanceId;
            p.x=destination->x;p.y=destination->y;p.z=destination->z;p.orientation=destination->orientation;
            p.portalCooldown=2;++p.positionRevision;finishLocalTeleport(p);g.regionTimer=1;
            result="Entered the instance as a ghost";return true;
        }
        uint32_t instanceId=0;
        // Either the catalog's instance_template or the client's own Map.dbc is
        // enough to make this an instance. That is what lets a dungeon the
        // upstream dump did not carry still open as one.
        if(instanceMap(destination->mapId)) {
            const auto party=g.partyOf(p.guid);
            uint64_t group=cmd.target ? p.guid : 0;
            if(party) {
                const auto owner=g.partyInstanceOwners.find(party);
                group=owner!=g.partyInstanceOwners.end() ? owner->second : (uint64_t(1)<<63)|g.nextInstanceId;
            }
            for(const auto& instance:g.instances)if(instance.mapId==destination->mapId && instance.groupId==group){instanceId=instance.id;break;}
            if(!instanceId) {
                if(g.instances.size()>=MaxInstances || g.nextInstanceId>65535)return reject("Local instance binding limit reached (128)");
                instanceId=g.nextInstanceId;g.instances.push_back({instanceId,destination->mapId,group});++g.nextInstanceId;
            }
            if(party)g.partyInstanceOwners[party]=group;
            LOG_INFO("[LOCAL_PARTY_INSTANCE] enter player=",p.guid," party=",party," owner=",group,
                " map=",destination->mapId," instance=",instanceId," pending_save=1");
            if(!p.instanceId) {
                p.returnMapId=p.mapId;p.returnInstanceId=p.instanceId;p.returnX=p.x;p.returnY=p.y;p.returnZ=p.z;p.returnOrientation=p.orientation;p.hasInstanceReturn=true;
            }
        } else p.hasInstanceReturn=false;
        p.mapId=destination->mapId;p.instanceId=instanceId;p.x=destination->x;p.y=destination->y;p.z=destination->z;p.orientation=destination->orientation;
        p.attackTarget=0;p.portalCooldown=2;++p.positionRevision;g.regionTimer=1;
        finishLocalTeleport(p);
        const auto* targetMap = clientMap(destination->mapId);
        const std::string where = destination->name.empty() && targetMap ? targetMap->name : destination->name;
        result = instanceId ? (targetMap && targetMap->instanceType == 2 ? "Entered raid instance: " : "Entered instance: ") + where
                            : "Travelled to " + where;
        return true;
    }

    // --- Travel ------------------------------------------------------------
    if (cmd.action == LocalAction::TakeFlight || cmd.action == LocalAction::DiscoverTaxi) {
        if(cmd.target || (cmd.action==LocalAction::DiscoverTaxi && cmd.id))return reject("Invalid flight request");
        if(p.attackTarget || p.castingSpellId || localCombatActive(p,g.npcs))
            return reject("Use flight services while stationary and out of combat");
        if (p.flight.active) return reject("You are already in flight");
        if (p.transportEntry) return reject("Step off the transport first");
        if (!g.travel.loaded()) return reject("No taxi data; client DBCs are not loaded");
        // The flight is bought from a flight master, so one has to be here.
        // The node the player departs from is the one that NPC serves, never a
        // node the client asked for - which is what stops a guest from
        // departing anywhere it likes.
        const auto* master=serviceNpc(p,LocalTravelNetwork::NpcFlagFlightMaster,cmd.serviceNpcGuid);
        if (!master || p.instanceId) return reject("Speak to a friendly flight master");
        const auto allowedNode=[&](uint32_t id){const auto* node=g.travel.node(id);return node && ((p.race==2 || p.race==5 || p.race==6 || p.race==8 || p.race==10)?node->mountHorde:node->mountAlliance);};
        if(!allowedNode(master->taxiNodeId))return reject("This flight point does not serve your faction");
        if(cmd.action==LocalAction::DiscoverTaxi){
            if(std::find(p.knownTaxiNodes.begin(),p.knownTaxiNodes.end(),master->taxiNodeId)!=p.knownTaxiNodes.end())return reject("You already know this flight point");
            if(!discoverTaxiNode(p,master->taxiNodeId))return reject("Flight discovery limit reached");
            result="Flight point discovered";return true;
        }
        if(!allowedNode(cmd.id))return reject("The destination does not serve your faction");
        if (master->taxiNodeId == cmd.id) return reject("You are already here");
        // Standing at the flight master is how a node becomes known, so the
        // departure node is discovered even if the player walked here.
        // Discovery is committed only after the purchase has passed every check.
        const auto reachable = flightDestinations(p, master->taxiNodeId);
        if (std::find(reachable.begin(), reachable.end(), cmd.id) == reachable.end()) {
            return reject("No known flight path leads there");
        }
        const LocalTaxiPath* route = g.travel.directPath(master->taxiNodeId, cmd.id);
        if (!route) return reject("No known flight path leads there");
        if (route->cost > p.money) return reject("You cannot afford that flight");
        LocalFlightState flight;
        if (!g.travel.beginFlight(master->taxiNodeId, cmd.id, flight)) {
            return reject("That route has no path in your client data");
        }
        discoverTaxiNode(p,master->taxiNodeId);
        flight.originMap=p.mapId;flight.originX=p.x;flight.originY=p.y;flight.originZ=p.z;flight.originOrientation=p.orientation;
        ++p.positionRevision;finishLocalTeleport(p);
        p.money -= route->cost;
        p.flight = flight;
        p.attackTarget = 0;
        if (p.castingSpellId) clearCast(p, LocalCastStatus::Interrupted);
        const LocalTaxiNode* destination = g.travel.node(cmd.id);
        result = "Flying to " + (destination ? destination->name : std::to_string(cmd.id));
        return true;
    }
    if (cmd.action == LocalAction::LeaveTransport) {
        if (cmd.target || cmd.id) return reject("Leaving a transport takes no argument");
        if (!p.transportEntry) return reject("You are not on a transport");
        ++p.positionRevision;finishLocalTeleport(p);
        result = "Stepped off the transport";
        return true;
    }
    if (cmd.action == LocalAction::BoardTransport) {
        if (cmd.target || !cmd.id) return reject("Invalid transport boarding request");
        if (p.flight.active) return reject("You are in flight");
        if (p.transportEntry) return reject("You are already aboard");
        const LocalTransportState* hull = nullptr;
        for (const auto& t : g.transports) if (t.entry == cmd.id) { hull = &t; break; }
        if (!hull) return reject("No such transport is running");
        if (hull->mapId != p.mapId || p.instanceId) return reject("That transport is elsewhere");
        // Boarding range, not a teleport onto the deck: the player has to walk
        // up the gangway like anyone else.
        const float dx = p.x - hull->x, dy = p.y - hull->y, dz = p.z - hull->z;
        if (dx * dx + dy * dy + dz * dz > 90 * 90) return reject("Move closer to board");
        ++p.positionRevision;finishLocalTeleport(p);
        p.transportEntry = hull->entry;
        const float c = std::cos(hull->orientation), s = std::sin(hull->orientation);
        p.transportOffsetX = c * dx + s * dy;
        p.transportOffsetY = -s * dx + c * dy;
        p.transportOffsetZ = dz;
        p.transportLastYaw = hull->orientation;
        p.attackTarget = 0;
        result = "Aboard";
        return true;
    }

    // --- Merchants, repair, trainers and innkeepers --------------------------
    //
    // Merchant commands identify the selected NPC. The authority validates
    // that exact service at the player's own position and faction. Guests cannot
    // trade with a merchant on the far side of the world, buy training
    // from something that is not a trainer, or bind its home to thin air.
    if (cmd.action == LocalAction::BuybackItem) {
        if (p.flight.active) return reject("You are in flight");
        if (cmd.target || !cmd.id) return reject("Invalid buyback entry");
        if (!serviceNpc(p, kLocalNpcFlagAnyVendor, cmd.serviceNpcGuid)) return reject("Stand at a merchant");
        const auto found = std::find_if(p.buyback.begin(), p.buyback.end(), [&](const auto& row) { return row.id == cmd.id; });
        if (found == p.buyback.end()) return reject("That buyback item is no longer available");
        const auto row = *found;
        if (!c.item(row.itemId)) return reject("No such item");
        if (row.price > p.money) return reject("You cannot afford that");
        auto candidate = p;
        if (!addItem(candidate, c, row.itemId, row.count)) return reject("Inventory full; free space before buying back");
        candidate.money -= row.price;
        candidate.buyback.erase(candidate.buyback.begin() + (found - p.buyback.begin()));
        p = std::move(candidate); questStatus(p, c);
        result = "Bought back " + std::to_string(row.count) + " item(s)";
        return true;
    }
    if (cmd.action == LocalAction::SellToVendor || cmd.action == LocalAction::BuyFromVendor) {
        if (p.flight.active) return reject("You are in flight");
        const bool buying = cmd.action == LocalAction::BuyFromVendor;
        if (!cmd.target || cmd.target > 65535) return reject("Invalid stack size");
        const auto count = uint32_t(cmd.target);
        const auto* merchant = serviceNpc(p, kLocalNpcFlagAnyVendor, cmd.serviceNpcGuid);
        if (!merchant) return reject("Stand at a merchant");
        // Resolve the stock first: paging other item definitions can invalidate
        // a pointer into the bounded item cache.
        if (buying) {
            const auto stock = vendorStock(p, merchant->guid);
            if (std::find(stock.begin(), stock.end(), cmd.id) == stock.end())
                return reject("This merchant does not carry that");
        }
        const auto* loadedItem = c.item(cmd.id);
        if (!loadedItem) return reject("No such item");
        const LocalItemDefinition itemDefinition = *loadedItem;
        const auto* item = &itemDefinition;
        if (buying) {
            const auto price = localVendorBuyTotal(*item, count);
            if (price > p.money) return reject("You cannot afford that");
            // Build the whole purchase before any of it is committed: a bag
            // that fills halfway through must not have taken the gold.
            auto candidate = p;
            if (!addItem(candidate, c, cmd.id, count)) return reject("Inventory full; free space before buying");
            const auto* offer = localVendorOffer(merchant->entry, cmd.id);
            const auto* definition = c.npc(merchant->entry);
            // An explicit catalog stock overrides the transcribed stock rules.
            if (offer && (!definition || definition->vendorItems.empty()) &&
                !impl_->vendorInventory.consume(merchant->guid, *offer, count,
                                                 localVendorBuyCount(cmd.id), impl_->now))
                return reject("This merchant is out of stock; wait for restocking");
            candidate.money -= price;
            p = std::move(candidate);
            questStatus(p, c);
            result = "Bought " + std::to_string(count) + "x " + item->name;
            return true;
        }
        if (totalItem(p, cmd.id) < count) return reject("You do not have that many");
        // Worn gear is not junk to be sold out from under the character.
        if (std::find(p.equipment.begin(), p.equipment.end(), cmd.id) != p.equipment.end() &&
            totalItem(p, cmd.id) - count < uint32_t(std::count(p.equipment.begin(), p.equipment.end(), cmd.id)))
            return reject("Unequip that first");
        const uint64_t fullSaleValue=uint64_t(item->value)*count;
        if(fullSaleValue>1000000000ULL)return reject("Sale exceeds the money limit; no items were sold");
        const auto paid = uint32_t(fullSaleValue);
        if (uint64_t(p.money) + paid > 1000000000) return reject("You cannot carry any more money");
        if (p.buybackSerial == UINT32_MAX) return reject("Buyback transaction limit reached");
        // Build the ledger before removing goods; allocation failure cannot
        // turn a completed sale into an absent buyback entry.
        auto candidate = p;
        candidate.buyback.insert(candidate.buyback.begin(), {++candidate.buybackSerial, cmd.id, paid, uint16_t(count)});
        if (candidate.buyback.size() > kLocalMaxBuyback) candidate.buyback.resize(kLocalMaxBuyback);
        removeItem(candidate, cmd.id, count); candidate.money += paid;
        p = std::move(candidate);
        stats(p, c, false); questStatus(p, c);
        result = paid ? "Sold " + std::to_string(count) + "x " + item->name + " for " + std::to_string(paid) + " copper"
                      : "Sold " + std::to_string(count) + "x " + item->name + "; this merchant pays nothing for it";
        return true;
    }
    if (cmd.action == LocalAction::RepairEquipment) {
        if (cmd.target || cmd.id) return reject("Repair takes no argument");
        if (!serviceNpc(p, kLocalNpcFlagRepair, cmd.serviceNpcGuid)) return reject("Stand at a blacksmith or repair merchant");
        // Honest no-op. Durability is item_template.MaxDurability plus a per-item
        // wear counter, neither of which exists here: the world catalog does not
        // carry the column and nothing in this ruleset damages equipment. So
        // there is nothing to restore, and charging for it would be inventing a
        // cost for work that was never done.
        result = "Nothing to repair: this realm does not track equipment durability, so you were not charged";
        return true;
    }
    const bool training = cmd.action==LocalAction::TrainRiding || cmd.action==LocalAction::LearnSpell || cmd.action==LocalAction::LearnRecipe ||
        cmd.action==LocalAction::LearnProfession || cmd.action==LocalAction::TrainProfessionRank;
    if(training && (p.flight.active || p.transportEntry || p.castingSpellId || p.attackTarget ||
        localCombatActive(p,g.npcs)))
        return reject("Train while stationary and out of combat");
    if(cmd.action==LocalAction::TrainRiding){
        if(cmd.target || !cmd.serviceNpcGuid)return reject("Select a riding trainer");
        const auto* trainer=serviceNpc(p,kLocalNpcFlagTrainerProfession,cmd.serviceNpcGuid);
        if(!trainer || trainer->trainerSkill!=762)return reject("This NPC does not teach riding");
        const LocalRidingRank* rank=nullptr;for(const auto& row:LocalRidingRanks)if(row.skill==cmd.id)rank=&row;
        if(!rank || p.ridingSkill!=rank->previous)return reject("Learn the preceding riding rank first");
        if(p.level<rank->level)return reject("Level too low for this riding rank");
        if(p.money<rank->cost)return reject("You cannot afford riding training");
        p.money-=rank->cost;p.ridingSkill=rank->skill;result=std::string("Learned ")+rank->name;return true;
    }
    if (cmd.action == LocalAction::LearnSpell) {
        if (cmd.target) return reject("Training takes only an ability");
        const auto* trainer = serviceNpc(p, kLocalNpcFlagTrainerClass, cmd.serviceNpcGuid);
        if (!trainer) return reject("Stand at a trainer of your own class");
        if (!trainer->trainerClass) return reject("This trainer teaches something this realm does not model");
        if (trainer->trainerClass != p.classId) return reject("This trainer does not teach your class");
        const auto teachable = trainableSpells(p, trainer->guid);
        if (std::find(teachable.begin(), teachable.end(), cmd.id) == teachable.end())
            return reject("This trainer cannot teach you that yet");
        const auto* spell = c.spell(cmd.id);
        if (!spell) return reject("No such ability");
        const auto cost = localTrainerSpellCost(*spell);
        if (cost > p.money) return reject("You cannot afford that training");
        // Work on a candidate: skipping several ranks still replaces every
        // earlier learned rank, and an upgrade fits in a full spellbook. Keep
        // the longest remaining cooldown so training cannot reset an ability.
        auto candidate = p;
        const auto replaced = [&](uint32_t id) { return laterSpellRank(c, id, cmd.id); };
        candidate.knownSpells.erase(std::remove_if(candidate.knownSpells.begin(), candidate.knownSpells.end(), replaced), candidate.knownSpells.end());
        if (candidate.knownSpells.size() >= MaxSpells) return reject("Your spellbook is full (" + std::to_string(MaxSpells) + " abilities)");
        uint32_t inheritedCooldown = 0;
        for (const auto& cd : candidate.cooldowns)
            if (replaced(cd.spellId)) inheritedCooldown = std::max(inheritedCooldown, cd.remainingMs);
        candidate.cooldowns.erase(std::remove_if(candidate.cooldowns.begin(), candidate.cooldowns.end(),
            [&](const LocalCooldown& cd) { return replaced(cd.spellId); }), candidate.cooldowns.end());
        candidate.knownSpells.push_back(cmd.id);
        if (inheritedCooldown) candidate.cooldowns.push_back({cmd.id, inheritedCooldown});
        candidate.money -= cost;
        result = "Learned " + spell->name;
        p = std::move(candidate);
        return true;
    }
    const auto inCombat = [&] {
        return localCombatActive(p,impl_->npcs);
    };
    if(cmd.action==LocalAction::BackpackMove || cmd.action==LocalAction::BankWithdrawSlot){
        const bool bank=cmd.action==LocalAction::BankWithdrawSlot;
        if(inCombat() || p.castingSpellId || p.flight.active || p.transportEntry)return reject("Cannot rearrange inventory now");
        if(bank && (!cmd.serviceNpcGuid || !serviceNpc(p,kLocalNpcFlagBanker,cmd.serviceNpcGuid)))return reject("Stand at the selected friendly banker");
        if(!cmd.target || cmd.target>65535 || !cmd.id || cmd.id>(bank?kLocalBankSlots:24) || !cmd.buyout || cmd.buyout>24 || (!bank && cmd.id==cmd.buyout))return reject("Choose different valid inventory cells");
        auto candidate=p;normalizeLocalInventory(candidate);
        const auto sourceIndex=bank?size_t(cmd.id-1):localInventoryIndex(candidate,cmd.id-1);
        if(!bank && sourceIndex>=candidate.inventory.size())return reject("The source backpack cell is empty");
        const auto originalSource=bank?candidate.bank[sourceIndex]:candidate.inventory[sourceIndex];
        auto destinationIndex=localInventoryIndex(candidate,cmd.buyout-1);
        const auto originalDestination=destinationIndex<candidate.inventory.size()?candidate.inventory[destinationIndex]:LocalItemStack{};
        if(originalSource.itemId!=cmd.bid || originalSource.count!=cmd.bankSourceCount || originalDestination.itemId!=cmd.durationMinutes || originalDestination.count!=cmd.bankDestinationCount)return reject("Those stacks changed; pick up the item again");
        if(bank && originalDestination.itemId && originalDestination.itemId!=originalSource.itemId){
            const auto worn=uint32_t(std::count(p.equipment.begin(),p.equipment.end(),originalDestination.itemId));
            if(totalItem(p,originalDestination.itemId)<worn+originalDestination.count)return reject("Unequip that stack before swapping it into the bank");
        }
        if(destinationIndex==candidate.inventory.size()){
            if(candidate.inventory.size()>=MaxInventory)return reject("Backpack full");
            candidate.inventory.push_back({0,0,uint8_t(cmd.buyout-1)});
        }
        auto& source=bank?candidate.bank[sourceIndex]:candidate.inventory[sourceIndex];
        if(!moveLocalInventoryStack(source,candidate.inventory[destinationIndex],uint16_t(cmd.target),c))return reject("Only complete valid stacks can swap; check the available stack space");
        if(bank && !source.count)source={};
        std::erase_if(candidate.inventory,[](const auto& s){return !s.count;});
        if(!validLocalInventoryLayout(candidate) || !validEquipment(candidate,c))return reject("Inventory move would invalidate equipment or layout");
        p=std::move(candidate);questStatus(p,c);result=bank?"Bank item placed in selected backpack cell":"Backpack layout saved";return true;
    }
    if (cmd.action == LocalAction::BankDeposit || cmd.action == LocalAction::BankDepositFromSlot || cmd.action == LocalAction::BankWithdraw || cmd.action == LocalAction::BankMove || cmd.action == LocalAction::BankDepositSlot) {
        if (!cmd.serviceNpcGuid || p.flight.active || inCombat() || p.castingSpellId ||
            !serviceNpc(p, kLocalNpcFlagBanker, cmd.serviceNpcGuid)) return reject("Stand at a friendly banker out of combat");
        if (!cmd.target || cmd.target > 65535) return reject("Invalid bank transfer quantity");
        auto candidate = p;normalizeLocalInventory(candidate);
        const uint32_t count = uint32_t(cmd.target);
        if (cmd.action == LocalAction::BankDepositSlot) {
            const auto sourceIndex=cmd.id?localInventoryIndex(candidate,cmd.id-1):candidate.inventory.size();
            if(sourceIndex>=candidate.inventory.size() || !cmd.buyout || cmd.buyout>kLocalBankSlots)
                return reject("Choose a valid backpack and bank slot");
            auto& source=candidate.inventory[sourceIndex];
            auto& destination=candidate.bank[cmd.buyout-1];
            if(!source.itemId || source.itemId!=cmd.bid || source.count!=cmd.bankSourceCount || source.count<count ||
               destination.itemId!=cmd.durationMinutes || destination.count!=cmd.bankDestinationCount)
                return reject("Those inventory stacks changed; pick up the item again");
            const auto* definition=c.item(source.itemId);
            if(!definition || !source.count || source.count>definition->stack)
                return reject("Invalid backpack stack");
            const auto stackLimit=definition->stack;
            const auto equipped=uint32_t(std::count(p.equipment.begin(),p.equipment.end(),source.itemId));
            if(totalItem(p,source.itemId)<equipped+count)return reject("Unequip that item before banking it");
            if(!destination.itemId) {
                if(destination.count || count>stackLimit)return reject("Invalid empty bank slot");
                destination={source.itemId,uint16_t(count)};source.count-=uint16_t(count);
            } else if(destination.itemId==source.itemId) {
                if(!destination.count || destination.count>stackLimit || count>uint32_t(stackLimit-destination.count))
                    return reject("That bank stack has no room for this quantity");
                destination.count+=uint16_t(count);source.count-=uint16_t(count);
            } else {
                const auto* other=c.item(destination.itemId);
                if(!other || !destination.count || destination.count>other->stack || count!=source.count)
                    return reject("Only whole valid stacks can swap with the bank");
                std::swap(source.itemId,destination.itemId);std::swap(source.count,destination.count);
            }
            if(!source.count)candidate.inventory.erase(candidate.inventory.begin()+sourceIndex);
        } else if (cmd.action == LocalAction::BankMove) {
            // BankMove: id=source slot, buyout=destination slot, target=quantity,
            // bid/durationMinutes=expected item IDs; counts travel in the action's
            // four-byte extension. Validate both complete stacks before any edit.
            if (!cmd.id || cmd.id > kLocalBankSlots || !cmd.buyout || cmd.buyout > kLocalBankSlots || cmd.id == cmd.buyout)
                return reject("Choose two different bank slots");
            auto& source = candidate.bank[cmd.id - 1];
            auto& destination = candidate.bank[cmd.buyout - 1];
            if (!source.itemId || source.itemId != cmd.bid || source.count != cmd.bankSourceCount ||
                destination.itemId != cmd.durationMinutes || destination.count != cmd.bankDestinationCount || source.count < count)
                return reject("Those bank stacks changed; pick up the item again");
            const auto* item = c.item(source.itemId);
            if (!item || !source.count || source.count > item->stack) return reject("Invalid source bank stack");
            if (!destination.itemId) {
                destination = {source.itemId, uint16_t(count)};
                source.count -= uint16_t(count); if (!source.count) source = {};
            } else if (destination.itemId == source.itemId) {
                if (!destination.count || destination.count > item->stack || count > uint32_t(item->stack - destination.count))
                    return reject("That bank stack has no room for this quantity");
                destination.count += uint16_t(count);
                source.count -= uint16_t(count); if (!source.count) source = {};
            } else {
                const auto* other = c.item(destination.itemId);
                if (!other || !destination.count || destination.count > other->stack || count != source.count)
                    return reject("Only whole valid stacks can swap bank slots");
                std::swap(source, destination);
            }
        } else if (cmd.action == LocalAction::BankDeposit || cmd.action==LocalAction::BankDepositFromSlot) {
            const bool exact=cmd.action==LocalAction::BankDepositFromSlot;
            const auto index=cmd.id?localInventoryIndex(candidate,cmd.id-1):candidate.inventory.size();
            const auto itemId=exact?cmd.bid:cmd.id;
            if(exact && (index>=candidate.inventory.size() || candidate.inventory[index].itemId!=cmd.bid || candidate.inventory[index].count!=cmd.buyout || candidate.inventory[index].count<count))return reject("That backpack stack changed; select it again");
            const auto* item = c.item(itemId);
            const uint32_t equipped = uint32_t(std::count(p.equipment.begin(), p.equipment.end(), itemId));
            if (!item || totalItem(p, itemId) < equipped + count) return reject("Not enough unequipped items");
            uint32_t left = count;
            for (auto& slot : candidate.bank) if (slot.itemId == itemId) {
                if (!slot.count || slot.count > item->stack) return reject("Invalid destination bank stack");
                const auto moved = std::min(left, uint32_t(item->stack - slot.count));
                slot.count += uint16_t(moved); left -= moved;
            }
            for (auto& slot : candidate.bank) if (!slot.itemId && left) {
                const auto moved = std::min(left, uint32_t(item->stack));
                slot = {itemId, uint16_t(moved)}; left -= moved;
            }
            if (left) return reject("Bank full; no items were moved");
            if(exact){candidate.inventory[index].count-=uint16_t(count);if(!candidate.inventory[index].count)candidate.inventory.erase(candidate.inventory.begin()+index);}
            else removeItem(candidate, itemId, count);
        } else {
            if (!cmd.id || cmd.id > kLocalBankSlots) return reject("Invalid bank slot");
            auto& slot = candidate.bank[cmd.id - 1];
            if (!slot.itemId || slot.itemId != cmd.bid || slot.count < count || (cmd.buyout && slot.count != cmd.buyout)) return reject("That bank stack changed; reopen the bank");
            if (!addItem(candidate, c, slot.itemId, count)) return reject("Inventory full; no items were moved");
            slot.count -= uint16_t(count); if (!slot.count) slot = {};
        }
        p = std::move(candidate); questStatus(p, c);
        result = "Bank transfer complete"; return true;
    }
    if (cmd.action == LocalAction::UnlearnProfession) {
        if (cmd.target || inCombat() || p.castingSpellId || p.flight.active) return reject("Cannot unlearn a profession now");
        const auto* line = localProfession(skillLines(), cmd.id);
        if (!line || line->category != kLocalSkillCategoryProfession) return reject("Only primary professions can be unlearned");
        auto found = std::find_if(p.professions.begin(), p.professions.end(), [&](const auto& s){return s.skillId == cmd.id;});
        if (found == p.professions.end()) return reject("You do not know that profession");
        auto candidate = p;
        candidate.knownRecipes.erase(std::remove_if(candidate.knownRecipes.begin(), candidate.knownRecipes.end(),
            [&](uint32_t id){const auto* r=c.recipe(id);return r && r->skillId==cmd.id;}), candidate.knownRecipes.end());
        candidate.professions.erase(candidate.professions.begin() + (found-p.professions.begin()));
        p = std::move(candidate); result = "Unlearned " + line->name + " and its recipes"; return true;
    }
    if (cmd.action == LocalAction::LearnRecipe) {
        if (cmd.serviceNpcGuid && !serviceNpc(p,kLocalNpcFlagTrainerProfession,cmd.serviceNpcGuid)) return reject("Stand at the selected trainer");
        if (cmd.target) return reject("Training takes only a recipe");
        const auto* recipe = c.recipe(cmd.id);
        if (!recipe) return reject("Your client's data describes no such recipe");
        const auto teachable = trainableRecipes(p, cmd.serviceNpcGuid);
        if (std::find(teachable.begin(), teachable.end(), cmd.id) == teachable.end())
            return reject("This trainer cannot teach you " + recipe->name + " yet");
        if (p.knownRecipes.size() >= MaxRecipes)
            return reject("Your recipe book is full (" + std::to_string(MaxRecipes) + ")");
        const auto cost = localRecipeCost(*recipe);
        if (cost > p.money) return reject("You cannot afford that recipe");
        auto candidate=p;
        candidate.knownRecipes.push_back(cmd.id);
        std::sort(candidate.knownRecipes.begin(), candidate.knownRecipes.end());
        candidate.money -= cost;
        p=std::move(candidate);
        result = "Learned " + recipe->name;
        return true;
    }
    if (cmd.action == LocalAction::CraftItem) {
        const uint32_t batch = cmd.target ? uint32_t(cmd.target) : 1;
        if (cmd.target > 20 || inCombat() || p.castingSpellId || p.flight.active) return reject("Craft 1–20 items out of combat");
        if (std::find(p.knownRecipes.begin(), p.knownRecipes.end(), cmd.id) == p.knownRecipes.end())
            return reject("You have not learned that recipe");
        const auto* recipe = c.recipe(cmd.id);
        if (!recipe) return reject("Your client's data no longer describes that recipe");
        if(!localRecipeAllows(*recipe,p))return reject(recipe->unsupportedReason.empty()?"This recipe is not available to your race or class":recipe->unsupportedReason);
        if(!localRecipeHasTools(*recipe,p))return reject("You need the required crafting tools in your inventory");
        auto skill = std::find_if(p.professions.begin(), p.professions.end(),
            [&](const LocalProfessionSkill& s) { return s.skillId == recipe->skillId; });
        if (skill == p.professions.end()) return reject("You no longer have that profession");
        if (recipe->requiredSkill > skill->current)
            return reject("Requires skill " + std::to_string(recipe->requiredSkill));
        auto candidate = p;
        std::string gained;
        for (uint32_t craft = 0; craft < batch; ++craft) {
        for (const auto& reagent : recipe->reagents) {
            if (localRecipeReagentCount(*recipe,candidate,reagent.itemId) >= reagent.count) continue;
            const auto* item = c.item(reagent.itemId);
            return reject("You need " + std::to_string(reagent.count) + "x " +
                          (item ? item->name : std::to_string(reagent.itemId)));
        }
        // Build the finished character before committing any of it: reagents
        // must not be consumed by a craft whose product has nowhere to go.
        for (const auto& reagent : recipe->reagents) removeItem(candidate, reagent.itemId, reagent.count);
        if (!addItem(candidate, c, recipe->createdItemId, recipe->createdCount))
            return reject("Inventory full; free space before crafting");
        // The client's own trivial ranks say what this is worth. Accumulated
        // rather than rolled - see localCraftSkillChance about why.
        auto& learned = *std::find_if(candidate.professions.begin(), candidate.professions.end(),
            [&](const LocalProfessionSkill& s) { return s.skillId == recipe->skillId; });
        if (learned.current < learned.max) {
            learned.progress = uint16_t(learned.progress + localCraftSkillChance(*recipe, learned.current));
            while (learned.progress >= 1000 && learned.current < learned.max) {
                learned.progress = uint16_t(learned.progress - 1000);
                ++learned.current;
                gained = " (skill " + std::to_string(learned.current) + "/" + std::to_string(learned.max) + ")";
            }
            if (learned.current >= learned.max) learned.progress = 0;
        }
        }
        p = std::move(candidate);
        stats(p, c, false); questStatus(p, c);
        const auto* product = c.item(recipe->createdItemId);
        result = "Crafted " + std::to_string(batch) + "x " + (product ? product->name : recipe->name) + gained;
        return true;
    }
    if (cmd.action == LocalAction::LearnProfession || cmd.action == LocalAction::TrainProfessionRank) {
        if (cmd.target) return reject("Training takes only a profession");
        const auto* trainer = serviceNpc(p, kLocalNpcFlagTrainerProfession, cmd.serviceNpcGuid);
        if (!trainer) return reject("Stand at a profession trainer");
        if (!trainer->trainerSkill) return reject("This trainer teaches a profession this realm does not model");
        if (trainer->trainerSkill != cmd.id) return reject("This trainer does not teach that profession");
        const auto* line = localProfession(skillLines(), cmd.id);
        if (!line) return reject("Your client's SkillLine data does not describe that profession");
        auto existing = std::find_if(p.professions.begin(), p.professions.end(),
                                     [&](const LocalProfessionSkill& s) { return s.skillId == cmd.id; });
        const auto& ranks = localProfessionRanks();
        if (cmd.action == LocalAction::LearnProfession) {
            if (existing != p.professions.end()) return reject("You already know " + line->name);
            if (p.professions.size() >= MaxProfessions) return reject("You cannot carry more professions");
            if (line->category == kLocalSkillCategoryProfession) {
                size_t primaries = 0;
                for (const auto& known : p.professions) {
                    const auto* other = localProfession(skillLines(), known.skillId);
                    if (other && other->category == kLocalSkillCategoryProfession) ++primaries;
                }
                if (primaries >= kLocalMaxPrimaryProfessions)
                    return reject("You already have two primary professions; unlearn one first");
            }
            // Retail gates the primary professions on character level and lets
            // anyone pick up a secondary skill; so does this.
            if (line->category == kLocalSkillCategoryProfession && p.level < ranks.front().level)
                return reject("Come back at level " + std::to_string(ranks.front().level));
            if (ranks.front().cost > p.money) return reject("You cannot afford that training");
            auto candidate=p;
            candidate.professions.push_back({uint16_t(cmd.id), 1, ranks.front().cap});
            candidate.money -= ranks.front().cost;
            p=std::move(candidate);
            result = "Learned " + line->name + " (" + ranks.front().name + ")";
            return true;
        }
        if (existing == p.professions.end()) return reject("Learn " + line->name + " first");
        const auto* next = localNextProfessionRank(existing->max);
        if (!next) return reject("You have mastered " + line->name);
        if (p.level < next->level)
            return reject(std::string(next->name) + " requires level " + std::to_string(next->level));
        // Retail refuses the next rank until the current one is nearly used up,
        // and so does this: a cap nobody has worked towards is not a rank.
        if (existing->current + 25 < existing->max)
            return reject("Raise your " + line->name + " skill closer to " + std::to_string(existing->max) + " first");
        if (next->cost > p.money) return reject("You cannot afford that training");
        p.money -= next->cost;
        existing->max = next->cap;
        result = std::string(next->name) + " " + line->name + " (skill cap " + std::to_string(next->cap) + ")";
        return true;
    }
    if (cmd.action == LocalAction::SetHome) {
        if (p.flight.active || p.transportEntry) return reject("Finish travelling before setting your home");
        if (cmd.target || cmd.id) return reject("Binding takes no argument");
        const auto* innkeeper = serviceNpc(p, kLocalNpcFlagInnkeeper, cmd.serviceNpcGuid);
        if (!innkeeper) return reject("Stand at an innkeeper");
        if (p.instanceId) return reject("You cannot make an inn of a dungeon");
        // The innkeeper's own position, not the player's: home is the inn.
        p.hasHome = true; p.homeMapId = innkeeper->mapId;
        p.homeX = innkeeper->x; p.homeY = innkeeper->y; p.homeZ = innkeeper->z;
        p.homeOrientation = innkeeper->orientation;
        result = "Home set at " + innkeeper->name;
        return true;
    }
    if (cmd.action == LocalAction::ReturnHome) {
        if (cmd.target || cmd.id) return reject("Returning home takes no argument");
        if (!p.hasHome) return reject("Speak to an innkeeper first");
        if (p.hearthCooldown > 0)
            return reject("Available again in " + std::to_string(uint32_t(p.hearthCooldown + 0.5f)) + " seconds");
        if (p.flight.active) return reject("You are in flight");
        p.mapId = p.homeMapId; p.instanceId = 0; p.x = p.homeX; p.y = p.homeY; p.z = p.homeZ;
        p.orientation = p.homeOrientation;
        p.hasInstanceReturn = false; p.transportEntry = 0; p.attackTarget = 0;
        if (p.castingSpellId) clearCast(p, LocalCastStatus::Interrupted);
        p.hearthCooldown = HearthCooldownSeconds; p.portalCooldown = 2; ++p.positionRevision;
        finishLocalTeleport(p);g.regionTimer=1;
        result = "Returned to the inn";
        return true;
    }

    if(cmd.action==LocalAction::CancelCast) {if(!p.castingSpellId)return reject("No spell is being cast");clearCast(p,LocalCastStatus::Interrupted);result="Cast cancelled";return true;}
    if(cmd.action==LocalAction::StopAttack){localStopRangedAuto(p);p.attackTarget=0;if(p.castingSpellId)clearCast(p,LocalCastStatus::Interrupted);result="Attack and casting stopped";return true;}
    if(cmd.action==LocalAction::Attack) {
        if(!n||n->dead||!canAttack(p,*n))return reject("Choose a living enemy");
        if(distance2(p,*n)>30*30)return reject("Target is too far away");
        localStopRangedAuto(p);p.mountSpellId=0;p.attackTarget=n->guid;result="Attacking "+n->name;return true;
    }
    if(cmd.action==LocalAction::CastSpell) {
        const auto* ranged=c.spell(cmd.id);
        if(ranged&&localRangedAutoSpell(*ranged)) {
            if(p.rangedAutoSpellId==cmd.id&&p.rangedTarget==cmd.target){localStopRangedAuto(p);result="Ranged attack stopped";return true;}
            if(std::find(p.knownSpells.begin(),p.knownSpells.end(),cmd.id)==p.knownSpells.end())return reject("Spell is not learned");
            if(!n||n->dead||!n->health||!canAttack(p,*n)||n->mapId!=p.mapId||n->instanceId!=p.instanceId)return reject("Choose a living enemy");
            if(p.castingSpellId)return reject("A spell is already being cast");
            const auto weapon=localRangedAmounts(p,c,*ranged);
            if(!weapon.active)return reject("Equip a usable ranged weapon and compatible ammunition");
            const auto distance=distance2(p,*n);
            // Spell::CheckRange for the one SPELL_RANGE_RANGED row: the minimum
            // is min_range + Unit::GetMeleeRange(target) (Spell.cpp:7369-7376),
            // and the maximum adds both combat reaches (:7358, Unit.cpp:766-780).
            // Both were reach-blind (a hard 5 yd minimum) before the reference.
            const float rangedReach=localCreatureCombatReach(c.npc(n->entry));
            if(localSpellTargetTooClose(*ranged,float(distance),rangedReach))return reject("Target is too close");
            if(!localWithinCombatRange(float(distance),localSpellMaximumRange(p,c,*ranged),kLocalDefaultCombatReach,rangedReach))
                return reject("Target is too far away");
            if(!localComboFacingReady(p,*n,false))return reject("Face your target");
            p.rangedAutoSpellId=cmd.id;p.rangedTarget=cmd.target;p.rangedWeapon=p.equipment[17];p.attackTarget=0;
            if(ranged->rangedAutoProfile==2)p.rangedRemainingMs=std::max(500u,p.rangedRemainingMs);
            localRangedRememberPosition(p);result="Ranged attack started";return true;
        }
        const bool cast=executeCastSpell(p,cmd,players,result,false);
        if(cast&&p.rangedAutoSpellId==5019)localStopRangedAuto(p);
        return cast;
    }
    if(cmd.action==LocalAction::EquipItem) {
        if(inCombat() || p.castingSpellId || p.flight.active || p.transportEntry)return reject("Cannot change equipment now");
        if (!equipItem(p, c, cmd.id, cmd.target)) return reject("Item cannot be equipped in that slot");
        stats(p,c,false);result="Equipped "+c.item(cmd.id)->name;return true;
    }
    if (cmd.action == LocalAction::UnequipItem) {
        if(inCombat() || p.castingSpellId || p.flight.active || p.transportEntry)return reject("Cannot change equipment now");
        if (cmd.target || cmd.id >= p.equipment.size() || !p.equipment[cmd.id] || !validEquipment(p, c))
            return reject("Equipment slot cannot be cleared");
        const auto* item = c.item(p.equipment[cmd.id]);
        p.equipment[cmd.id] = 0;
        stats(p,c,false);result="Unequipped "+item->name;return true;
    }
    if(cmd.action==LocalAction::UseItem) {
        if(const auto* metadata=localAuctionMetadata(cmd.id);metadata && metadata->mountSpell) {
            if(cmd.target)return reject("Mount learning takes only an item");
            if(inCombat() || p.castingSpellId || p.flight.active || p.transportEntry)return reject("Learn mounts while stationary and out of combat");
            if(!p.race || p.race>32 || !p.classId || p.classId>32)return reject("Invalid mount learner");
            if(!totalItem(p,cmd.id))return reject("Mount item is not in your inventory");
            if(!localMountSupported(c,cmd.id))return reject("This mount's flight or scripted effects are not supported locally");
            if(p.ridingSkill<localMountRidingRequirement(*c.spell(metadata->mountSpell)))return reject("Train the required riding rank first");
            if(p.level<metadata->requiredLevel)return reject("Level too low to learn this mount");
            if(metadata->allowableRaces && !(metadata->allowableRaces&(1u<<(p.race-1))))return reject("This mount cannot be learned by your race");
            if(metadata->allowableClasses && !(metadata->allowableClasses&(1u<<(p.classId-1))))return reject("This mount cannot be learned by your class");
            if(std::find(p.knownSpells.begin(),p.knownSpells.end(),metadata->mountSpell)!=p.knownSpells.end())return reject("You already know this mount");
            if(p.knownSpells.size()>=MaxSpells)return reject("Your spellbook is full");
            auto candidate=p;
            candidate.knownSpells.push_back(metadata->mountSpell);removeItem(candidate,cmd.id,1);questStatus(candidate,c);
            p=std::move(candidate);
            result="Learned mount: "+c.spell(metadata->mountSpell)->name;return true;
        }
        const auto* def=c.item(cmd.id);if(!def||!totalItem(p,cmd.id)||(!def->heal&&!def->mana))return reject("Item cannot be used");
        const uint32_t restoredMana=p.resourceType==LocalResourceType::Mana?def->mana:0;
        if((!def->heal||p.health==p.maxHealth)&&(!restoredMana||p.mana==p.maxMana))return reject("Health/resource are already full");
        p.health=std::min(p.maxHealth,p.health+def->heal);p.mana=std::min(p.maxMana,p.mana+restoredMana);removeItem(p,cmd.id,1);stats(p,c,false);questStatus(p,c);result="Used "+def->name;return true;
    }
    if(!n||distance2(p,*n)>8*8)return reject("Move within 8 yards of the target");
    if(cmd.action==LocalAction::Loot) {
        if(!n->dead||!n->lootable)return reject("Nothing to loot");
        if(n->lootOwner!=p.guid)return reject("Loot is reserved for another player");
        const auto* def=c.npc(n->entry);if(!def)return reject("Missing loot definition");
        auto candidate=p;for(const auto& s:def->loot)if(!addItem(candidate,c,s.itemId,s.count))return reject("Inventory full; free space before looting");
        // Money is shared with the original death-time cohort, filtered again
        // for online/living/same-instance proximity at collection. Items remain
        // with the assigned looter. Late joiners never gain old corpse rights.
        std::array<LocalRealmPlayer*,5> recipients{};size_t count=1;recipients[0]=&p;
        for(auto* member:players) {
            if(!member || member->guid==p.guid || member->dead || !member->health || distance2(*member,*n)>60*60 ||
               std::find(n->lootCandidates.begin(),n->lootCandidates.end(),member->guid)==n->lootCandidates.end())continue;
            if(std::any_of(recipients.begin(),recipients.begin()+count,[&](auto* prior){return prior->guid==member->guid;}))continue;
            if(count<recipients.size())recipients[count++]=member;
        }
        std::sort(recipients.begin(),recipients.begin()+count,[](auto* a,auto* b){return a->guid<b->guid;});
        std::array<uint32_t,5> shares{};
        for(size_t i=0;i<count;++i) {
            shares[i]=def->money/uint32_t(count)+(i<def->money%uint32_t(count)?1u:0u);
            if(shares[i] && uint64_t(recipients[i]->money)+shares[i]>1000000000ULL)
                return reject("A loot recipient would exceed the money limit; no loot was collected");
        }
        // Everything that can reject the bundle is checked before any wallet
        // changes. LocalRealm saves all owners together or restores every one.
        for(size_t i=0;i<count;++i) {
            if(recipients[i]->guid==p.guid)candidate.money+=shares[i];
            else recipients[i]->money+=shares[i];
            if(shares[i])LOG_INFO("[LOCAL_GROUP_MONEY] applied npc=",n->guid," collector=",p.guid,
                " recipient=",recipients[i]->guid," copper=",shares[i]," recipients=",count," pending_save=1");
        }
        p=std::move(candidate);n->lootable=false;questStatus(p,c);result="Loot received";return true;
    }
    if(n->dead)return reject("Target is dead");
    if ((cmd.action == LocalAction::Interact || cmd.action == LocalAction::AcceptQuest || cmd.action == LocalAction::TurnInQuest) && isAggressive(p, *n))
        return reject("This character is hostile to your race");
    if(cmd.action==LocalAction::Interact) {
        objectiveCredit(p,c,LocalQuestObjective::Type::Talk,n->entry);result="Speaking with "+n->name;return true;
    }
    if(cmd.action==LocalAction::AcceptQuest) {
        const auto* def=c.quest(cmd.id);if(!def||def->giverEntry!=n->entry||!n->questGiver)return reject("This character does not offer that quest");
        // Professions exist now, so a skill-gated quest is answered rather than
        // refused wholesale. The catalog's own importer still drops quests with
        // a requiredskillid, so this only bites content that carries them.
        if (def->requiredSkill) {
            const auto* line = localProfession(skillLines(), def->requiredSkill);
            const auto known = std::find_if(p.professions.begin(), p.professions.end(),
                [&](const LocalProfessionSkill& s) { return s.skillId == def->requiredSkill; });
            if (!line) return reject("This quest requires a skill this realm does not model");
            if (known == p.professions.end()) return reject("This quest requires " + line->name);
        }
        if (def->allowableRaces && !(def->allowableRaces & (1u << (p.race - 1)))) return reject("This quest is not offered to your race");
        if (def->allowableClasses && !(def->allowableClasses & (1u << (p.classId - 1)))) return reject("This quest is not offered to your class");
        if(p.level<def->minLevel)return reject("Level too low for this quest");
        if (questRewarded(p, cmd.id)) return reject("Quest reward already claimed");
        for(const auto& q:p.quests)if(q.id==cmd.id)return reject("Quest already accepted");
        if(p.quests.size()>=MaxQuests)return reject("Active quest log is full (32); turn in or abandon a quest");
        if(def->prerequisite && !questRewarded(p, def->prerequisite)) return reject("Complete the prerequisite quest first");
        LocalQuestProgress q;q.id=cmd.id;q.progress.resize(def->objectives.size(),0);p.quests.push_back(std::move(q));questStatus(p,c);result="Accepted: "+def->title;return true;
    }
    if(cmd.action==LocalAction::TurnInQuest) {
        const auto* def=c.quest(cmd.id);if(!def||def->turnInEntry!=n->entry||!n->questGiver)return reject("Wrong quest recipient");
        if(!validLocalQuestRewards(*def))return reject("Invalid quest reward bundle");
        // bid is the 1-based choice index for this action, never an item ID.
        // Do not silently select an item for a client with no selection.
        if(def->rewardChoices.empty()?cmd.bid!=0:cmd.bid==0 || cmd.bid>def->rewardChoices.size())
            return reject("Choose a valid quest reward before completing this quest");
        if (questRewarded(p, cmd.id)) return reject("Quest reward already claimed");
        auto candidate=p;
        questStatus(candidate,c);
        const auto progress=std::find_if(candidate.quests.begin(),candidate.quests.end(),[&](const LocalQuestProgress& q){return q.id==cmd.id;});
        if(progress==candidate.quests.end()||progress->status!=LocalQuestStatus::Complete)return reject("Quest objectives are not complete, or reward already claimed");
        if (candidate.completedQuestIds.size() >= MaxCompletedQuests) return reject("Completed quest history storage limit reached; reward remains unclaimed");
        if (uint64_t(candidate.money) + def->money > 1000000000ULL)
            return reject("Quest reward would exceed the money limit; reward remains unclaimed");
        for(const auto& obj:def->objectives)if(obj.type==LocalQuestObjective::Type::Collect)removeItem(candidate,obj.entry,obj.count);
        for(size_t i=0;i<localQuestRewardCount(*def);++i) {
            const auto r=localQuestRewardAt(*def,i);
            if(!addItem(candidate,c,r.itemId,r.count))return reject("Inventory full or reward unavailable; entire quest reward remains unclaimed");
        }
        if(cmd.bid) {
            const auto r=def->rewardChoices[cmd.bid-1];
            if(!addItem(candidate,c,r.itemId,r.count))return reject("Inventory full or reward unavailable; entire quest reward remains unclaimed");
        }
        candidate.money+=def->money;experience(candidate,c,def->xp);
        candidate.quests.erase(progress);
        candidate.completedQuestIds.insert(std::lower_bound(candidate.completedQuestIds.begin(), candidate.completedQuestIds.end(), cmd.id), cmd.id);
        p=std::move(candidate);stats(p,c,false);questStatus(p,c);result="Quest rewarded: "+def->title;
        LOG_INFO("[LOCAL_QUEST_REWARD] applied player=",p.guid," quest=",cmd.id," choice=",cmd.bid," fixed=",localQuestRewardCount(*def));return true;
    }
    return reject("Unsupported local action");
}

bool LocalGameplay::executeCastSpell(LocalRealmPlayer& p,const LocalRealmCommand& cmd,
        const std::vector<LocalRealmPlayer*>& players,std::string& result,bool finishing) {
    auto& g=*impl_;const auto& c=content();
    const auto reject=[&](const std::string& reason){result=reason;if(finishing)clearCast(p,LocalCastStatus::Failed);return false;};
    const auto* d=c.spell(cmd.id);
    if(d&&d->formId&&p.formSpellId==d->id)return reject("This form or stance is already active");
    if(d && d->npcOnly)return reject("NPC spell cannot be cast by a player");
    if(d && d->triggeredOnly)return reject("Triggered spell cannot be cast directly");
    if(d && d->passive)return reject("Passive talents cannot be cast");
    if(d&&!localProcTalentPrerequisite(p,c,*d))return reject("Learn the required root talent first");
    if(d&&!localStormstrikeTalentReady(p,c,*d))return reject("Learn the required Stormstrike talent first");
    if(d&&!localTimedDamageTalentReady(p,c,*d))return reject("Learn the required damage talent first");
    if(!d||std::find(p.knownSpells.begin(),p.knownSpells.end(),cmd.id)==p.knownSpells.end())return reject("Spell is not learned");
    if(p.dead)return reject("Cannot cast while dead");
    if(d->mountDisplayId) {
        if((p.classId==11||p.classId==7)&&p.formSpellId)return reject("Leave your current form before mounting");
        if(!finishing && p.mountSpellId==d->id) {p.mountSpellId=0;result="Dismounted";return true;}
        if(p.ridingSkill<localMountRidingRequirement(*d))return reject("Train the required riding rank first");
        // The same unlock level the trainer gate uses . Every ground
        // mount's Spell.dbc BaseLevel is 0 and its SpellLevel 1, so this gate
        // was, and remains, never reached: what actually gates a mount here is
        // localMountRidingRequirement above.
        if(p.level<spellUnlockLevel(*d))return reject("Level too low to summon this mount");
        if(p.flight.active || p.transportEntry || p.instanceId || (p.movementState&kLocalMovementInLiquid))
            return reject("Cannot mount while swimming, travelling or inside an instance");
        if(p.attackTarget || localCombatActive(p,g.npcs))
            return reject("Cannot mount during combat");
    }
    if(!d->unsupportedReason.empty())return reject(d->name+": "+d->unsupportedReason);
    if(!localSpellFormReady(p,*d))return reject("This spell cannot be used in the current form or stance");
    if(!localFormEnvironmentReady(p,*d))return reject("This form cannot be used while travelling or in this environment");
    if((d->meleeSpecialProfile||d->stormstrikeProfile)&&!validEquipment(p,c))return reject("Invalid equipped weapon state");
    if(!localSpellEquipmentReady(p,c,*d))return reject("Required spell equipment is not equipped");
    if(!d->maxAuraStacks)return reject("Invalid aura stack limit");
    if(!finishing&&p.castingSpellId)return reject("A spell is already being cast; move or stop to cancel");
    if(!finishing&&p.globalCooldownMs)return reject("Global cooldown is active");
    if(d->allowableClasses&&!(d->allowableClasses&(1u<<(p.classId-1))))return reject("This ability is not available to your class");
    if(!d->formId && (d->resourceType==5 ? p.classId!=6 : d->resourceType!=255&&d->resourceType!=uint8_t(p.resourceType)))
        return reject("Ability uses a different resource type");
    const auto runeMask=selectLocalRunes(p.classId,p.runeCooldownMs,d->runeCost);
    if(!runeMask)return reject("Not enough ready runes");
    if(finishing&&!p.castCostPrepared)return reject("Cast preparation is unavailable");
    uint32_t appliedCostAura=0;
    const auto cost=finishing?p.castPreparedCost:localChargedSpellCost(p,c,*d,localSpellBaseResourceCost(p,c,*d),&appliedCostAura);
    if((d->formId&&p.classId==11?localAvailableMana(p):p.mana)<cost)return reject("Not enough resource");
    migrateLocalCategoryCooldowns(p,c);
    if(localSpellCooldownRemaining(p,c,*d))return reject("Spell or shared category is on cooldown");
    const auto cooldown=d->clientSpell?localSpellRecoveryDuration(p,c,*d,false):std::max(500U,d->cooldownMs);
    const auto categoryCooldown=localSpellRecoveryDuration(p,c,*d,true);
    size_t cooldownSlot=p.cooldowns.size();
    if(cooldown) {
        for(size_t i=0;i<p.cooldowns.size();++i)
            if(p.cooldowns[i].spellId==cmd.id) {cooldownSlot=i;break;}
        if(cooldownSlot==p.cooldowns.size())
            for(size_t i=0;i<p.cooldowns.size();++i)
                if(!p.cooldowns[i].remainingMs) {cooldownSlot=i;break;}
        if(cooldownSlot==p.cooldowns.size() && p.cooldowns.size()>=MaxCooldowns)
            return reject("Too many active cooldowns; wait for an ability to recover");
    }
    size_t categorySlot=p.categoryCooldowns.size();
    if(categoryCooldown) {
        if(!d->cooldownCategory||d->categoryCooldownMs>3600000)return reject("Invalid category cooldown");
        for(size_t i=0;i<p.categoryCooldowns.size();++i)
            if(p.categoryCooldowns[i].category==d->cooldownCategory&&p.categoryCooldowns[i].family==d->spellFamily){categorySlot=i;break;}
        if(categorySlot==p.categoryCooldowns.size())for(size_t i=0;i<p.categoryCooldowns.size();++i)
            if(!p.categoryCooldowns[i].remainingMs){categorySlot=i;break;}
        if(categorySlot==p.categoryCooldowns.size()&&p.categoryCooldowns.size()>=kLocalMaxCategoryCooldowns)
            return reject("Too many active shared cooldown categories");
    }
    auto* n=g.npc(cmd.target);LocalRealmPlayer* healed=nullptr;
    if(d->summonPetEntry) {
        if(!validLocalSummonPet(*d))return reject("Invalid summon profile");
        // P07 : the creature comes from the compiled pet catalog, not
        // from the spawnable world catalog. previously this asked
        // `c.npc(entry)`, and the shipped 14,496-record pack contains only
        // SPAWNABLE creatures - none of which is a pet a summon names - so
        // every summon in the build was refused here and no pet could exist.
        // See the source audit section 0.1.
        if(!g.petCreatureKnown(d->summonPetEntry))
            return reject("This realm has no creature record for that summon");
        if(p.flight.active||p.transportEntry)return reject("Cannot summon while travelling");
        if(p.formSpellId)return reject("Leave your current form before summoning");
        // Spell::EffectSummonPet returns without summoning when the owner's
        // existing pet is in corpse state; refuse the cast rather than spend it.
        if(const auto* existing=g.controlledPetOf(p.guid))
            { if(existing->dead)return reject("Your summon is dead"); }
        else if(g.pets.size()>=kLocalMaxPets)return reject("Too many active summons");
    }
    // P05-5 / P05-7 . The target's own combat reach enters every range
    // test (Unit.cpp:766-803) and its creature type is what
    // SpellInfo::CheckTargetCreatureType compares column 17 against
    // (SpellInfo.cpp:1906-1919). `localNpcCreatureType` is creature_template.type
    // for every template the pinned dump carries, which is the same authority
    // the reference reads (Unit::GetCreatureType, Unit.cpp:11485).
    const auto* rangeTarget=n?c.npc(n->entry):nullptr;
    const float targetReach=n?localCreatureCombatReach(rangeTarget):kLocalDefaultCombatReach;
    if((d->damage||d->periodicDamage||d->snarePercent||d->controlProfile||d->stormstrikeProfile==1)&&!d->areaRadius) {
        if(!n||n->dead||!canAttack(p,*n))return reject("Choose a living enemy");
        if(!localSpellTargetInRange(p,c,*d,n->mapId,n->instanceId,n->x,n->y,n->z,targetReach,!finishing))
            return reject("Spell target out of effective range");
        // SpellInfo::CheckTarget, SpellInfo.cpp:1819-1825: a creature whose type
        // mask misses the spell's TargetCreatureType is SPELL_FAILED_BAD_TARGETS.
        // A type of 0 (the reference's "no creature type") passes, as :1919 does.
        if(!localSpellCreatureTypeAllowed(*d,localNpcCreatureType(n->entry)))
            return reject(d->name+": that target is not a valid creature type for this spell");
        // SPELL_ATTR1_ONLY_PEACEFUL_TARGETS, SpellInfo.cpp:1694-1695:
        // SPELL_FAILED_TARGET_AFFECTING_COMBAT. Unit::IsInCombat on a local
        // creature is exactly "it has a threat entry or a target".
        if(d->sourceOnlyPeacefulTargets&&localNpcInCombat(*n))
            return reject(d->name+": that target is already in combat");
        // P05-3: FacingCasterFlags & SPELL_FACING_FLAG_INFRONT, the arm of
        // Spell::CheckRange at :7360 this build asked only for the 79 profiled
        // specials and the ranged shot. SPELL_FAILED_UNIT_NOT_INFRONT unless the
        // caster has the target in a pi arc (Position::HasInArc, Position.cpp:148-181)
        // or stands inside its boundary radius (Unit.cpp:820-828).
        if(localSpellRequiresFacing(*d)&&
           !localSpellFacingReady(p.x,p.y,p.orientation,n->x,n->y,n->z,p.z,localCreatureBoundingRadius(rangeTarget)))
            return reject("Face the enemy");
        // P05 line of sight . Spell::CheckCast, Spell.cpp:6092-6118: an
        // explicit unit target other than the caster must be in line of sight
        // unless the spell carries SPELL_ATTR2_IGNORE_LINE_OF_SIGHT or
        // SPELL_ATTR5_ALWAYS_AOE_LINE_OF_SIGHT. Inert - always "visible" - until
        // the player installs a collision pack; see the source audit.
        if(!d->sourceIgnoreLineOfSight&&
           !localLineOfSightReady(collision(),p.mapId,p.x,p.y,p.z,n->x,n->y,n->z,targetReach))
            return reject("Target is not in line of sight");
    }
    if(d->comboProfile||d->meleeSpecialProfile||d->stormstrikeProfile==1){
        if(p.flight.active||p.transportEntry)return reject("Cannot use combo attacks while travelling");
        if(!n||!localComboFacingReady(p,*n,d->requiresBehind))return reject(d->requiresBehind?"Stand behind the enemy and face it":"Face the enemy");
        if(d->comboFinisher&&!localComboTargetValid(p,n))return reject("Build combo points on this enemy first");
    }
    const auto* stormMain=d->stormstrikeProfile==1?c.spell(32175):nullptr;
    const auto* stormOff=d->stormstrikeProfile==1?c.spell(32176):nullptr;
    if(d->stormstrikeProfile==1) {
        if(!stormMain||!stormOff||!stormOff->unsupportedReason.empty()||!validLocalProc(*stormMain)||!validLocalProc(*stormOff)||
           !localStormstrikeWeaponReady(p,c,*stormMain,false))return reject("Stormstrike requires a complete weapon chain and a usable main hand weapon");
        if(!n||n->transportEntry||localStormstrikeSlot(*n,p.guid)>=kLocalMaxNpcStormstrikeAuras)return reject("No slot for the Stormstrike effect");
    }
    const uint8_t spentCombo=d->comboFinisher?p.comboPoints:0;
    const uint32_t extraEnergy=d->comboProfile==uint8_t(LocalComboProfile::FerociousBite)?std::min(30u,p.mana-cost):0;
    if(!validLocalProc(*d))return reject("Invalid proc definition");
    if(d->procCanCrit) {
        const auto* leaf=c.spell(d->proc.spellId);
        if(!leaf||!leaf->triggeredOnly||!leaf->unsupportedReason.empty()||!validLocalProc(*leaf)||
           !localDirectMagicCritEligible(*leaf)||leaf->damage!=d->proc.amount||
           leaf->schoolMask!=d->proc.schoolMask||leaf->spellFamily!=d->proc.spellFamily||
           leaf->spellFamilyFlags!=d->proc.spellFamilyFlags)
            return reject("Triggered retaliation chain is unavailable");
    }
    const auto* triggeredAura=d->triggeredAuraSpellId?c.spell(d->triggeredAuraSpellId):nullptr;
    if(d->meleeSpecialProfile>1 || (d->meleeSpecialProfile==1 &&
       (d->id!=23881 || d->comboProfile || d->damage!=50 || d->triggeredAuraSpellId!=23885)))
        return reject("Invalid melee special profile");
    if(d->triggeredAuraSpellId && (!d->meleeSpecialProfile || !triggeredAura || !triggeredAura->triggeredOnly ||
       !triggeredAura->unsupportedReason.empty() || !validLocalProc(*triggeredAura) ||
       triggeredAura->proc.effect!=LocalProcEffect::HealOwnerPctMaxHealth || !localHasTimedAura(*triggeredAura)))
        return reject("Triggered aura chain is unavailable");
    if(triggeredAura && p.statAuras.size()>=kLocalMaxStatAuras &&
       std::none_of(p.statAuras.begin(),p.statAuras.end(),[&](const auto& a){return a.spellId==triggeredAura->id;}))
        return reject("Too many active stat buffs");
    const bool buff=localHasTimedAura(*d);
    if(d->heal||d->periodicHeal||buff) {
        if(((buff&&d->buffSelfOnly)||(!buff&&d->healingSelfOnly))&&cmd.target&&cmd.target!=p.guid)return reject("This spell only heals its caster");
        healed=cmd.target?g.player(cmd.target,players):&p;if(!healed&&d->damage)healed=&p;
        if(!healed||healed->dead||!healed->health)return reject("Choose a living player");
        const auto* casterFaction=p.race<g.raceFactions.size()?definition(g.factions,g.raceFactions[p.race]):nullptr;
        const auto* targetFaction=healed->race<g.raceFactions.size()?definition(g.factions,g.raceFactions[healed->race]):nullptr;
        if(casterFaction&&targetFaction&&
           (factionRelation(*casterFaction,*targetFaction)>0||factionRelation(*targetFaction,*casterFaction)>0))
            return reject("Choose a friendly player");
        if(!localSpellTargetInRange(p,c,*d,healed->mapId,healed->instanceId,healed->x,healed->y,healed->z,
                                    kLocalDefaultCombatReach,!finishing))
            return reject("Healing target out of range");
        // The same facing arm: Spell::CheckRange runs it for every unit target
        // that is not the caster, friendly ones included (:7345-7361). A player's
        // UNIT_FIELD_BOUNDINGRADIUS is DEFAULT_WORLD_OBJECT_SIZE (Player.h:1103),
        // which IsWithinBoundaryRadius floors at MIN_MELEE_REACH anyway.
        if(healed!=&p&&localSpellRequiresFacing(*d)&&
           !localSpellFacingReady(p.x,p.y,p.orientation,healed->x,healed->y,healed->z,p.z,kLocalWorldObjectSize))
            return reject("Face your target");
        // The same Spell.cpp:6092-6118 gate for a friendly unit target: the
        // reference makes no distinction, and both ends are players here, so
        // both contribute their raw position plus the collision height
        // (Object.cpp:1418-1431).
        if(healed!=&p&&!d->sourceIgnoreLineOfSight&&healed->mapId==p.mapId&&healed->instanceId==p.instanceId&&
           !localLineOfSightReadyPlayers(collision(),p.mapId,p.x,p.y,p.z,healed->x,healed->y,healed->z))
            return reject("Target is not in line of sight");
        // The player arm of SpellInfo::CheckTargetCreatureType: a player's type
        // is its active form's, else humanoid (Unit.cpp:11475-11483).
        const auto* healedForm=localActiveForm(*healed);
        if(!localSpellCreatureTypeAllowed(*d,localFormCreatureType(healedForm?healedForm->form:0),true))
            return reject(d->name+": that target is not a valid creature type for this spell");
    }
    // --- competing auras (P04, the implementation) --------------------------------------
    // The reference decides in two steps (the source audit
    // section 6): the same id from the same caster refreshes in place
    // (Unit::_TryStackingOrRefreshingExistingAura, Unit.cpp:4350-4407; stat
    // auras are keyed by spell alone here, which has the same outcome because a
    // second caster's same-id aura replaces the first's, SpellAuras.cpp:2160-2177);
    // otherwise a new aura is created and every existing aura it cannot stack
    // with is REMOVED (Unit::_RemoveNoStackAurasDueToAura, :4662-4683, through
    // Aura::CanStackWith, SpellAuras.cpp:2027-2180). A lower rank over a higher
    // one therefore replaces it; there is no "higher rank is already active"
    // refusal anywhere in the reference. The only cast-time refusal for a
    // competing aura is SPELL_FAILED_AURA_BOUNCED for a stronger
    // SPELL_GROUP_STACK_RULE_EXCLUSIVE_HIGHEST group-mate (Spell::CheckCast,
    // Spell.cpp:6996-7001; Thorns, Mark of the Wild, Death Wish among the
    // accepted set). The slot found here is the first aura the new one
    // displaces (the refreshed one when there is one); any further displaced
    // entries are swept at commit.
    const auto stackSlot=[&](size_t count,auto spellAt,auto casterAt,auto matchesTarget) {
        size_t slot=count;bool refresh=false;
        for(size_t i=0;i<count;++i) {
            if(!matchesTarget(i))continue;
            if(spellAt(i)==d->id&&casterAt(i)==p.guid){slot=i;refresh=true;break;}
            const auto* old=c.spell(spellAt(i));
            if(!refresh&&slot==count&&old&&!localAuraCanStackWith(c,*d,p.guid,*old,casterAt(i)))slot=i;
        }
        return slot;
    };
    // Unit::_RemoveNoStackAurasDueToAura, Unit.cpp:4680-4683: once the new aura
    // is in place, every OTHER aura on the target it cannot stack with is
    // removed. Erases in reverse so `keep` (the new aura's slot) stays valid;
    // returns its final index.
    const auto sweepNoStack=[&](auto& entries,size_t keep,auto spellAt,auto casterAt,auto matchesTarget,auto onRemove) {
        for(size_t i=entries.size();i-- >0;) {
            if(i==keep||!matchesTarget(i))continue;
            const auto* old=c.spell(spellAt(i));
            if(!old||localAuraCanStackWith(c,*d,p.guid,*old,casterAt(i)))continue;
            onRemove(i);entries.erase(entries.begin()+ptrdiff_t(i));
            if(i<keep)--keep;
        }
        return keep;
    };
    size_t buffSlot=buff?healed->statAuras.size():0;
    if(buff){
        if(!d->durationMs||d->durationMs>3600000)return reject("Invalid buff duration");
        for(const auto& a:healed->statAuras) {
            const auto* old=c.spell(a.spellId);
            if(!old||a.spellId==d->id)continue;
            if(localExclusiveHighestBounce(*d,localStatAuraExclusiveAmount(p,c,*d,nullptr),localAuraEffectCount(*d),
                                           *old,localStatAuraExclusiveAmount(p,c,*old,&a),localAuraEffectCount(*old)))
                return reject("A more powerful spell is already active");
        }
        buffSlot=stackSlot(healed->statAuras.size(),[&](size_t i){return healed->statAuras[i].spellId;},
            [&](size_t i){const auto caster=healed->statAuras[i].casterGuid;
                // Stat auras refresh by spell whoever cast them (rule 7 of the
                // audit's section 8.1); the caster key only feeds CanStackWith.
                return healed->statAuras[i].spellId==d->id?p.guid:(caster?caster:healed->guid);},
            [](size_t){return true;});
        if(buffSlot==healed->statAuras.size()&&healed->statAuras.size()>=kLocalMaxStatAuras)return reject("Too many active stat buffs");
    }
    size_t damageSlot=g.periodicDamage.size();
    if(d->periodicDamage){
        if(!d->durationMs||d->durationMs>600000||!d->periodicIntervalMs||d->periodicIntervalMs>d->durationMs)
            return reject("Invalid periodic damage duration or interval");
        size_t onTarget=0;
        for(const auto& a:g.periodicDamage)if(a.target==cmd.target)++onTarget;
        damageSlot=stackSlot(g.periodicDamage.size(),[&](size_t i){return g.periodicDamage[i].spell;},
            [&](size_t i){return g.periodicDamage[i].owner;},[&](size_t i){return g.periodicDamage[i].target==cmd.target;});
        if(damageSlot==g.periodicDamage.size()&&(g.periodicDamage.size()>=MaxNpcs*8||onTarget>=8))
            return reject("Too many active periodic damage effects");
    }
    size_t healSlot=g.periodicHeals.size();
    if(d->periodicHeal) {
        if(!d->durationMs||d->durationMs>600000||!d->periodicIntervalMs||d->periodicIntervalMs>d->durationMs)
            return reject("Invalid periodic healing duration or interval");
        size_t onTarget=0;
        for(const auto& a:g.periodicHeals)if(a.target==healed->guid)++onTarget;
        healSlot=stackSlot(g.periodicHeals.size(),[&](size_t i){return g.periodicHeals[i].spell;},
            [&](size_t i){return g.periodicHeals[i].owner;},[&](size_t i){return g.periodicHeals[i].target==healed->guid;});
        if(healSlot==g.periodicHeals.size()&&
           (g.periodicHeals.size()>=MaxPeriodicHeals||onTarget>=MaxPeriodicHealsPerTarget))
            return reject("Too many active periodic healing effects");
    }
    size_t controlSlot=n?n->controls.size():0;
    if(d->controlProfile) {
        if(!n||n->transportEntry||!d->durationMs||d->durationMs>600000)
            return reject("Invalid NPC control target or duration");
        // No caster filter: a second caster's rank of the same control replaces
        // the first caster's (CanStackWith step b.9 exempts only periodic,
        // channelled and DOT_STACKING_RULE auras from a different caster).
        controlSlot=stackSlot(n->controls.size(),[&](size_t i){return n->controls[i].spellId;},
            [&](size_t i){return n->controls[i].casterGuid;},[](size_t){return true;});
        if(controlSlot==n->controls.size()&&n->controls.size()>=kLocalMaxNpcControls)
            return reject("Too many active NPC controls");
    }
    size_t snareSlot=n?n->snares.size():0;
    if(d->snarePercent) {
        if(!n||n->transportEntry||!d->durationMs||d->durationMs>600000||d->snarePercent>99)
            return reject("Invalid NPC snare target or duration");
        snareSlot=stackSlot(n->snares.size(),[&](size_t i){return n->snares[i].spellId;},
            [&](size_t i){return n->snares[i].casterGuid;},[](size_t){return true;});
        if(snareSlot==n->snares.size()&&n->snares.size()>=kLocalMaxNpcSnares)
            return reject("Too many active NPC snares");
    }
    std::array<LocalRealmNpc*,MaxNpcs> chainNpcs{};std::array<LocalRealmPlayer*,3> chainPlayers{};
    size_t chainCount=1;
    chainNpcs[0]=n;chainPlayers[0]=healed;
    if(d->areaRadius) {
        if(!std::isfinite(d->areaRadius)||d->areaRadius<=0||d->areaRadius>30||!d->damage||d->heal||d->chainTargets!=1||
           d->periodicDamage||d->periodicHeal||buff||d->snarePercent)return reject("Invalid caster-area spell profile");
        chainCount=0;
        for(auto& candidate:g.npcs) {
            if(candidate.dead||!candidate.health||candidate.mapId!=p.mapId||candidate.instanceId!=p.instanceId||!canAttack(p,candidate))continue;
            const auto dist=distance2(p,candidate);
            if(std::isfinite(dist)&&dist<=d->areaRadius*d->areaRadius&&chainCount<chainNpcs.size())chainNpcs[chainCount++]=&candidate;
        }
        std::sort(chainNpcs.begin(),chainNpcs.begin()+chainCount,[](auto* a,auto* b){return a->guid<b->guid;});
    }
    if(d->chainTargets>1) {
        if(d->chainTargets>3||!std::isfinite(d->chainRadius)||d->chainRadius<=0||d->chainRadius>12.5f||
           d->chainMultiplierPermille>1000||!d->chainMultiplierPermille||d->periodicDamage||d->periodicHeal||buff||d->snarePercent||
           (!!d->damage==!!d->heal))return reject("Invalid chain spell profile");
        for(;chainCount<d->chainTargets;++chainCount) {
            if(d->damage) {
                const auto* from=chainNpcs[chainCount-1];LocalRealmNpc* best=nullptr;float nearest=d->chainRadius*d->chainRadius;
                for(auto& candidate:g.npcs) {
                    if(candidate.dead||!candidate.health||candidate.mapId!=p.mapId||candidate.instanceId!=p.instanceId||!canAttack(p,candidate)||
                       std::find(chainNpcs.begin(),chainNpcs.begin()+chainCount,&candidate)!=chainNpcs.begin()+chainCount)continue;
                    const auto dist=distance2(candidate.x,candidate.y,candidate.z,from->x,from->y,from->z);
                    if(std::isfinite(dist)&&(dist<nearest||(dist==nearest&&(!best||candidate.guid<best->guid)))) {best=&candidate;nearest=dist;}
                }
                if(!best)break;chainNpcs[chainCount]=best;
            } else {
                const auto* from=chainPlayers[chainCount-1];LocalRealmPlayer* best=nullptr;uint32_t deficit=0;
                for(auto* candidate:players) {
                    if(!candidate||candidate->dead||!candidate->health||candidate->health>=candidate->maxHealth||candidate->mapId!=p.mapId||candidate->instanceId!=p.instanceId||
                       std::find(chainPlayers.begin(),chainPlayers.begin()+chainCount,candidate)!=chainPlayers.begin()+chainCount)continue;
                    const auto dist=distance2(candidate->x,candidate->y,candidate->z,from->x,from->y,from->z);
                    if(!std::isfinite(dist)||dist>d->chainRadius*d->chainRadius)continue;
                    const auto* casterFaction=p.race<g.raceFactions.size()?definition(g.factions,g.raceFactions[p.race]):nullptr;
                    const auto* targetFaction=candidate->race<g.raceFactions.size()?definition(g.factions,g.raceFactions[candidate->race]):nullptr;
                    if(!casterFaction||!targetFaction||factionRelation(*casterFaction,*targetFaction)>0||factionRelation(*targetFaction,*casterFaction)>0)continue;
                    const auto missing=candidate->maxHealth-candidate->health;
                    if(missing>deficit||(missing==deficit&&(!best||candidate->guid<best->guid))) {best=candidate;deficit=missing;}
                }
                if(!best)break;chainPlayers[chainCount]=best;
            }
        }
    }
    if(!localArcaneBlastCanApply(p,c,*d))return reject("No slot for the Arcane Blast effect");
    const bool reserveArcaneAura=d->arcaneBlastProfile==1&&std::none_of(p.statAuras.begin(),p.statAuras.end(),
        [](const auto& a){return a.spellId==36032&&a.remainingMs;});
    const auto castTime=localSpellCastTime(p,c,*d);
    if(!finishing&&castTime) {
        prepareLocalSpellCost(p,cost,appliedCostAura);
        p.castingSpellId=d->id;p.castTarget=cmd.target;p.castRemainingMs=p.castTotalMs=castTime;
        if(!++p.castSequence)++p.castSequence;
        p.castPushbackMs=0;p.castPushbackCount=0;
        p.castOriginX=p.x;p.castOriginY=p.y;p.castOriginZ=p.z;p.castOriginMap=p.mapId;p.castOriginInstance=p.instanceId;
        p.castStatus=LocalCastStatus::Casting;p.globalCooldownMs=d->globalCooldownMs;
        result="Casting "+d->name;return true;
    }
    // Secure an effect slot before resources or direct effects commit. Casts
    // with a cast time repeat the same validation at completion.
    if(d->snarePercent&&snareSlot==n->snares.size())n->snares.reserve(n->snares.size()+1);
    if(d->controlProfile&&controlSlot==n->controls.size())n->controls.reserve(n->controls.size()+1);
    if(d->stormstrikeProfile==1)n->stormstrikeAuras.reserve(kLocalMaxNpcStormstrikeAuras);
    if(triggeredAura||d->arcaneBlastProfile==1)p.statAuras.reserve(kLocalMaxStatAuras);
    if(buff&&buffSlot==healed->statAuras.size())healed->statAuras.reserve(healed->statAuras.size()+1);
    if(d->periodicDamage&&damageSlot==g.periodicDamage.size())g.periodicDamage.reserve(g.periodicDamage.size()+1);
    if(d->periodicHeal)healed->healingAuras.reserve(kLocalMaxHealingAuraViews);
    if(d->periodicHeal&&healSlot==g.periodicHeals.size())g.periodicHeals.reserve(g.periodicHeals.size()+1);
    if(cooldown&&cooldownSlot==p.cooldowns.size())p.cooldowns.reserve(p.cooldowns.size()+1);
    if(categoryCooldown&&categorySlot==p.categoryCooldowns.size())p.categoryCooldowns.reserve(p.categoryCooldowns.size()+1);
    struct BuffReservation {
        Impl& g;uint64_t prior;
        BuffReservation(Impl& state,uint64_t owner):g(state),prior(state.pendingStatAuraOwner){g.pendingStatAuraOwner=owner;}
        ~BuffReservation(){g.pendingStatAuraOwner=prior;}
    } buffReservation(g,buff&&(buffSlot==healed->statAuras.size()||!healed->statAuras[buffSlot].remainingMs)?healed->guid:reserveArcaneAura?p.guid:0);
    if(!finishing)prepareLocalSpellCost(p,cost,appliedCostAura);
    const bool meleeSpecial=d->comboProfile||d->meleeSpecialProfile||d->stormstrikeProfile==1;
    // P05-1 : a hostile-targeted DmgClass 2 definition with no profile
    // rolls MeleeSpellHitResult too (WorldObject::SpellHitResult dispatches
    // MELEE and RANGED there, Object.cpp:3719-3723). previously these 37
    // accepted spells - Shield Slam, Mongoose Bite, Lacerate, Rend, Bash, Sap,
    // Heroic Throw, the Judgements, Shield of Righteousness - took an
    // unconditional Hit and could never crit (audit section 2.3, which counted
    // 39: the two seal DoTs Holy Vengeance / Blood Corruption carry a
    // 0-base-point periodic effect, apply nothing here and never reach the
    // hostile target gate).
    const bool meleeClassRoll=!meleeSpecial&&n&&localMeleeClassSpellRoll(*d);
    const bool meleeRoll=meleeSpecial||meleeClassRoll;
    // A hostile magic spell rolls hit in the player-to-creature direction. Until
    // the implementation it could not miss at any level difference, which was a behavioural
    // defect rather than an unimplemented feature. A positive spell never rolls:
    // the reference returns SPELL_MISS_NONE for one on a non-hostile target.
    const bool magicHitRoll=!meleeRoll&&n&&d->clientSpell&&d->sourceDamageClass==1&&
        (d->damage||d->periodicDamage||d->snarePercent||d->controlProfile)&&!d->heal&&!d->periodicHeal;
    // P04 creature template immunity. WorldObject::SpellHitResult asks
    // Creature::IsImmunedToSpell FIRST (Object.cpp:3746-3751), before the melee
    // or magic roll and before Spell::DoSpellHitOnUnit ever reads a diminishing
    // record (Spell.cpp:3195), so an immune creature neither rolls nor seeds a
    // record. The caster is hostile to every legal target of a hostile cast
    // (canAttack above), which is the `casterFriendly` clause's constant. The
    // cost is paid below exactly as for a miss: there is no SPELL_FAILED_IMMUNE.
    const auto* targetDefinition=n?c.npc(n->entry):nullptr;
    const bool hostileCast=n&&!n->dead&&(d->damage||d->periodicDamage||d->snarePercent||d->controlProfile||meleeSpecial||d->dispelProfile);
    const bool templateImmune=hostileCast&&targetDefinition&&localNpcImmuneToSpell(*targetDefinition,*d,false);
    // Spell.cpp:2413-2416: the effect slots the template strips from a cast
    // that still lands. Bit k is column 71+k; the snare rides slot 0 on both
    // Frostbolt and Frost Shock, the periodic and direct amounts name theirs.
    const uint8_t strippedEffects=hostileCast&&targetDefinition&&!templateImmune?localNpcStrippedEffects(*targetDefinition,*d):0;
    if(templateImmune)LOG_INFO("[LOCAL_IMMUNE] npc=",n->guid," entry=",n->entry," spell=",d->id," mechanic=",unsigned(d->mechanic),
                               " school=",d->schoolMask," set school=",unsigned(targetDefinition->immuneSchoolMask));
    else if(strippedEffects)LOG_INFO("[LOCAL_IMMUNE] stripped npc=",n->guid," entry=",n->entry," spell=",d->id," effects=",unsigned(strippedEffects));
    // The attribute arms of MeleeSpellHitResult ride the same roll for every
    // melee-class cast, profiled or not: none of the 79 profiled rows carries
    // ALWAYS_HIT or NO_ACTIVE_DEFENSE (the Stormstrike children have their own
    // ALWAYS_HIT site), the five Rake ranks carry COMPLETELY_BLOCKED but also
    // a school damage effect, which cancels the full block at the pin
    // (Unit.cpp:3351, localSpellFullyBlockable).
    const LocalMeleeSpellRules meleeRules{d->sourceAlwaysHit,d->sourceNoActiveDefense,localSpellFullyBlockable(*d)};
    auto meleeOutcome=templateImmune?LocalMeleeOutcome::Immune:
        meleeRoll?localRollPlayerMelee(p,*n,localMeleeStats(p,c),true,g.meleeRoll(),g.meleeRoll(),false,meleeRules):
        magicHitRoll?g.playerSpellHitOutcome(p,*n,*d):LocalMeleeOutcome::Hit;
    // A full block (only reachable through localSpellFullyBlockable) is a
    // SPELL_MISS_BLOCK: no effect lands, and Spell::TakePower charges it in
    // full (Spell.cpp:5496, BLOCK is excluded from the refund).
    const bool fullyBlocked=meleeRoll&&meleeOutcome==LocalMeleeOutcome::Block;
    // P04 diminishing returns. Spell::DoSpellHitOnUnit runs this on a spell that
    // HIT, before the aura is applied, so the level is read and incremented here
    // and the resulting duration - or the immunity - is carried to the
    // application site below. The group is classified from the spell itself;
    // `triggered` is false because no proc in this build applies a control, so
    // every admitted control is a deliberate cast.
    auto diminishGroup=LocalDiminishingGroup::None;
    uint32_t diminishedDurationMs=d->durationMs;
    if(d->controlProfile&&n&&!n->dead&&!localOutcomeNullifiesDamage(meleeOutcome)) {
        diminishGroup=localDiminishingGroupForSpell(*d,false);
        const auto level=localDiminishingRead(*n,diminishGroup,g.authorityClockMs);
        const auto type=localDiminishingGroupType(diminishGroup);
        // Spell.cpp:3207 tests the TARGET and :3211 the CASTER. Transposing them
        // inverts this build twice over, since every control here has a player
        // caster and a creature target. targetOptIn is the reference's
        // player-owned-or-ALL_DIMINISH test: LocalNpcDefinition carries no
        // flags_extra, and zero of this realm's 14,496 creature templates carry
        // ALL_DIMINISH upstream, so it is a constant false rather than a clause
        // quietly dropped. tauntOptIn is the same for OBEYS_TAUNT.
        constexpr bool targetOptIn=false,tauntOptIn=false;
        constexpr bool casterIsPlayer=true; // A creature never increments; none casts a control here.
        if(diminishGroup!=LocalDiminishingGroup::None&&
           ((type==LocalDiminishingType::Player&&targetOptIn)||type==LocalDiminishingType::All)&&
           casterIsPlayer)
            localDiminishingIncrement(*n,diminishGroup,g.authorityClockMs);
        // Unit.cpp:11338, the PvP duration clamp. No producer in this build and
        // none claimed; written at its real gate so the table is right if one
        // ever appears.
        const auto limit=localDiminishingLimitDuration(diminishGroup,*d);
        if(limit&&diminishedDurationMs>limit&&targetOptIn&&casterIsPlayer)diminishedDurationMs=limit;
        const float modifier=localDiminishingMultiplier(diminishGroup,level,targetOptIn,tauntOptIn);
        diminishedDurationMs=uint32_t(float(diminishedDurationMs)*modifier);
        // Spell.cpp:3275-3287: diminished to nothing is an immunity, not a
        // shorter control. Every admitted control is single-effect, so the
        // reference's "another effect still landed" escape cannot apply here.
        if(modifier==0.f) {
            meleeOutcome=LocalMeleeOutcome::Immune;
            LOG_INFO("[LOCAL_DR] immune npc=",n->guid," spell=",d->id," group=",int(diminishGroup));
        } else if(level)
            LOG_INFO("[LOCAL_DR] npc=",n->guid," spell=",d->id," group=",int(diminishGroup),
                     " level=",int(level)," duration=",diminishedDurationMs," of ",d->durationMs);
    }
    // Melee avoidance alone reduces a special's resource cost; a missed magic
    // spell pays in full, so this predicate stays melee-only on purpose.
    const bool avoided=meleeRoll&&localMeleeAvoided(meleeOutcome);
    const bool nullified=localOutcomeNullifiesDamage(meleeOutcome)||fullyBlocked;
    // Original rage is measured in tenths: draw before converting back to
    // this realm's whole displayed units. This preserves the source draw
    // distribution while the remaining fractional rage model stays deferred.
    // Spell::TakePower (Spell.cpp:5489-5500, :5528-5532) refunds an avoided
    // cast to irand(0, cost / 4) only for rage, energy, runes and runic power;
    // a mana special (Mongoose Bite) pays in full, as Stormstrike always did here.
    const bool costRefundsOnAvoid=d->resourceType==1||d->resourceType==3||d->resourceType==5||d->resourceType==6;
    const uint32_t paidCost=avoided&&costRefundsOnAvoid&&d->stormstrikeProfile!=1?(d->meleeSpecialProfile?g.meleeRoll(cost*10/4)/10:g.meleeRoll(cost/4)):cost;
    p.mountSpellId=d->mountDisplayId?d->id:0;
    if(d->formId&&p.classId==11&&p.resourceType!=LocalResourceType::Mana)p.druidMana-=paidCost;else p.mana-=paidCost;
    if(!avoided)p.mana-=extraEnergy;
    if(d->comboFinisher&&!avoided)clearLocalCombo(p);
    if(d->comboGain&&!avoided&&p.comboTarget!=cmd.target)clearLocalCombo(p);
    if(cost&&(p.resourceType==LocalResourceType::Mana||(d->formId&&p.classId==11)))p.manaRegenDelayMs=5000;
    if(d->formId){
        const auto* profile=localFormProfile(d->id);
        // The reference's entry rules run before the form's own aura is applied,
        // so the rage a warrior stance keeps is read off the OLD stance's pool.
        const auto entry=localFormEntryResource(p,c,*profile,g.meleeRoll(99));
        enterLocalForm(p,*profile,entry);stats(p,c,false);
        LOG_INFO("[LOCAL_FORM] player=",p.guid," spell=",d->id," power=",int(p.resourceType)," entry=",entry," hiddenMana=",p.druidMana);
    }
    consumeLocalRunes(p.runeCooldownMs,*runeMask);
    if(p.resourceType==LocalResourceType::RunicPower)
        p.mana=uint32_t(std::min(uint64_t(p.maxMana),uint64_t(p.mana)+d->runicPowerGain));
    if(!finishing)p.globalCooldownMs=d->globalCooldownMs;
    if(cooldown) {
        if(cooldownSlot<p.cooldowns.size())p.cooldowns[cooldownSlot]={cmd.id,cooldown};
        else p.cooldowns.push_back({cmd.id,cooldown});
    }
    if(categoryCooldown) {
        LocalCategoryCooldown a{d->cooldownCategory,d->spellFamily,categoryCooldown};
        if(categorySlot<p.categoryCooldowns.size())p.categoryCooldowns[categorySlot]=a;else p.categoryCooldowns.push_back(a);
    }
    // Successful cast phase precedes every hit callback. Its recorded modifier
    // identity spends only the charge used when this cast was prepared.
    LocalCombatEvent castEvent{0,p.guid,healed?healed->guid:cmd.target,d->id,p.mapId,p.instanceId,0,0,0,LocalCombatEventKind::SpellCast};
    castEvent.positiveSpell=bool(healed)||d->formId||d->mountDisplayId;
    castEvent.spellTypeMask=7; // Source CAST/FINISH have no DamageInfo or HealInfo.
    castEvent.attackType=meleeRoll?LocalCombatAttackType::Melee:
        d->clientSpell?localProcSpellAttackType(d->sourceDamageClass):LocalCombatAttackType::Magic;
    castEvent.appliedCostAuraSpell=p.castCostModSpellId;castEvent.appliedCostAuraGeneration=p.castCostModGeneration;
    g.emitCombatEvent(castEvent,players);
    // P05-6 : Spell::HandleThreatSpells (Spell.cpp:5759-5811), which
    // Spell::_handle_immediate_phase runs before any effect lands (:4365). The
    // initial threat - the spell_threat row's flat and attack-power terms, else
    // SpellLevel unless CU_NO_INITIAL_THREAT; nothing for NO_THREAT or
    // SUPPRESS_TARGET_PROCS - is divided by the number of targets
    // (m_UniqueTargetInfo.size()); a missed target gets 0 of it (:5792-5793,
    // Immune included since it is a miss condition); a positive spell forwards
    // its share through ForwardThreatForAssistingMe with the caster's
    // modifiers (:5800-5801), a negative one adds it to each target with
    // ignoreModifiers = true (:5808). previously no cast added any of it:
    // 605 accepted spells, 118 of them heals (audit section 3.3).
    {
        const auto* threatRow=localSpellThreatRow(*d);
        const float initial=localSpellInitialThreat(*d,threatRow&&threatRow->apPctMod!=0.f?localMeleeStats(p,c).attackPower:0.f);
        if(initial>0.f&&std::isfinite(initial)) {
            const uint64_t thousandths=uint64_t(std::min(1.0e9,double(initial)*1000.0));
            if(castEvent.positiveSpell) {
                const size_t recipients=healed?chainCount:1;
                const uint64_t share=localTalentThreat(p,c,d,thousandths/recipients);
                for(size_t i=0;i<recipients;++i) {
                    const auto* recipient=healed?chainPlayers[i]:&p;
                    if(recipient)g.forwardThreatForAssisting(recipient->guid,p.guid,share,p.mapId,p.instanceId,players);
                }
            } else if(hostileCast) {
                const uint64_t share=thousandths/chainCount;
                for(size_t i=0;i<chainCount;++i) {
                    auto* target=chainNpcs[i];if(!target||target->dead)continue;
                    if(i==0?nullified:[&]{const auto* def=c.npc(target->entry);return def&&localNpcImmuneToSpell(*def,*d,false);}())continue;
                    g.addThreat(*target,p.guid,share);g.selectThreatTarget(*target,players);
                }
            }
        }
    }
    bool parentTargetCritical=!avoided&&meleeOutcome==LocalMeleeOutcome::Critical;
    const auto emitFinish=[&] {
        // Source Spell::_handle_finish_phase runs after all target effects and
        // after-cast scripts. Ordinary casts have no FINISH victim, even when
        // their earlier hit had one. Child proc criticals are not parent hits.
        LocalCombatEvent event{0,p.guid,0,d->id,p.mapId,p.instanceId,0,0,0,LocalCombatEventKind::SpellFinish};
        event.positiveSpell=castEvent.positiveSpell;event.attackType=castEvent.attackType;
        event.spellTypeMask=7;event.outcome=parentTargetCritical?LocalMeleeOutcome::Critical:LocalMeleeOutcome::Hit;
        g.emitCombatEvent(event,players);
    };
    if(triggeredAura) {
        // Source immediate-effect phase arms Bloodthirst before resolving its
        // melee target. The parent hit can spend the first of the new charges;
        // a miss/dodge/parry leaves all three available for subsequent hits.
        auto existing=std::find_if(p.statAuras.begin(),p.statAuras.end(),
            [&](const auto& a){return a.spellId==triggeredAura->id;});
        LocalStatAura aura{triggeredAura->id,triggeredAura->durationMs,p.mapId,p.instanceId,p.guid};
        aura.procCharges=triggeredAura->proc.charges;
        aura.procAmountSnapshot=triggeredAura->proc.amount;aura.hasProcAmountSnapshot=true;
        localPrepareAuraApplication(p,aura,existing==p.statAuras.end()?nullptr:&*existing);
        if(existing==p.statAuras.end())p.statAuras.push_back(aura);else *existing=aura;
        LOG_INFO("[LOCAL_TRIGGER_AURA] caster=",p.guid," parent=",d->id," aura=",aura.spellId,
            " charges=",unsigned(aura.procCharges)," duration=",aura.remainingMs," before_target_hit=1");
    }
    if(avoided||nullified){
        // The parry nudge is a melee rule and stays gated on the melee outcome.
        if(meleeOutcome==LocalMeleeOutcome::Parry&&n->attackTimer>.4f&&!(localNpcMeleeFlags(n->entry)&8))n->attackTimer=std::max(.4f,n->attackTimer-.8f);
        g.damageNpc(*n,p,0,players,true,d->id,false,0,nullptr,meleeOutcome);
        p.attackTarget=n->guid;if(!++p.castRevision)++p.castRevision;p.lastCastSpellId=d->id;p.lastCastTarget=n->guid;
        clearCast(p,LocalCastStatus::Finished);
        emitFinish();
        result=avoided?"Attack avoided":meleeOutcome==LocalMeleeOutcome::Immune?"Immune":
            meleeOutcome==LocalMeleeOutcome::Resist?"Resisted":"Spell missed";return true;
    }
    if(d->stormstrikeProfile==1) {
        // LAUNCH_TARGET triggers both independent ALWAYS_HIT weapon children
        // before HIT_TARGET refreshes the parent target aura.
        for(unsigned hand=0;hand<2&&!n->dead;++hand) {
            const auto* child=hand?stormOff:stormMain;
            if(!localStormstrikeWeaponReady(p,c,*child,hand!=0))continue;
            const auto w=localWeaponAmounts(p,c,hand!=0,false,true);
            const auto outcome=localStormstrikeChildOutcome(p,*n,localMeleeStats(p,c),hand!=0,g.meleeRoll());
            const float multiplier=outcome==LocalMeleeOutcome::Critical?2.f:1.f;
            const auto amount=uint32_t((w.low+(w.high-w.low)*g.meleeRoll()/9999.f)*multiplier);
            const auto magic=uint32_t((w.magicLow+(w.magicHigh-w.magicLow)*g.meleeRoll()/9999.f)*multiplier);
            g.damageNpc(*n,p,amount,players,true,child->id,false,0,nullptr,outcome,hand!=0,0,magic,true);
        }
        if(!n->dead) {
            const auto slot=localStormstrikeSlot(*n,p.guid);
            LocalNpcStormstrikeAura aura{17364,12000,p.guid,4,p.positionRevision};
            if(slot<n->stormstrikeAuras.size())n->stormstrikeAuras[slot]=aura;else n->stormstrikeAuras.push_back(aura);
        }
    }
    // Spell.cpp:2413-2416 dropped the stripped slot from what this cast applies;
    // an aggregated amount (slot 255) has no single slot to strip and is
    // measured to carry no effect mechanic on any accepted definition.
    const bool directStripped=d->directEffectSlot<3&&(strippedEffects&(1u<<d->directEffectSlot));
    if(d->damage&&!directStripped) {
        uint32_t baseAmount=d->meleeSpecialProfile?localMeleeSpecialAmount(localMeleeStats(p,c).attackPower,d->damage):
            localComboAmount(p,c,*d,localSpellEffectAmountAfterTalents(p,c,*d,spellAmount(p,*d,false),false,spentCombo),spentCombo,extraEnergy,false,true);
        // Spell::EffectSchoolDMG's warrior branch (SpellEffects.cpp:360-369):
        // Shield Slam adds the caster's shield block value, soft-capped at
        // level x 24.5 and hard-capped at level x 34.5 (Unit.h:1181-1194), to
        // the effect amount before any damage bonus. Shield Block (2565) doubles
        // both caps and is not admitted, so `limit` is 1.
        if(d->spellFamily==4&&(d->spellFamilyFlags[1]&0x200u)&&d->cooldownCategory==1209)
            baseAmount=uint32_t(std::min<uint64_t>(1000000,uint64_t(baseAmount)+
                localShieldSlamBlockValue(localMeleeStats(p,c).shieldBlockValue,uint32_t(float(p.level)*24.5f),uint32_t(float(p.level)*34.5f))));
        uint32_t amount=localSpellAmountAfterTalents(p,c,*d,baseAmount,false);
        amount=localArcaneBlastDamage(p,c,*d,amount);
        // Unit::SpellCriticalDamageBonus (Unit.cpp:9117-9131): a MELEE-class
        // critical is +100 %, and it is the melee roll's own Critical outcome,
        // for the profiled specials and  the unprofiled melee-class
        // spells alike; rollSpellCritical below is the MAGIC-class roll and
        // is never eligible for a DmgClass 2 row.
        if((d->comboProfile||d->meleeSpecialProfile||meleeClassRoll)&&meleeOutcome==LocalMeleeOutcome::Critical)amount=localMeleeCriticalAmount(amount);
        if(d->areaRadius&&chainCount>10)amount=uint32_t(uint64_t(amount)*10/chainCount);
        for(size_t i=0;i<chainCount;++i) {
            // Spell::AddUnitTarget judges every target of a chain or an area
            // cast on its own template (Spell.cpp:2413-2416); the first target
            // was judged above, the rest are judged here.
            if(i) {
                const auto* chainDefinition=c.npc(chainNpcs[i]->entry);
                if(chainDefinition&&localNpcImmuneToSpell(*chainDefinition,*d,false)) {
                    g.damageNpc(*chainNpcs[i],p,0,players,true,d->id,false,0,nullptr,LocalMeleeOutcome::Immune);
                    amount=uint32_t(uint64_t(amount)*d->chainMultiplierPermille/1000);continue;
                }
            }
            // P05-2 : every physical MELEE / RANGED class spell rolls
            // the independent partial block of Unit::isSpellBlocked
            // (Unit.cpp:1524-1531, :3263-3287), so a critical can also be
            // partially blocked; previously only the Bloodthirst profile did.
            // The creature's block value is level / 2 (Creature.h:158-161
            // without the strength term this catalog does not carry).
            const uint32_t blockValue=localSpellPartialBlockApplies(*d)&&localRollMeleeSpecialBlock(p,*chainNpcs[i],g.meleeRoll())?localCreatureBlockValue(*chainNpcs[i]):0;
            const bool critical=g.rollSpellCritical(p,*d,chainNpcs[i]);
            parentTargetCritical=parentTargetCritical||critical;
            g.damageNpc(*chainNpcs[i],p,critical?localMagicCriticalAmount(amount):amount,players,
                (!d->clientSpell||(d->schoolMask&1))&&!d->directIgnoresArmor,d->id,false,0,nullptr,
                critical?LocalMeleeOutcome::Critical:meleeOutcome,false,blockValue);
            amount=uint32_t(uint64_t(amount)*d->chainMultiplierPermille/1000);
        }
    } else if(d->damage&&directStripped)
        LOG_INFO("[LOCAL_IMMUNE] direct damage stripped npc=",n->guid," spell=",d->id," slot=",unsigned(d->directEffectSlot));
    if(d->dispelProfile&&n&&!n->dead) {
        // Spell::EffectDispel (SpellEffects.cpp:2718-2831) against this realm's
        // creature containers: the candidate list of Unit::GetDispellableAuraList
        // is empty for an offensive magic dispel because every creature-held
        // aura is harmful, so no aura is drawn, none is removed and no
        // SMSG_SPELLDISPELLOG or SMSG_DISPEL_FAILED is sent - the reference's
        // own outcome against a buff-less creature. The count is computed, not
        // assumed, and logged.
        const auto candidates=localNpcDispellableAuraCount(*n,c,localDispelMask(1),false,d->sourceNoImmunities);
        LOG_INFO("[LOCAL_DISPEL] npc=",n->guid," spell=",d->id," attempts=",unsigned(d->dispelAttempts)," candidates=",candidates);
    }
    if(d->snarePercent&&!n->dead&&!(strippedEffects&1u)) {
        LocalNpcSnare a{d->id,d->durationMs,p.guid,d->snarePercent,p.positionRevision};
        if(snareSlot<n->snares.size())n->snares[snareSlot]=a;else{snareSlot=n->snares.size();n->snares.push_back(a);}
        sweepNoStack(n->snares,snareSlot,[&](size_t i){return n->snares[i].spellId;},
            [&](size_t i){return n->snares[i].casterGuid;},[](size_t){return true;},[](size_t){});
        if(!n->targetGuid)n->targetGuid=p.guid;
    }
    // A control that did not land applies nothing: Spell::DoSpellHitOnUnit is
    // only reached for a target the spell actually hit, so a missed stun is a
    // missed stun rather than a shorter one, and a control diminished to nothing
    // has already set the outcome to Immune above.
    if(d->controlProfile&&!n->dead&&!nullified&&!(strippedEffects&(1u<<d->controlEffectSlot))) {
        LocalNpcControl a{d->id,diminishedDurationMs,p.guid,p.positionRevision,
            uint8_t(d->controlProfile==2?LocalNpcControlKind::Silence:LocalNpcControlKind::Stun)};
        const bool replacing=controlSlot<n->controls.size();
        auto replacedGroup=LocalDiminishingGroup::None;
        if(replacing) {
            if(const auto* previous=c.spell(n->controls[controlSlot].spellId))
                replacedGroup=localDiminishingGroupForSpell(*previous,false);
            n->controls[controlSlot]=a;
        } else {controlSlot=n->controls.size();n->controls.push_back(a);}
        // A further displaced control (only possible from a state the rule no
        // longer admits) leaves the way an expired one does: its group's stack
        // steps down (Unit::ApplyDiminishingAura(group, false)).
        sweepNoStack(n->controls,controlSlot,[&](size_t i){return n->controls[i].spellId;},
            [&](size_t i){return n->controls[i].casterGuid;},[](size_t){return true;},[&](size_t i){
                if(const auto* removed=c.spell(n->controls[i].spellId))
                    localDiminishingApply(*n,localDiminishingGroupForSpell(*removed,false),false,g.authorityClockMs);});
        // A replacement within one group is a refresh, not a removal and a
        // re-application: _TryStackingOrRefreshingExistingAura never calls
        // ApplyDiminishingAura again, so the stack must not dip to zero and
        // restamp the removal time on a rank upgrade.
        if(replacedGroup!=diminishGroup) {
            if(replacedGroup!=LocalDiminishingGroup::None)
                localDiminishingApply(*n,replacedGroup,false,g.authorityClockMs);
            localDiminishingApply(*n,diminishGroup,true,g.authorityClockMs);
        }
        if(!n->targetGuid)n->targetGuid=p.guid;
        // A stun stops the creature where it stands; the reference clears its
        // movement before the aura is even visible.
        if(a.kind==uint8_t(LocalNpcControlKind::Stun))localResetNpcSpellState(*n);
    }
    if(healed&&d->heal){
        uint32_t amount=localSpellAmountAfterTalents(p,c,*d,localSpellEffectAmountAfterTalents(p,c,*d,spellAmount(p,*d,true),false),false);
        for(size_t i=0;i<chainCount;++i) {
            auto* recipient=chainPlayers[i];const auto before=recipient->health;
            const bool critical=g.rollSpellCritical(p,*d);
            parentTargetCritical=parentTargetCritical||critical;
            const auto healedAmount=critical?localMagicCriticalAmount(amount):amount;
            recipient->health=uint32_t(std::min(uint64_t(recipient->maxHealth),uint64_t(recipient->health)+healedAmount));
            g.emitCombatEvent({0,p.guid,recipient->guid,d->id,p.mapId,p.instanceId,healedAmount,recipient->health-before,0,
                LocalCombatEventKind::DirectHeal,false,0,critical?LocalMeleeOutcome::Critical:LocalMeleeOutcome::Hit},players);
            amount=uint32_t(uint64_t(amount)*d->chainMultiplierPermille/1000);
        }
    }
    if(d->periodicHeal) {
        Impl::PeriodicHeal aura{p.guid,healed->guid,d->id,d->durationMs,d->periodicIntervalMs,d->periodicIntervalMs,
            localSpellAmountAfterTalents(p,c,*d,localSpellEffectAmountAfterTalents(p,c,*d,scaledSpellAmount(p,*d,d->periodicHeal,d->periodicHealMax,d->periodicHealPerLevel),true),true),p.mapId,p.instanceId};
        aura.critChanceBasisPoints=localPeriodicCritChanceBasisPoints(&p,c,*d);
        if(healSlot<g.periodicHeals.size())
            aura.stacks=nextLocalAuraStack(g.periodicHeals[healSlot].spell,g.periodicHeals[healSlot].stacks,*d);
        if(healSlot<g.periodicHeals.size())g.periodicHeals[healSlot]=aura;
        else{healSlot=g.periodicHeals.size();g.periodicHeals.push_back(aura);}
        sweepNoStack(g.periodicHeals,healSlot,[&](size_t i){return g.periodicHeals[i].spell;},
            [&](size_t i){return g.periodicHeals[i].owner;},[&](size_t i){return g.periodicHeals[i].target==healed->guid;},[](size_t){});
    }
    if(d->comboProfile&&!d->damage){g.addThreat(*n,p.guid,1);g.selectThreatTarget(*n,players);
        g.emitCombatEvent({0,p.guid,n->guid,d->id,p.mapId,p.instanceId,0,0,0,LocalCombatEventKind::SpellDamage,false,0,LocalMeleeOutcome::Hit},players);}
    const bool periodicStripped=d->periodicEffectSlot<3&&(strippedEffects&(1u<<d->periodicEffectSlot));
    if(d->periodicDamage&&n&&!n->dead&&periodicStripped)
        LOG_INFO("[LOCAL_IMMUNE] periodic stripped npc=",n->guid," spell=",d->id," slot=",unsigned(d->periodicEffectSlot));
    if(d->periodicDamage&&n&&!n->dead&&!periodicStripped) {

        Impl::PeriodicDamage aura{p.guid,n->guid,d->id,d->durationMs,d->periodicIntervalMs,d->periodicIntervalMs,localSpellAmountAfterTalents(p,c,*d,localComboAmount(p,c,*d,localSpellEffectAmountAfterTalents(p,c,*d,scaledSpellAmount(p,*d,d->periodicDamage,d->periodicDamageMax,d->periodicDamagePerLevel),true,spentCombo),spentCombo,0,true,true),true),p.mapId,p.instanceId};
        if(d->schoolMask&1)aura.damage=localPhysicalDamageAfterTalents(p,c,aura.damage);
        aura.targetEpoch=n->combatEpoch;
        aura.critChanceBasisPoints=localPeriodicCritChanceBasisPoints(&p,c,*d);
        if(damageSlot<g.periodicDamage.size())
            aura.stacks=nextLocalAuraStack(g.periodicDamage[damageSlot].spell,g.periodicDamage[damageSlot].stacks,*d);
        if(damageSlot<g.periodicDamage.size())g.periodicDamage[damageSlot]=aura;else{damageSlot=g.periodicDamage.size();g.periodicDamage.push_back(aura);}
        sweepNoStack(g.periodicDamage,damageSlot,[&](size_t i){return g.periodicDamage[i].spell;},
            [&](size_t i){return g.periodicDamage[i].owner;},[&](size_t i){return g.periodicDamage[i].target==n->guid;},[](size_t){});
    }
    if(buff){
        // Cast-phase charge removal and direct-hit procs may erase or append
        // aura entries. Resolve the reserved buff by identity again, never by
        // an index captured before those callbacks.
        buffSlot=stackSlot(healed->statAuras.size(),[&](size_t i){return healed->statAuras[i].spellId;},
            [&](size_t i){const auto caster=healed->statAuras[i].casterGuid;
                return healed->statAuras[i].spellId==d->id?p.guid:(caster?caster:healed->guid);},
            [](size_t){return true;});
        if(buffSlot==healed->statAuras.size())for(size_t i=0;i<healed->statAuras.size();++i)
            if(!healed->statAuras[i].remainingMs){buffSlot=i;break;}
        LocalStatAura a{d->id,d->durationMs,healed->mapId,healed->instanceId,p.guid,d->buffAbsorb};
        if(d->buffArmor)a.buffArmorSnapshot=uint32_t(std::min(uint64_t(1000000),
            uint64_t(d->buffArmor)*uint32_t(100+localTalentCastModifier(p,c,*d,8,true))/100));
        if(buffSlot<healed->statAuras.size())
            a.stacks=nextLocalAuraStack(healed->statAuras[buffSlot].spellId,healed->statAuras[buffSlot].stacks,*d);
        a.absorbRemaining=d->wardProfile?localWardAbsorbAmount(p,c,*d):localStackedAuraAmount(d->buffAbsorb,a.stacks);
        a.reflectChanceBasisPointsSnapshot=d->wardProfile?localWardReflectChanceBasisPoints(p,c,*d):0;
        a.procCharges=d->proc.charges;
        if(d->proc.effect!=LocalProcEffect::None){a.procAmountSnapshot=localProcAmountAtApplication(p,c,*d);a.hasProcAmountSnapshot=true;}
        if(buffSlot<healed->statAuras.size()&&d->proc.charges){
            const auto& prior=healed->statAuras[buffSlot];
            a.procCooldownMs=std::min(prior.procCooldownMs,d->proc.cooldownMs);
            if(prior.spellId==d->id)a.manaRegenRemainder=prior.manaRegenRemainder;
        }
        // A caster can maintain Earth Shield on one friendly recipient.
        // Transfer preserves any active ICD instead of granting a free proc.
        if(localEarthShield(*d))for(auto* priorOwner:g.auraOwners(players))if(priorOwner&&priorOwner!=healed) {
            for(auto& prior:priorOwner->statAuras) {
                const auto* old=c.spell(prior.spellId);
                if(old&&localEarthShield(*old)&&(prior.casterGuid?prior.casterGuid:priorOwner->guid)==p.guid) {
                    a.procCooldownMs=std::max(a.procCooldownMs,std::min(prior.procCooldownMs,d->proc.cooldownMs));prior.remainingMs=0;
                }
            }
            std::erase_if(priorOwner->statAuras,[](const auto& prior){return !prior.remainingMs;});
        }
        localPrepareAuraApplication(*healed,a,buffSlot<healed->statAuras.size()?&healed->statAuras[buffSlot]:nullptr);
        if(buffSlot<healed->statAuras.size())healed->statAuras[buffSlot]=a;else{buffSlot=healed->statAuras.size();healed->statAuras.push_back(a);}
        // The sweep is CanStackWith: SPELL_SPECIFIC_MAGE_ARMOR and
        // SPELL_SPECIFIC_ELEMENTAL_SHIELD (SpellInfo::IsAuraExclusiveBySpecificWith)
        // are two of its values, not hand rules any more. A shaman has one
        // elemental shield; replacing it preserves an outstanding internal
        // cooldown rather than enabling rapid procs.
        uint32_t shieldCooldown=a.procCooldownMs;
        buffSlot=sweepNoStack(healed->statAuras,buffSlot,[&](size_t i){return healed->statAuras[i].spellId;},
            [&](size_t i){const auto caster=healed->statAuras[i].casterGuid;return caster?caster:healed->guid;},
            [](size_t){return true;},[&](size_t i){
                const auto* old=c.spell(healed->statAuras[i].spellId);
                if(old&&localElementalShield(*d)&&localElementalShield(*old))shieldCooldown=std::max(shieldCooldown,healed->statAuras[i].procCooldownMs);});
        if(localElementalShield(*d))healed->statAuras[buffSlot].procCooldownMs=std::min(shieldCooldown,d->proc.cooldownMs);
        stats(*healed,c,false);
    }
    if((d->comboProfile||d->meleeSpecialProfile||d->stormstrikeProfile==1)&&n&&!n->dead){
        if(d->comboGain)addLocalCombo(p,*n,d->comboGain);
        p.attackTarget=n->guid;
    }
    if(d->summonPetEntry&&!g.summonPet(p,*d,players))return reject("The summon could not be created");
    if(d->areaAuraProfile) {
        // IsAuraExclusiveBySpecificPerCasterWith: one aura of this exclusivity
        // group per caster. A new rank replaces the old emitter rather than
        // adding a second, and the derived applications follow on the next pass.
        const auto group=localAreaAuraGroup(*d);
        const LocalAreaAuraEmitter emitter{d->id,p.mapId,p.instanceId,
            d->areaAuraAmounts[0]>0?uint32_t(d->areaAuraAmounts[0]):0u,
            d->areaAuraEffectMask,g.allocateAreaAuraGeneration()};
        // Validate and reserve capacity BEFORE displacing anything: a failed
        // cast must never leave the caster with the old aura cancelled. The
        // replaced emitter is counted out of the bound, because replacing is
        // not adding.
        const auto displaced=size_t(std::count_if(p.areaEmitters.begin(),p.areaEmitters.end(),
            [&](const LocalAreaAuraEmitter& e) {
                const auto* existing=c.spell(e.spellId);
                return !existing||localAreaAuraGroup(*existing)==group;
            }));
        if(!validLocalAreaAuraEmitter(emitter)||p.areaEmitters.size()-displaced>=kLocalMaxAreaAuraEmitters)
            return reject("Too many active area auras");
        std::erase_if(p.areaEmitters,[&](const LocalAreaAuraEmitter& e) {
            const auto* existing=c.spell(e.spellId);
            return !existing||localAreaAuraGroup(*existing)==group;
        });
        p.areaEmitters.push_back(emitter);
        g.reconcileAreaAuras(players);
        LOG_INFO("[LOCAL_AREA_AURA] owner=",p.guid," spell=",d->id," action=activate amount=",emitter.amount,
            " effects=",unsigned(emitter.effectMask)," radius=",d->areaAuraRadius);
    }
    g.refreshHealingViews(players);
    if (!++p.castRevision) ++p.castRevision;
    p.lastCastSpellId=d->id;p.lastCastTarget=healed?healed->guid:cmd.target;
    clearCast(p,LocalCastStatus::Finished);
    if(!d->damage&&!d->heal) {
        LocalCombatEvent hitEvent{0,p.guid,healed?healed->guid:cmd.target,d->id,p.mapId,p.instanceId,0,0,0,LocalCombatEventKind::SpellHit};
        hitEvent.positiveSpell=bool(healed)||d->formId||d->mountDisplayId;hitEvent.spellTypeMask=4;
        hitEvent.attackType=d->sourceDamageClass==1?LocalCombatAttackType::Magic:
            d->sourceDamageClass==2?LocalCombatAttackType::Melee:d->sourceDamageClass==3?LocalCombatAttackType::Ranged:LocalCombatAttackType::None;
        g.emitCombatEvent(hitEvent,players);
    }
    if(applyLocalArcaneBlast(p,c,*d))LOG_INFO("[LOCAL_ARCANE_BLAST] owner=",p.guid," action=apply stacks=",unsigned(localArcaneBlastStacks(p,c)));
    emitFinish();
    result="Cast "+d->name;return true;
}

double LocalGameplay::transportTime() const {return impl_->transportClock;}
void LocalGameplay::setTransportTime(double seconds) {
    if (!std::isfinite(seconds) || seconds<0 || seconds>31557600000.0) return;
    impl_->transportClock=seconds;
    impl_->travel.sampleTransports(seconds, impl_->transports);
}
void LocalGameplay::advanceTransportTime(double seconds) {
    if (std::isfinite(seconds) && seconds>=0) setTransportTime(impl_->transportClock+seconds);
}

bool LocalGameplay::tick(float seconds,const std::vector<LocalRealmPlayer*>& players) {
    if(!std::isfinite(seconds)||seconds<0)return false;
    // An offline looter cannot hold the group's corpse forever. Fail over only
    // to the original death-time cohort, never to a late joiner or a bystander.
    bool reassignedLoot=false;
    for(auto& n:impl_->npcs)if(n.dead && n.lootable && !impl_->player(n.lootOwner,players)) {
        for(auto guid:n.lootCandidates)if(guid) {
            auto* p=impl_->player(guid,players);
            if(p && !p->dead && p->health && distance2(*p,n)<=60*60) {
                n.lootOwner=guid;reassignedLoot=true;
                LOG_INFO("[LOCAL_GROUP_LOOT] reassigned npc=",n.guid," owner=",guid," reason=disconnected");break;
            }
        }
    }

    auto& g=*impl_;
    const float dt=std::min(seconds,0.25f);g.now+=dt;g.regionTimer+=dt;
    for (auto& npc : g.npcs) if (npc.transportEntry) {
        const auto hullIt=std::find_if(g.transports.begin(),g.transports.end(),
            [&](const auto& hull){return hull.entry==npc.transportEntry;});
        const auto* hull=hullIt!=g.transports.end()?&*hullIt:nullptr;
        if (!hull) continue;
        const auto pose=localTransportPassengerPose(*hull,npc.transportX,npc.transportY,
                                                   npc.transportZ,npc.transportOrientation);
        npc.mapId=hull->mapId;npc.x=npc.homeX=pose.x;npc.y=npc.homeY=pose.y;
        npc.z=npc.homeZ=pose.z;npc.orientation=pose.orientation;
    }
    if(g.regionTimer>=0.5f){g.regionTimer=0;g.regions(players);}
    g.areaAuraTimer+=dt;
    if(g.areaAuraTimer>=float(kLocalAreaAuraReconcileMs)/1000.f){g.areaAuraTimer=0;g.reconcileAreaAuras(players);}
    bool changed=reassignedLoot;
    // Anyone who has fallen out of the world is put back before anything else
    // this tick looks at them.
    //
    // Falling through the floor is not survivable on its own: nothing below the
    // terrain ever stops the fall, no tile streams in down there, and the
    // character ends up somewhere the client will not move them out of - which
    // reads to a player as the game freezing rather than as a fall. The
    // authority owns positions, so this is the only place that can undo it.
    //
    // The floor is well below any real terrain in 3.3.5a - Deepholm and the
    // deepest instance floors are far above it - so a character at this depth
    // has left the world rather than gone somewhere low.
    for (auto* p : players) {
        if (!p || p->dead || !std::isfinite(p->z) || p->z > kLocalWorldFloorZ) continue;
        const bool hadReturn = p->hasInstanceReturn;
        // Where to. An instance return point is the door they came in by; a
        // bound inn is the home they chose; the world start is where every
        // character can stand. First one that exists wins - none of them is
        // invented, which is why there is no arbitrary "safe spot" here.
        if (hadReturn) {
            p->mapId = p->returnMapId; p->instanceId = p->returnInstanceId;
            p->x = p->returnX; p->y = p->returnY; p->z = p->returnZ;
            p->orientation = p->returnOrientation;
            p->hasInstanceReturn = false;
        } else if (p->hasHome) {
            p->mapId = p->homeMapId; p->instanceId = 0;
            p->x = p->homeX; p->y = p->homeY; p->z = p->homeZ;
            p->orientation = p->homeOrientation;
        } else {
            const auto& start = g.content->start;
            p->mapId = start.mapId; p->instanceId = 0;
            p->x = start.x; p->y = start.y; p->z = start.z;
            p->orientation = start.orientation;
        }
        // Whatever they were doing is over. A cast or a swing left running
        // against a target on the other side of the world is the second half
        // of the freeze: the client waits for something that can never land.
        ++p->positionRevision;
        finishLocalTeleport(*p);
        p->portalCooldown=2;g.regionTimer=1;
        changed = true;
        LOG_WARNING("[LOCAL_RESCUE] recovered a character from below the world: guid=",
                    p->guid, " to map=", p->mapId, " xyz=", p->x, ",", p->y, ",", p->z,
                    " via=", hadReturn ? "instance return" : (p->hasHome ? "inn" : "world start"));
    }
    // Preserve sub-millisecond time across frames. Per-frame rounding drifts
    // at ordinary frame rates and stops timers at sufficiently high rates.
    const double milliseconds=double(dt)*1000.0+g.combatMillisecondRemainder;
    const uint32_t elapsedMs=uint32_t(milliseconds);
    g.combatMillisecondRemainder=milliseconds-elapsedMs;
    g.authorityClockMs+=elapsedMs;
    for(auto* p:players)if(p&&!p->statAuras.empty()){
        const auto periods=meleePeriods(*p,content());
        p->statAuras.reserve(kLocalMaxStatAuras);
        for(auto& a:p->statAuras){
            const auto* d=content().spell(a.spellId);
            if(p->dead||!p->health||!d||!d->unsupportedReason.empty()||d->passive||!localHasTimedAura(*d)||!validLocalProc(*d)||
               a.mapId!=p->mapId||a.instanceId!=p->instanceId||!localProcChildTalentReady(*p,content(),*d)||!localTimedDamageTalentReady(*p,content(),*d)||
               (d->procParentTalentId&&a.casterGuid!=p->guid)||
               (d->mageArmorGroup&&(p->classId!=8||a.casterGuid!=p->guid||a.remainingMs>d->durationMs))||
               ((d->physicalDamageDonePct||d->damageTakenPct)&&(a.casterGuid!=p->guid||a.remainingMs>d->durationMs))||
               (d->arcaneBlastProfile==2&&(p->classId!=8||a.casterGuid!=p->guid||a.remainingMs>d->durationMs)))a.remainingMs=0;
            else {
                const auto activeMs=std::min(elapsedMs,a.remainingMs);
                a.stacks=std::min(a.stacks,d->maxAuraStacks);a.remainingMs-=activeMs;
                a.absorbRemaining=std::min(a.absorbRemaining,d->wardProfile?1000000u:localStackedAuraAmount(d->buffAbsorb,a.stacks));
                if(d->buffAbsorb&&!a.absorbRemaining){a.remainingMs=0;continue;}
                a.procCharges=std::min(a.procCharges,d->proc.charges);
                a.procCooldownMs-=std::min(elapsedMs,a.procCooldownMs);
                if(d->proc.charges&&!a.procCharges){a.remainingMs=0;continue;}
                const bool hiddenMana=p->classId==11&&p->formSpellId&&p->resourceType!=LocalResourceType::Mana;
                if(d->periodicHealMaxHealthPct) {
                    // This non-mana aura uses the persisted sub-5000 field as
                    // elapsed milliseconds. Reapplication resets to zero.
                    const uint32_t credit=a.manaRegenRemainder%d->periodicIntervalMs+activeMs;
                    a.manaRegenRemainder=credit%d->periodicIntervalMs;
                    for(uint32_t tick=0;tick<credit/d->periodicIntervalMs;++tick) {
                        const uint32_t amount=uint32_t(uint64_t(p->maxHealth)*d->periodicHealMaxHealthPct/100);
                        const uint32_t effective=std::min(amount,p->maxHealth-p->health);
                        p->health+=effective;
                        LocalCombatEvent event{0,p->guid,p->guid,d->id,p->mapId,p->instanceId,amount,effective,0,LocalCombatEventKind::PeriodicHeal};
                        event.schoolMask=d->schoolMask;event.attackType=LocalCombatAttackType::None;
                        g.emitCombatEvent(event,players);
                        LOG_INFO("[LOCAL_BLOOD_CRAZE] owner=",p->guid," spell=",d->id," attempted=",amount," effective=",effective," remaining=",a.remainingMs);
                    }
                } else if(d->manaPer5&&(p->resourceType==LocalResourceType::Mana||hiddenMana)){
                    auto& mana=hiddenMana?p->druidMana:p->mana;
                    const auto capacity=localManaCapacity(*p);
                    const uint64_t credit=uint64_t(localStackedAuraAmount(d->manaPer5,a.stacks))*activeMs+a.manaRegenRemainder;
                    mana=uint32_t(std::min(uint64_t(capacity),uint64_t(mana)+credit/5000));
                    a.manaRegenRemainder=mana==capacity?0:uint32_t(credit%5000);
                } else a.manaRegenRemainder=0;
            }
        }
        std::erase_if(p->statAuras,[](const auto& a){return !a.remainingMs;});
        normalizeLocalMageArmors(*p,content());
        stats(*p,content(),false);localRescaleMeleeTimers(*p,periods[0],periods[1],content());changed=true;
    }
    uint32_t periodicOffset=0;
    const auto advanceDamageAuras=[&](uint32_t periodicStep){
    for(auto& aura:g.periodicDamage) {
        auto* owner=g.player(aura.owner,players);auto* target=g.npc(aura.target);
        if(!target||target->dead||target->combatEpoch!=aura.targetEpoch||target->mapId!=aura.mapId||target->instanceId!=aura.instanceId||
           (aura.spell!=12654&&(!owner||owner->dead||owner->mapId!=aura.mapId||owner->instanceId!=aura.instanceId))) {changed=changed||aura.remaining!=0;aura.remaining=0;continue;}
        auto elapsed=std::min(periodicStep,aura.remaining);const auto activeElapsed=elapsed;
        aura.remaining-=elapsed;changed=changed||elapsed!=0;
        while(aura.next<=elapsed&&aura.interval&&!target->dead) {
            elapsed-=aura.next;aura.next=aura.interval;
            const auto* spell=content().spell(aura.spell);
            g.stormstrikeEventOffsetMs=periodicOffset+activeElapsed-elapsed;
            // AuraEffect::HandlePeriodicDamageAurasTick (SpellAuraEffects.cpp:6287-6291):
            // a target immune to the aura's school takes no tick and keeps the
            // aura - SendTickImmune, then return. The observation is an Immune
            // periodic event with nothing attempted; an orphaned Ignite with no
            // owner to show it to is simply skipped.
            const auto* targetDefinition=content().npc(target->entry);
            const bool tickImmune=spell&&targetDefinition&&localNpcImmuneToDamage(*targetDefinition,*spell,false);
            if(tickImmune) {
                if(owner)g.damageNpc(*target,*owner,0,players,false,aura.spell,true,0,nullptr,LocalMeleeOutcome::Immune);
                LOG_INFO("[LOCAL_IMMUNE] tick npc=",target->guid," spell=",aura.spell," school=",spell->schoolMask);
            }
            else if(aura.spell==12654&&(!owner||owner->mapId!=aura.mapId||owner->instanceId!=aura.instanceId))g.orphanIgniteHit(*target,aura.damage,aura.owner,players);
            else g.damageNpc(*target,*owner,localStackedAuraAmount(aura.damage,aura.stacks),players,spell&&(spell->schoolMask&1)&&!spell->periodicIgnoresArmor,aura.spell,true,
                             0,nullptr,LocalMeleeOutcome::Hit,false,0,0,false,false,aura.critChanceBasisPoints);
            g.stormstrikeEventOffsetMs=0;changed=true;
        }
        aura.next=elapsed<aura.next?aura.next-elapsed:0;
    }
    g.periodicDamage.erase(std::remove_if(g.periodicDamage.begin(),g.periodicDamage.end(),[](const auto& aura){return !aura.remaining;}),g.periodicDamage.end());
        periodicOffset+=periodicStep;
    };
    uint32_t igniteElapsed=elapsedMs;
    while(!g.pendingIgnites.empty()){
        const auto soon=std::min_element(g.pendingIgnites.begin(),g.pendingIgnites.end(),[](const auto& a,const auto& b){return a.delay<b.delay;});
        if(soon->delay>igniteElapsed)break;
        const auto step=soon->delay;advanceDamageAuras(step);igniteElapsed-=step;
        for(auto& pending:g.pendingIgnites)pending.delay-=std::min(step,pending.delay);
        for(size_t index=0;index<g.pendingIgnites.size();){
            if(g.pendingIgnites[index].delay){++index;continue;}
            const auto pending=g.pendingIgnites[index];g.pendingIgnites.erase(g.pendingIgnites.begin()+index);
            auto* owner=g.player(pending.owner,players);auto* target=g.npc(pending.target);const auto* child=content().spell(12654);
            if(!owner||!target||target->dead||!target->health||target->transportEntry||target->combatEpoch!=pending.epoch||
               owner->mapId!=pending.map||owner->instanceId!=pending.instance||target->mapId!=pending.map||target->instanceId!=pending.instance||
               !child||!child->triggeredOnly||!child->unsupportedReason.empty())continue;
            auto slot=std::find_if(g.periodicDamage.begin(),g.periodicDamage.end(),[&](const auto& a){return a.owner==pending.owner&&a.target==pending.target&&a.spell==12654;});
            const auto onTarget=std::count_if(g.periodicDamage.begin(),g.periodicDamage.end(),[&](const auto& a){return a.target==pending.target&&a.remaining;});
            if(slot==g.periodicDamage.end()&&(g.periodicDamage.size()>=MaxNpcs*8||onTarget>=8))continue;
            Impl::PeriodicDamage fresh{pending.owner,pending.target,12654,4000,2000,2000,pending.damage,pending.map,pending.instance};fresh.targetEpoch=pending.epoch;
            if(slot==g.periodicDamage.end())g.periodicDamage.push_back(fresh);else *slot=fresh;
            LocalCombatEvent applied{0,pending.owner,pending.target,12654,pending.map,pending.instance,0,0,0,LocalCombatEventKind::ProcAura,false,pending.parent};
            applied.parentSequence=pending.sourceSequence;applied.rootSequence=pending.rootSequence;applied.procDepth=pending.depth;
            applied.auraOwnerGuid=applied.auraCasterGuid=pending.owner;applied.auraApplied=true;applied.auraDurationMs=4000;g.emitCombatEvent(applied,players);changed=true;
        }
    }
    advanceDamageAuras(igniteElapsed);
    for(auto& pending:g.pendingIgnites)pending.delay-=std::min(igniteElapsed,pending.delay);
    for(auto& aura:g.periodicHeals) {
        auto* owner=g.player(aura.owner,players);auto* target=g.player(aura.target,players);
        if(!owner||owner->dead||!target||target->dead||!target->health||
           owner->mapId!=aura.mapId||target->mapId!=aura.mapId||
           owner->instanceId!=aura.instanceId||target->instanceId!=aura.instanceId) {
            changed=changed||aura.remaining!=0;aura.remaining=0;continue;
        }
        auto elapsed=std::min(elapsedMs,aura.remaining);
        aura.remaining-=elapsed;changed=changed||elapsed!=0;
        while(aura.next<=elapsed&&aura.interval) {
            elapsed-=aura.next;aura.next=aura.interval;
            // The same application-time snapshot decides a periodic heal's
            // critical. A zero snapshot can never crit.
            const bool critical=aura.critChanceBasisPoints&&g.meleeRoll()<aura.critChanceBasisPoints;
            const auto attempted=critical?localMagicCriticalAmount(localStackedAuraAmount(aura.amount,aura.stacks))
                                        :localStackedAuraAmount(aura.amount,aura.stacks);
            const auto health=uint32_t(std::min(uint64_t(target->maxHealth),uint64_t(target->health)+attempted));
            const auto effective=health-target->health;
            changed=changed||target->health!=health;target->health=health;
            g.emitCombatEvent({0,owner->guid,target->guid,aura.spell,aura.mapId,aura.instanceId,
                attempted,effective,0,LocalCombatEventKind::PeriodicHeal,false,0,
                critical?LocalMeleeOutcome::Critical:LocalMeleeOutcome::Hit},players);
        }
        aura.next=elapsed<aura.next?aura.next-elapsed:0;
    }
    g.periodicHeals.erase(std::remove_if(g.periodicHeals.begin(),g.periodicHeals.end(),
        [](const auto& aura){return !aura.remaining;}),g.periodicHeals.end());
    for(auto& n:g.npcs) {
        const bool hadStormstrike=!n.stormstrikeAuras.empty();
        std::erase_if(n.stormstrikeAuras,[&](auto& a){
            const auto* owner=g.player(a.casterGuid,players);
            if(n.dead||n.transportEntry||!owner||owner->dead||owner->flight.active||owner->transportEntry||
               owner->mapId!=n.mapId||owner->instanceId!=n.instanceId||owner->positionRevision!=a.casterRevision)return true;
            a.remainingMs-=std::min(a.remainingMs,elapsedMs);return !a.remainingMs||!a.charges;
        });
        changed=changed||hadStormstrike;
    }
    // Transports run on world time, not on accumulated steps: the schedule is
    // a pure function of the clock, so a guest whose frames stuttered still
    // agrees with the host about where a ship is.
    if (g.travel.loaded() && !g.travel.transportRoutes().empty()) {
        const size_t before = g.transports.size();
        g.travel.sampleTransports(g.transportClock, g.transports);
        if (g.transports.size() != before) changed = true;
        else if (!g.transports.empty()) changed = true;
    }

    for(auto* p:players) {
        // one predicate for both the cast-time refusal and the eviction,
        // so the two can no longer drift. localFormEnvironmentRetained reads
        // SPELL_ATTR0_ONLY_OUTDOORS from the definition instead of naming form
        // 16 - and that bit is set on Travel Form 783 as well as on Ghost Wolf
        // 2645, which is why walking indoors now ends Travel Form too.
        if(const auto* form=localActiveForm(*p))
            if(const auto* d=content().spell(form->spell);
               d&&d->formId&&d->unsupportedReason.empty()&&!localFormEnvironmentRetained(*p,*d)) {
                leaveLocalForm(*p);stats(*p,content(),false);changed=true;
            }
        // Fall damage, measured and dealt here and nowhere else.
        //
        // A guest never reaches this code: LocalRealm ticks the ruleset only
        // while it is authoritative, so a connected console's health is
        // whatever the host's snapshot says it is. What a client contributes is
        // the two movement bits beside its reported position - it owns the
        // collision and the water, the authority does not - and the authority
        // contributes everything else: where the fall began, how far it ran and
        // what it cost.
        //
        // The measurement deliberately refuses to run for anything that is not
        // a character falling under its own weight. A flight, a transport deck
        // and death are all positions somebody else is writing, and every
        // authority relocation bumps positionRevision - which is the same test
        // that keeps the below-the-world rescue directly above from arriving
        // here as a 2,000-unit landing. Falling out of the world is not a fall
        // onto ground and must never be charged as one.
        {
            const bool descending = (p->movementState & kLocalMovementFalling) != 0;
            const bool inLiquid = (p->movementState & kLocalMovementInLiquid) != 0;
            if (p->dead || p->flight.active || p->transportEntry ||
                p->positionRevision != p->fallRevision || !std::isfinite(p->z)) {
                p->falling = false;
                p->fallRevision = p->positionRevision;
            } else if (descending && !inLiquid) {
                // The client reports falling from the apex down, so the first
                // tick of a descent is the height to measure from.
                if (!p->falling) { p->falling = true; p->fallStartZ = p->z; }
            } else if (p->falling) {
                p->falling = false;
                // Water breaks a fall, and a descent that ended in it is the
                // client saying so - it has the liquid heights, this does not.
                const uint32_t damage = inLiquid ? 0u : localFallDamage(p->fallStartZ - p->z, p->maxHealth);
                if (damage) {
                    if (damage >= p->health) {
                        // Environmental self damage has no KILL/KILLED pair.
                        // DEATH is evaluated while the victim's auras and live
                        // resource state still exist, as in Unit::Kill.
                        g.emitCombatEvent(localDeathProcEvent(p->guid,p->mapId,p->instanceId,true,p->level),players);
                        p->health = 0; localStopRangedAuto(*p);leaveLocalForm(*p); clearLocalCombo(*p); p->dead = true; p->mountSpellId=0; p->deadTimer = 0; p->attackTarget = 0;
                        clearCast(*p, LocalCastStatus::Interrupted);
                    } else p->health -= damage;
                    changed = true;
                    LOG_INFO("[LOCAL_FALL] guid=", p->guid, " dropped=", p->fallStartZ - p->z,
                             " damage=", damage, " of ", p->maxHealth,
                             p->dead ? " (fatal)" : "");
                }
            }
        }
        p->portalCooldown=std::max(0.0f,p->portalCooldown-dt);
        // The way home runs down even in flight or on a boat: it is a rest
        // timer, not something combat or travel suspends.
        if (p->hearthCooldown > 0) { p->hearthCooldown = std::max(0.0f, p->hearthCooldown - dt); changed = true; }
        const uint32_t ms=elapsedMs;
        // Cooldowns advance even when flight or a missing transport skips combat.
        changed=migrateLocalCategoryCooldowns(*p,content())||changed;
        for(auto& cd:p->cooldowns)if(cd.remainingMs){cd.remainingMs=cd.remainingMs>ms?cd.remainingMs-ms:0;changed=true;}
        for(auto& cd:p->categoryCooldowns)if(cd.remainingMs){cd.remainingMs-=std::min(cd.remainingMs,ms);changed=true;}
        if(p->globalCooldownMs){p->globalCooldownMs=p->globalCooldownMs>ms?p->globalCooldownMs-ms:0;changed=true;}
        // Travel can return early below; riding state and rune recovery still
        // advance so landing cannot restore a mount that was cleared in flight.
        if(p->mountSpellId && (p->dead || p->flight.active || p->transportEntry || p->instanceId || (p->movementState&kLocalMovementInLiquid))) {
            p->mountSpellId=0;changed=true;
        }
        changed=advanceLocalRunes(p->runeCooldownMs,ms)||changed;
        changed=advanceLocalRegeneration(*p,elapsedMs,localCombatActive(*p,g.npcs),localRegenerationRates(*p,content()))||changed;
        // A flight owns the character's position for its duration. Combat,
        // casting and NPC aggro are all suppressed by the same rule that
        // suppresses them for a dead player: nothing else runs for them below.
        if(p->dead && p->flight.active){p->flight={};changed=true;}
        if (p->flight.active) {
            if(!g.travel.loaded()){changed=true;continue;}
            uint32_t mapId = p->mapId;
            float x = p->x, y = p->y, z = p->z, orientation = p->orientation;
            g.travel.advanceFlight(p->flight, dt, mapId, x, y, z, orientation);
            const bool moved = mapId != p->mapId || x != p->x || y != p->y || z != p->z;
            p->mapId = mapId; p->x = x; p->y = y; p->z = z; p->orientation = orientation;
            if (moved) ++p->positionRevision;
            if (!p->flight.active) {
                // Arriving is how the destination becomes a known node.
                discoverTaxiNode(*p, p->flight.destinationNode);
                ++p->positionRevision;finishLocalTeleport(*p);
            }
            p->attackTarget = 0;
            changed = true;
            continue;
        }
        // A passenger rides with the hull. Storing the offset rather than
        // re-seating the player each tick is what lets them walk around on
        // deck while the ship moves under them.
        if (p->transportEntry) {
            if(!g.travel.loaded()){changed=true;continue;}
            const LocalTransportState* hull = nullptr;
            for (const auto& t : g.transports) if (t.entry == p->transportEntry) { hull = &t; break; }
            if (!hull) {
                // The route went away (content reload). Leave the player where
                // they are rather than at the origin.
                ++p->positionRevision;finishLocalTeleport(*p);changed=true;
            } else {
                const float c = std::cos(hull->orientation), s = std::sin(hull->orientation);
                const float x = hull->x + c * p->transportOffsetX - s * p->transportOffsetY;
                const float y = hull->y + s * p->transportOffsetX + c * p->transportOffsetY;
                // Smooth travel is handled by the client's live deck transform.
                // Only a seam invalidates the client's current map/camera position.
                const bool seam = p->mapId != hull->mapId || std::hypot(x - p->x, y - p->y) > 200.0f;
                if (seam) ++p->positionRevision;
                p->orientation = std::remainder(p->orientation + hull->orientation - p->transportLastYaw, 6.28318530718f);
                if (p->orientation < 0) p->orientation += 6.28318530718f;
                p->transportLastYaw = hull->orientation;
                p->mapId = hull->mapId;
                p->x = x; p->y = y;
                p->z = hull->z + p->transportOffsetZ;
                if (seam) {
                    // Keep the passenger attached, but never carry fall/cast
                    // origins from another continent into the new coordinates.
                    if(p->castingSpellId)clearCast(*p,LocalCastStatus::Interrupted);
                    p->mountSpellId=0;p->movementState=0;p->falling=false;
                    p->fallStartZ=p->z;p->fallRevision=p->positionRevision;
                    g.regionTimer=1;
                    LOG_INFO("[LOCAL_TRANSPORT] seam player=",p->guid," map=",p->mapId," revision=",p->positionRevision);
                }
                changed = true;
            }
        }
        if(p->castingSpellId) {
            changed=true;
            const auto* casting=content().spell(p->castingSpellId);
            bool invalidTarget=false;
            if(casting) {
                if((casting->damage||casting->periodicDamage||casting->snarePercent)&&!casting->areaRadius) {
                    const auto* target=g.npc(p->castTarget);
                    invalidTarget=!target||target->dead||!target->health||target->mapId!=p->mapId||target->instanceId!=p->instanceId||!canAttack(*p,*target);
                } else if(casting->heal||casting->periodicHeal||localHasTimedAura(*casting)) {
                    const auto* target=p->castTarget?g.player(p->castTarget,players):p;
                    invalidTarget=!target||target->dead||!target->health||target->mapId!=p->mapId||target->instanceId!=p->instanceId;
                }
            }
            if(!casting||p->dead||invalidTarget||
               std::find(p->knownSpells.begin(),p->knownSpells.end(),p->castingSpellId)==p->knownSpells.end()||
               (casting&&(!localSpellEquipmentReady(*p,content(),*casting)||!localFormEnvironmentReady(*p,*casting)))||p->mapId!=p->castOriginMap||p->instanceId!=p->castOriginInstance||
               ((casting->interruptFlags&1u) &&
                distance2(p->x,p->y,p->z,p->castOriginX,p->castOriginY,p->castOriginZ)>.01f))
                clearCast(*p,LocalCastStatus::Interrupted);
            else if(p->castRemainingMs<=ms) {
                const LocalRealmCommand command{LocalAction::CastSpell,p->castTarget,p->castingSpellId};
                std::string outcome;executeCastSpell(*p,command,players,outcome,true);
            } else p->castRemainingMs-=ms;
        }
        if(p->dead){
            localCaptureCorpse(*p);localStopRangedAuto(*p);p->deadTimer+=dt;
            if(!p->ghost && p->deadTimer>=3){std::string released;changed=execute(*p,{LocalAction::Respawn},players,released)||changed;}
            continue;
        }
        p->rangedRemainingMs-=std::min(p->rangedRemainingMs,ms);
        if(p->rangedAutoSpellId) {
            const auto* d=content().spell(p->rangedAutoSpellId);
            auto* n=g.npc(p->rangedTarget);
            const auto weapon=d?localRangedAmounts(*p,content(),*d):LocalRangedAmounts{};
            const bool moved=localRangedPositionChanged(*p);
            const bool wand=d&&d->rangedAutoProfile==2;
            const bool invalid=!d||!weapon.active||!n||n->dead||!n->health||n->mapId!=p->mapId||n->instanceId!=p->instanceId||
                p->rangedOriginRevision!=p->positionRevision||p->rangedWeapon!=p->equipment[17]||
                std::find(p->knownSpells.begin(),p->knownSpells.end(),p->rangedAutoSpellId)==p->knownSpells.end()||
                !canAttack(*p,*n);
            if(invalid||(wand&&(moved||p->castingSpellId))){localStopRangedAuto(*p);changed=true;}
            else if(!p->castingSpellId&&!moved&&!p->rangedRemainingMs) {
                const auto distance=distance2(*p,*n);
                const float autoReach=localCreatureCombatReach(content().npc(n->entry));
                const bool ready=!localSpellTargetTooClose(*d,float(distance),autoReach)&&
                    localWithinCombatRange(float(distance),localSpellMaximumRange(*p,content(),*d),kLocalDefaultCombatReach,autoReach)&&
                    localComboFacingReady(*p,*n,false);
                if(!ready) {if(wand){localStopRangedAuto(*p);changed=true;}}
                else {
                    // Commit ammunition before CAST callbacks and protect against
                    // a repeated start command bypassing weapon recovery.
                    if(weapon.ammoId)removeItem(*p,weapon.ammoId,1);
                    p->rangedRemainingMs=weapon.periodMs;
                    LocalCombatEvent cast{0,p->guid,n->guid,d->id,p->mapId,p->instanceId,0,0,0,LocalCombatEventKind::SpellCast};
                    cast.attackType=LocalCombatAttackType::Ranged;cast.schoolMask=weapon.schoolMask;cast.spellTypeMask=7;
                    g.emitCombatEvent(cast,players);
                    auto schoolDefinition=*d;schoolDefinition.schoolMask=weapon.schoolMask;
                    // Auto Shot 75 and Shoot 5019 are spells and take the same
                    // template immunity read at the hit roll (Object.cpp:3746),
                    // with the weapon's school: a frost wand against a
                    // frost-immune creature is IMMUNE before any ranged roll.
                    const auto* rangedTarget=content().npc(n->entry);
                    const bool rangedImmune=rangedTarget&&localNpcImmuneToSpell(*rangedTarget,schoolDefinition,false);
                    const auto outcome=rangedImmune?LocalMeleeOutcome::Immune:localRollRanged(*p,*n,weapon,g.meleeRoll(),g.meleeRoll(),wand);
                    const float multiplier=outcome==LocalMeleeOutcome::Critical?(wand?1.5f:2.f):1.f;
                    const auto damage=uint32_t(std::clamp((weapon.low+(weapon.high-weapon.low)*g.meleeRoll()/9999.f)*multiplier,0.f,1000000.f));
                    const bool blocked=!wand&&outcome!=LocalMeleeOutcome::Miss&&localRollMeleeSpecialBlock(*p,*n,g.meleeRoll());
                    // Creature::GetShieldBlockValue (Creature.h:158-161) is
                    // level / 2 + strength / 20, and a plain creature's
                    // strength is never set at the pin (Creature::UpdateStats
                    // is a no-op, StatSystem.cpp:1034-1037; only Player.cpp and
                    // Pet.cpp call SetCreateStat), so the value is level / 2
                    // exactly; the implementation unified the three level / 2 + 1 sites.
                    g.damageNpc(*n,*p,damage,players,(weapon.schoolMask&1)!=0,d->id,false,0,&schoolDefinition,outcome,false,
                        blocked?localCreatureBlockValue(*n):0,0,true,true);
                    auto finish=cast;finish.target=0;finish.kind=LocalCombatEventKind::SpellFinish;finish.outcome=outcome;
                    g.emitCombatEvent(finish,players);
                    p->attackTimer=localMeleeSpeed(*p,content());p->offHandTimer=localMeleeSpeed(*p,content(),true);
                    if(!++p->castRevision)++p->castRevision;p->lastCastSpellId=d->id;p->lastCastTarget=n->guid;
                    LOG_INFO("[LOCAL_RANGED] source=",p->guid," target=",n->guid," spell=",d->id," ammo=",weapon.ammoId,
                        " period=",weapon.periodMs," outcome=",unsigned(outcome)," school=",weapon.schoolMask);
                    changed=true;
                }
            }
            localRangedRememberPosition(*p);
        }
        const auto weaponChanged=p->meleeWeaponMain!=p->equipment[15]||p->meleeWeaponOff!=p->equipment[16]||p->meleeForm!=p->formSpellId;
        if(weaponChanged){p->meleeWeaponMain=p->equipment[15];p->meleeWeaponOff=p->equipment[16];p->meleeForm=p->formSpellId;
            p->attackTimer=p->meleePeriodMain=localMeleeSpeed(*p,content());p->offHandTimer=p->meleePeriodOff=localMeleeSpeed(*p,content(),true);}
        const auto periods=meleePeriods(*p,content());
        localRescaleMeleeTimers(*p,periods[0],periods[1],content());
        p->attackTimer=std::max(0.f,p->attackTimer-dt);p->offHandTimer=std::max(0.f,p->offHandTimer-dt);
        if(p->attackTarget){
            auto* n=g.npc(p->attackTarget);
            if(!n||n->dead||n->mapId!=p->mapId||n->instanceId!=p->instanceId||distance2(*p,*n)>60*60){p->attackTarget=0;changed=true;}
            // Player::Update swings when IsWithinMeleeRange(victim)
            // (PlayerUpdates.cpp:173, :213), which is
            // max(reachA + reachB + 4/3, NOMINAL_MELEE_RANGE) and was a fixed
            // 4.5 yd here (audit D21 / D25).
            else if(!p->castingSpellId&&
                    localWithinMeleeRange(distance2(*p,*n),kLocalDefaultCombatReach,localCreatureCombatReach(content().npc(n->entry)))&&
                    localComboFacingReady(*p,*n,false)){
                const auto ms=localMeleeStats(*p,content());
                for(unsigned hand=0;hand<2&&!n->dead;++hand){
                    auto& timer=hand?p->offHandTimer:p->attackTimer;if(timer>0||(hand&&!ms.offHand))continue;
                    // Source dual-wield scheduling staggers the other hand
                    // before attack callbacks. A new haste aura may then
                    // rescale this remaining fraction along with that hand.
                    if(ms.offHand) {
                        auto& otherTimer=hand?p->attackTimer:p->offHandTimer;
                        otherTimer=std::max(.2f,otherTimer);
                    }
                    const auto w=localWeaponAmounts(*p,content(),hand!=0);
                    auto outcome=localRollPlayerMelee(*p,*n,ms,false,g.meleeRoll(),g.meleeRoll(),hand!=0);
                    float multiplier=outcome==LocalMeleeOutcome::Critical?2.f:outcome==LocalMeleeOutcome::Glancing?1.f-std::min(3,int(n->level)-int(p->level))*.1f:1.f;
                    const auto damage=uint32_t((w.low+(w.high-w.low)*g.meleeRoll()/9999.f)*multiplier);
                    const auto magic=uint32_t((w.magicLow+(w.magicHigh-w.magicLow)*g.meleeRoll()/9999.f)*multiplier);
                    g.damageNpc(*n,*p,damage,players,true,0,false,0,nullptr,outcome,hand!=0,outcome==LocalMeleeOutcome::Block?localCreatureBlockValue(*n):0,magic);
                    // resetAttackTimer follows the source hit/proc handling:
                    // consuming the last charge starts the next normal-speed
                    // interval unless a critical hit refreshed Flurry.
                    timer=localMeleeSpeed(*p,content(),hand!=0);
                    if(outcome==LocalMeleeOutcome::Parry&&n->attackTimer>.4f&&!(localNpcMeleeFlags(n->entry)&8))n->attackTimer=std::max(.4f,n->attackTimer-.8f);
                    changed=true;
                }
            }
        }

    }
    // --- Owned creatures ----------------------------------------------------
    // Retire first, so nothing below simulates a summon whose owner has gone,
    // then regenerate, follow and swing. Source order: a dead or expired summon
    // never acts, and focus regeneration is not suspended by combat.
    {
        std::vector<uint64_t> retiring;
        for(auto& summon:g.pets) {
            auto* owner=localPetOwner(summon,players);
            if(!localPetOwnerEligible(summon,owner)){retiring.push_back(summon.guid);continue;}
            if(summon.dead){retiring.push_back(summon.guid);continue;}
            if(summon.kind==LocalPetKind::Guardian) {
                summon.remainingMs-=std::min(summon.remainingMs,elapsedMs);
                if(!summon.remainingMs){retiring.push_back(summon.guid);continue;}
            }
            if(localPetRegenerate(summon,elapsedMs))changed=true;
            const auto previousMana=summon.power;g.regeneratePetMana(summon,elapsedMs);
            changed=changed||summon.power!=previousMana;
            // Pet::SynchronizeLevelWithOwner: a SUMMON_PET is always the
            // owner's level, so an owner who levelled while the pet was out
            // takes it with them and its stats are recomputed from the row.
            if(auto* live=owner)if(g.synchronizePetLevel(summon,*live))changed=true;
        }
        for(auto guid:retiring){g.retirePet(guid,"owner or lifetime ended",players);changed=true;}
        g.tickPetSpells(elapsedMs,players,*this);
        for(auto& summon:g.pets) {
            auto* owner=localPetOwner(summon,players);
            if(!owner)continue;
            if(g.petCasts.contains(summon.guid)){changed=true;continue;}
            // PetAI::UpdateAI (PetAI.cpp:148-213), reduced to what this realm
            // carries. A guardian is not commanded - HandlePetAction needs a
            // CharmInfo, which only a controlled minion has - so it keeps the
            // stance and command defaults and behaves exactly as before.
            const bool commanded=summon.kind==LocalPetKind::Controlled;
            const bool holding=commanded&&localPetHoldsPosition(summon);
            auto* target=g.npc(summon.targetGuid);
            // A victim that died, left, boarded a hull, changed map or instance,
            // walked outside the leash - or simply stopped existing, which is
            // what a despawn is here - ends the engagement. _stopAttack ->
            // ClearCharmInfoFlags (PetAI.cpp:72-90, 816-831) drops the attack
            // order with it and leaves the command state alone, so the pet
            // returns to whichever of stay or follow it was holding.
            if(summon.targetGuid&&(!target||target->dead||!target->health||target->transportEntry||
               target->mapId!=summon.mapId||target->instanceId!=summon.instanceId||
               distance2(summon.x,summon.y,summon.z,target->x,target->y,target->z)>kLocalPetLeashDistance*kLocalPetLeashDistance)) {
                localPetStopAttack(summon);target=nullptr;
            }
            const bool commandedAttack=commanded&&summon.commandAttack;
            // Target acquisition is the stance's. Four routes, in the
            // reference's own order and with its own gates:
            //
            //   PetAI::AttackedBy        (PetAI.cpp:832-845) - something is
            //       hitting the pet. Not passive.
            //   PetAI::OwnerAttackedBy   (:452-470) - something is hitting the
            //       owner. Not passive.
            //   PetAI::OwnerAttacked     (:472-491) - the owner picked a
            //       target. Not passive: "3.0.2 - Pets now start attacking
            //       their owners victim in defensive mode as soon as the hunter
            //       does" (SelectNextTarget, :531-533).
            //   SelectNextTarget's allowAutoSelect arm (:536-554) - the nearest
            //       hostile within MAX_AGGRO_RADIUS. AGGRESSIVE ONLY. This is
            //       the one route that separates aggressive from defensive.
            //
            // All four are skipped while the pet has a living victim
            // ("Prevent pet from disengaging from current target"), which is
            // what `!target` already means here. An explicit COMMAND_ATTACK
            // stays on its victim regardless of the stance, which is what
            // SetIsCommandAttack(true) means.
            const auto acquirable=[&](LocalRealmNpc* candidate)->LocalRealmNpc* {
                if(!candidate||candidate->dead||!candidate->health||candidate->transportEntry||
                   candidate->mapId!=summon.mapId||candidate->instanceId!=summon.instanceId||
                   !canAttack(*owner,*candidate))return nullptr;
                return candidate;
            };
            const bool defends=!commanded||localPetDefendsOwner(summon);
            if(!target&&defends) {
                // AttackedBy, then OwnerAttackedBy: whatever already holds this
                // summon or its owner as its victim.
                for(auto& hostile:g.npcs) {
                    if(hostile.targetGuid!=summon.guid&&hostile.targetGuid!=owner->guid)continue;
                    if(auto* found=acquirable(&hostile)){summon.targetGuid=found->guid;target=found;break;}
                }
            }
            if(!target&&defends&&owner->attackTarget)
                if(auto* found=acquirable(g.npc(owner->attackTarget)))
                    {summon.targetGuid=found->guid;target=found;}
            if(!target&&commanded&&localPetAutoAcquires(summon)) {
                // SelectNearestTargetInAttackDistance(MAX_AGGRO_RADIUS): the
                // NEAREST hostile the pet itself could start on, 45 yards
                // floored at ATTACK_DISTANCE 5. The reference additionally
                // requires `!IsReturning() || IsFollowing() || IsAtStay()`; a
                // pet here with no victim is always following or at stay, so
                // that condition is unconditionally true and is not restated.
                LocalRealmNpc* nearest=nullptr;float best=0;
                for(auto& hostile:g.npcs) {
                    auto* found=acquirable(&hostile);if(!found)continue;
                    const float d=distance2(summon.x,summon.y,summon.z,found->x,found->y,found->z);
                    if(d>kLocalPetAutoAcquireRange*kLocalPetAutoAcquireRange)continue;
                    if(!nearest||d<best){nearest=found;best=d;}
                }
                if(nearest){summon.targetGuid=nearest->guid;target=nearest;}
            }
            // The anchor a pet returns to: its stay point while holding, its
            // owner otherwise. HandleReturnMovement (PetAI.cpp:561-610) picks
            // between GetStayPosition() and MoveFollow(owner) on exactly this.
            const float anchorX=holding?summon.stayX:owner->x;
            const float anchorY=holding?summon.stayY:owner->y;
            const float anchorZ=holding?summon.stayZ:owner->z;
            const float anchorDistance=distance2(summon.x,summon.y,summon.z,anchorX,anchorY,anchorZ);
            const float ownerDistance=distance2(summon.x,summon.y,summon.z,owner->x,owner->y,owner->z);
            const float step=std::min(dt*7.0f,std::sqrt(std::max(0.0f,anchorDistance)));
            if(!target) {
                // A staying pet returns to its stay point exactly, not to
                // within the follow distance of it.
                const float slack=holding?0.0f:kLocalPetFollowDistance*kLocalPetFollowDistance;
                if(anchorDistance>slack&&step>0) {
                    const float dx=anchorX-summon.x,dy=anchorY-summon.y,dz=anchorZ-summon.z;
                    const float length=std::sqrt(anchorDistance);
                    summon.x+=dx/length*step;summon.y+=dy/length*step;summon.z+=dz/length*step;
                    if(dx||dy)summon.orientation=std::atan2(dy,dx);
                    changed=true;
                }
                summon.attackTimer=std::max(0.0f,summon.attackTimer-dt);
                continue;
            }
            if(ownerDistance>kLocalPetLeashDistance*kLocalPetLeashDistance){localPetStopAttack(summon);continue;}
            const float d2=distance2(summon.x,summon.y,summon.z,target->x,target->y,target->z);
            if(commanded&&summon.fireboltAutocast)if(const auto spell=localPetFireboltSpell(summon.entry,summon.level)) {
                std::string why;
                if(g.startPetSpell(summon,*owner,*target,spell,why)){changed=true;continue;}
            }
            summon.orientation=std::atan2(target->y-summon.y,target->x-summon.x);
            // UnitAI::DoMeleeAttackIfReady (UnitAI.cpp:50) for an owned creature
            // too: the swing gate is IsWithinMeleeRange, reach-aware since the reference.
            // The chase threshold keeps its own constant; movement is not P05's.
            // The pet's own reach is the creature_model_info value the compiled
            // pet catalog carries - the world catalog has no record for it.
            const auto* petTemplate=localPetTemplate(summon.entry);
            const bool petInMeleeRange=localWithinMeleeRange(d2,
                petTemplate?petTemplate->combatReach:localCreatureCombatReach(content().npc(summon.entry)),
                localCreatureCombatReach(content().npc(target->entry)));
            // COMMAND_STAY does not chase: "Check before attacking to prevent
            // pets from leaving stay position" (PetAI.cpp:186-192). It swings
            // only at a victim already inside its reach, unless it was
            // explicitly commanded onto that victim.
            if(d2>kLocalPetMeleeRange*kLocalPetMeleeRange&&(!holding||commandedAttack)) {
                const float dx=target->x-summon.x,dy=target->y-summon.y,dz=target->z-summon.z;
                const float length=std::sqrt(d2),chase=std::min(dt*7.0f,std::max(0.0f,length-2.5f));
                if(length>0&&chase>0){summon.x+=dx/length*chase;summon.y+=dy/length*chase;summon.z+=dz/length*chase;changed=true;}
            }
            summon.attackTimer=std::max(0.0f,summon.attackTimer-dt);
            if(petInMeleeRange&&summon.attackTimer<=0) {
                summon.attackTimer=float(summon.attackPeriodMs)/1000.0f;
                // Guardian::InitStatsForLevel's SUMMON_PET weapon damage is
                // pet_levelstats min_dmg..max_dmg (Pet.cpp:1184-1188). Before
                // the implementation this was the spawnable catalog's single `damage` value,
                // which for a pet creature did not exist at all.
                const auto weapon=g.petWeaponDamage(summon);
                const auto outcome=localRollPetMelee(summon,*target,g.meleeRoll());
                const float scale=outcome==LocalMeleeOutcome::Critical?2.0f:
                                  outcome==LocalMeleeOutcome::Crushing?1.5f:1.0f;
                const float roll=float(g.meleeRoll())/9999.0f;
                const uint32_t raw=uint32_t(std::clamp(
                    (float(weapon.first)+(float(weapon.second)-float(weapon.first))*roll)*scale,0.0f,1000000.0f));
                g.damageNpcByPet(*target,summon,*owner,raw,players,outcome,
                    outcome==LocalMeleeOutcome::Block?localCreatureBlockValue(*target):0);
                changed=true;
            }
        }
    }
    for(auto& n:g.npcs) {
        const auto* def=content().npc(n.entry);if(!def)continue;
        const bool hadSnares=!n.snares.empty();
        std::erase_if(n.snares,[&](const auto& a){
            const auto* owner=g.player(a.casterGuid,players);
            return n.dead||n.transportEntry||!owner||owner->dead||owner->flight.active||
                owner->mapId!=n.mapId||owner->instanceId!=n.instanceId||owner->positionRevision!=a.casterRevision;
        });
        const auto pursuitStep=localNpcPursuitDistance(n,elapsedMs);
        for(auto& a:n.snares)a.remainingMs-=std::min(a.remainingMs,elapsedMs);
        std::erase_if(n.snares,[](const auto& a){return !a.remainingMs;});
        changed=changed||hadSnares;
        // P04 controls follow the snare's lifecycle exactly: the same caster
        // eligibility, the same revision invalidation, the same expiry.
        const bool hadControls=!n.controls.empty();
        // Both removal paths - caster invalidation and expiry - lower the
        // diminishing stack, exactly as Unit::ApplyDiminishingAura(false) does
        // wherever an aura leaves a unit.
        const auto dropControl=[&](const LocalNpcControl& a){
            if(const auto* d=content().spell(a.spellId))
                localDiminishingApply(n,localDiminishingGroupForSpell(*d,false),false,g.authorityClockMs);
        };
        std::erase_if(n.controls,[&](const auto& a){
            const auto* owner=g.player(a.casterGuid,players);
            const bool drop=n.dead||n.transportEntry||!owner||owner->dead||owner->flight.active||
                owner->mapId!=n.mapId||owner->instanceId!=n.instanceId||owner->positionRevision!=a.casterRevision;
            if(drop)dropControl(a);
            return drop;
        });
        const bool stunned=localNpcStunned(n);
        const bool silenced=localNpcSilenced(n);
        for(auto& a:n.controls)a.remainingMs-=std::min(a.remainingMs,elapsedMs);
        std::erase_if(n.controls,[&](const auto& a){
            if(a.remainingMs)return false;
            dropControl(a);return true;
        });
        changed=changed||hadControls;
        if(n.dead) {
            localResetNpcSpellState(n);
            n.respawnTimer-=dt;
            if(n.respawnTimer<=0){n.combatEpoch=g.allocateNpcEpoch();n.dead=false;n.health=n.maxHealth;n.x=n.homeX;n.y=n.homeY;n.z=n.homeZ;n.lootable=false;n.lootOwner=0;n.targetGuid=0;changed=true;}
            continue;
        }
        // Crew stays in authored deck-local positions. Ordinary ground pursuit
        // would send it through the hull or back to the dock as the ship moves.
        if (n.transportEntry) { localResetNpcSpellState(n);n.targetGuid=0;n.threat={};continue; }
        const bool wasEngaged=n.targetGuid||std::any_of(n.threat.begin(),n.threat.end(),[](const auto& e){return e.guid!=0;});
        for(auto& entry:n.threat)if(entry.guid)if(auto* member=g.player(entry.guid,players))if(!canAttack(*member,n))entry={};
        g.selectThreatTarget(n,players);
        if(wasEngaged&&(!n.targetGuid||distance2(n.x,n.y,n.z,n.homeX,n.homeY,n.homeZ)>60*60)) {
            localResetNpcSpellState(n);n.targetGuid=0;n.threat={};n.combatEpoch=g.allocateNpcEpoch();n.lootOwner=0;n.health=n.maxHealth;n.x=n.homeX;n.y=n.homeY;n.z=n.homeZ;n.snares.clear();g.releaseNpcControls(n);n.damageAuras.clear();n.stormstrikeAuras.clear();changed=true;continue;
        }
        if(!n.targetGuid) {
            float nearest=std::numeric_limits<float>::max();
            for(auto* p:players)if(!p->dead && isAggressive(*p,n)) {
                const float radius=def->aggroRadius>0?def->aggroRadius:std::clamp(20.0f+float(n.level)-float(p->level),5.0f,45.0f);
                const float d=distance2(*p,n);if(d<radius*radius && d<nearest){nearest=d;n.targetGuid=p->guid;}
            }
        }
        if(n.targetGuid&&!std::any_of(n.threat.begin(),n.threat.end(),[&](const auto& e){return e.guid==n.targetGuid;}))g.addThreat(n,n.targetGuid,1);
        auto* target=g.player(n.targetGuid,players);
        // An owned creature that has taken the top threat is attacked as the
        // real target it is. Nothing here reaches its owner's health or auras.
        if(!target&&n.targetGuid)if(auto* summon=g.pet(n.targetGuid)) {
            localResetNpcSpellState(n);
            const float leash=distance2(n.x,n.y,n.z,n.homeX,n.homeY,n.homeZ);
            const float reach=distance2(n.x,n.y,n.z,summon->x,summon->y,summon->z);
            if(summon->dead||!summon->health||summon->mapId!=n.mapId||summon->instanceId!=n.instanceId||
               !std::isfinite(reach)||reach>70*70||leash>60*60) {
                for(auto& entry:n.threat)if(entry.guid==summon->guid)entry={};
                n.targetGuid=0;g.selectThreatTarget(n,players);changed=true;continue;
            }
            if(!stunned)n.attackTimer=std::max(0.0f,n.attackTimer-dt);
            n.orientation=std::atan2(summon->y-n.y,summon->x-n.x);
            if(reach>3*3&&!stunned) {
                const float dx=summon->x-n.x,dy=summon->y-n.y,dz=summon->z-n.z;
                const float length=std::sqrt(reach),stride=std::min(pursuitStep,std::max(0.0f,length-2.5f));
                if(length>0&&stride>0){n.x+=dx/length*stride;n.y+=dy/length*stride;n.z+=dz/length*stride;changed=true;}
            }
            if(localWithinMeleeRange(reach,localCreatureCombatReach(def),localCreatureCombatReach(content().npc(summon->entry)))&&
               n.attackTimer<=0&&!stunned) {
                n.attackTimer=2;
                const auto outcome=localRollNpcAgainstPet(n,*summon,g.meleeRoll());
                const uint32_t raw=uint32_t(std::min(uint64_t(1000000),
                    uint64_t(def->damage)*(outcome==LocalMeleeOutcome::Critical?4:outcome==LocalMeleeOutcome::Crushing?3:2)/2));
                g.damagePetByNpc(*summon,n,raw,players,outcome);
                changed=true;
            }
            continue;
        }
        if(n.targetGuid&&(!target||target->dead||!canAttack(*target,n)||distance2(*target,n)>70*70||distance2(n.x,n.y,n.z,n.homeX,n.homeY,n.homeZ)>60*60)) {
            localResetNpcSpellState(n);n.targetGuid=0;n.threat={};n.combatEpoch=g.allocateNpcEpoch();n.lootOwner=0;n.health=n.maxHealth;n.x=n.homeX;n.y=n.homeY;n.z=n.homeZ;n.snares.clear();g.releaseNpcControls(n);n.damageAuras.clear();n.stormstrikeAuras.clear();changed=true;continue;
        }
        if(!target||!n.targetGuid){localResetNpcSpellState(n);continue;}
        // Unit::SetStunned freezes the swing before anything else. The timer
        // does not run down while stunned, so a stun costs the creature the
        // whole swing rather than only its landing.
        if(!stunned)n.attackTimer=std::max(0.0f,n.attackTimer-dt);
        // Spell::CheckCasterAuras: a stun blocks the cast outright, while a
        // silence blocks only a spell whose OWN PreventionType is silence. The
        // prevention type belongs to the spell being prevented, never to the
        // aura preventing it, so this reads the creature's own spell.
        const auto* npcCastProfile=localNpcSpellProfile(n.entry);
        const auto* npcCastSpell=npcCastProfile?content().spell(npcCastProfile->spellId):nullptr;
        const bool castBlocked=stunned||
            (silenced&&npcCastSpell&&npcCastSpell->preventionType==kLocalPreventionSilence);
        if(castBlocked)localResetNpcSpellState(n);
        else {
        #include "local_npc_spell_tick.inc"
        }
        const float d2=distance2(*target,n);
        n.orientation=std::atan2(target->y-n.y,target->x-n.x);
        if(d2>0.0001f){const auto facing=std::atan2(target->y-n.y,target->x-n.x);changed=changed||n.orientation!=facing;n.orientation=facing;}
        if(d2>3*3&&!stunned) {
            const float dx=target->x-n.x,dy=target->y-n.y,dz=target->z-n.z;
            const float length=std::sqrt(d2),step=std::min(pursuitStep,std::max(0.0f,length-2.5f));
            if(length>0){n.x+=dx/length*step;n.y+=dy/length*step;n.z+=dz/length*step;changed=changed||step>0;}
        }
        // UnitAI::DoMeleeAttackIfReady, UnitAI.cpp:50: the creature's own
        // IsWithinMeleeRange, which was a fixed 4 yd here.
        if(localWithinMeleeRange(d2,localCreatureCombatReach(def),kLocalDefaultCombatReach)&&n.attackTimer<=0&&!stunned) {
            n.attackTimer=2;const auto meleeStats=localMeleeStats(*target,content());
            const auto outcome=localRollNpcMelee(n,*target,meleeStats,g.meleeRoll());
            const uint32_t armor=localMeleeArmor(*target,content());
            const uint32_t raw=uint32_t(std::min(uint64_t(1000000),uint64_t(def->damage)*(outcome==LocalMeleeOutcome::Critical?4:outcome==LocalMeleeOutcome::Crushing?3:2)/2));
            const auto attempted=localMeleeAvoided(outcome)?0u:localFormDamage(*target,localArmorReducedDamage(localIncomingDamageAfterTalents(*target,content(),raw),armor,n.level),true);
            const auto blocked=outcome==LocalMeleeOutcome::Block?std::min(attempted,meleeStats.blockValue):0;
            const uint32_t damage=localAbsorbDamage(*target,content(),attempted-blocked,1);
            if(outcome==LocalMeleeOutcome::Parry){const auto minimum=localMeleeSpeed(*target,content())*.2f;if(target->attackTimer>minimum)target->attackTimer=std::max(minimum,target->attackTimer-localMeleeSpeed(*target,content())*.4f);}
            stats(*target,content(),false);
            const auto effective=std::min(damage,target->health);
            if(damage>=target->health){
                g.emitCombatEvent(localKillProcEvent(n.guid,target->guid,target->mapId,target->instanceId,false,n.level,false),players);
                g.emitCombatEvent(localDeathProcEvent(target->guid,target->mapId,target->instanceId,true,target->level),players);
                target->health=0;localStopRangedAuto(*target);leaveLocalForm(*target);clearLocalCombo(*target);target->dead=true;target->mountSpellId=0;target->deadTimer=0;target->attackTarget=0;clearCast(*target,LocalCastStatus::Interrupted);
                g.selectThreatTarget(n,players);
                if(!n.targetGuid){n.threat={};n.combatEpoch=g.allocateNpcEpoch();n.lootOwner=0;n.health=n.maxHealth;n.x=n.homeX;n.y=n.homeY;n.z=n.homeZ;n.snares.clear();g.releaseNpcControls(n);n.damageAuras.clear();n.stormstrikeAuras.clear();}}
            else {
                target->health-=damage;
                // Direct NPC melee: abort flags also react to fully absorbed hits.
                // Ordinary pushback requires health damage and is capped at two
                // 500 ms delays, with remaining time never above the original cast.
                if(!localMeleeAvoided(outcome)&&target->castingSpellId)if(const auto* casting=content().spell(target->castingSpellId)) {
                    if(casting->interruptFlags&0x10u)clearCast(*target,LocalCastStatus::Interrupted);
                    else if(damage && (casting->interruptFlags&2u) && !casting->noPushback && target->castPushbackCount<2) {
                        ++target->castPushbackCount;
                        const auto reduction=std::min(100u,localTalentPushbackReduction(*target,content(),*casting)+localProcPushbackReduction(*target,content(),*casting));
                        const auto delay=std::min(500u*(100u-reduction)/100u,target->castTotalMs-target->castRemainingMs);
                        target->castRemainingMs+=delay;target->castPushbackMs+=delay;
                    }
                }
                if (damage && target->resourceType == LocalResourceType::Rage) target->mana=std::min(target->maxMana,target->mana+std::min(10U,damage/4+1));
            }
            g.emitCombatEvent({0,n.guid,target->guid,0,target->mapId,target->instanceId,attempted,effective,attempted-blocked-damage,LocalCombatEventKind::NpcMelee,target->dead,0,outcome,blocked},players);
            // Unit::DealMeleeDamage:2126-2128 - the shield runs only when the
            // swing actually dealt damage, and after the swing is resolved.
            if(damage&&!target->dead)g.dealAreaAuraShieldDamage(*target,n,players);
            changed=true;
        }
    }
    for(auto* p:players)if(p->comboPoints&&!localComboTargetValid(*p,g.npc(p->comboTarget))){clearLocalCombo(*p);changed=true;}
    for(auto* player:players)if(player && player->dead)localCaptureCorpse(*player);
    g.refreshHealingViews(players);
    return changed;
}
void LocalGameplay::refreshInventoryObjectives(LocalRealmPlayer& player){questStatus(player,content());}
} // namespace wowee::game
