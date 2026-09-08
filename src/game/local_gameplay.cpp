#include "game/local_gameplay.hpp"
#include "game/local_mount.hpp"
#include "game/local_services.hpp"
#include "game/local_scripted_portals.hpp"
#include "game/local_world_catalog.hpp"
#include "game/local_transport_passenger.hpp"
#include "core/logger.hpp"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
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
uint32_t number(const Json& j, const char* key, uint32_t fallback, uint32_t max = 1000000000) {
    if (!j.contains(key)) return fallback;
    const auto& v = j.at(key);
    if (!v.is_number_integer() || (v.is_number_integer() && v.get<int64_t>() < 0) || v.get<uint64_t>() > max)
        throw std::runtime_error(std::string("Invalid unsigned value: ") + key);
    return v.get<uint32_t>();
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
    for (auto& stack : p.inventory) if (stack.itemId == id && count) {
        const auto moved = std::min(count, uint32_t(def->stack - stack.count));
        stack.count += uint16_t(moved); count -= moved;
    }
    while (count) { const uint16_t moved = uint16_t(std::min(count, uint32_t(def->stack))); p.inventory.push_back({id,moved}); count -= moved; }
    return true;
}
void removeItem(LocalRealmPlayer& p, uint32_t id, uint32_t count) {
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
    // Keep authoritative derived health inside the save/snapshot vitals bound.
    p.maxHealth=std::min(1000000U,100+uint32_t(p.level-1)*25+equipmentValue(p,c,0));
    p.maxMana=p.resourceType == LocalResourceType::Mana ? 100+uint32_t(p.level-1)*10 : 100;
    p.health=heal?p.maxHealth:std::min(p.health,p.maxHealth);
    p.mana=heal ? (p.resourceType == LocalResourceType::Rage || p.resourceType == LocalResourceType::RunicPower ? 0 : p.maxMana) : std::min(p.mana,p.maxMana);
}
void experience(LocalRealmPlayer& p,const LocalWorldContent& c,uint32_t reward) {
    p.xp=uint32_t(std::min(uint64_t(p.xp)+reward,uint64_t(1000000000)));
    while(p.level<80 && p.xp>=p.xpToLevel) {p.xp-=p.xpToLevel;++p.level;stats(p,c,true);}
    if(p.level==80)p.xp=0;
}
uint32_t spellResourceCost(const LocalRealmPlayer& p, const LocalSpellDefinition& d) {
    // Percent costs use this standalone realm's base resource pool.
    return d.mana + uint32_t(uint64_t(p.maxMana) * d.manaPercent / 100);
}
void clearCast(LocalRealmPlayer& p, LocalCastStatus status) {
    p.castingSpellId=0;p.castTarget=0;p.castRemainingMs=0;p.castTotalMs=0;p.castStatus=status;
}
uint32_t spellAmount(const LocalRealmPlayer& p, const LocalSpellDefinition& d, bool heal) {
    const auto low=heal?d.heal:d.damage, high=heal?d.healMax:d.damageMax;
    if(!d.clientSpell) return low + (heal?0:uint32_t(p.level-1)*2);
    const auto effective=d.maxLevel?std::min(uint32_t(p.level),d.maxLevel):uint32_t(p.level);
    const float scale=heal?d.healPerLevel:d.damagePerLevel;
    const double amount=(double(low)+std::max(low,high))*0.5 +
        double(effective>d.baseLevel?effective-d.baseLevel:0)*scale;
    return uint32_t(std::clamp(amount,0.0,1000000.0));
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
    std::shared_ptr<LocalWorldContent> content = std::make_shared<LocalWorldContent>();
    std::vector<LocalRealmNpc> npcs;
    LocalVendorInventory vendorInventory;
    struct PeriodicDamage { uint64_t owner=0,target=0;uint32_t spell=0,remaining=0,next=0,interval=0,damage=0; };
    std::vector<PeriodicDamage> periodicDamage;
    std::unordered_map<std::string,std::vector<size_t>> grid;
    std::unordered_map<uint64_t,double> respawnAt;
    std::vector<LocalAreaTriggerVolume> volumes;
    std::vector<LocalFactionTemplate> factions;
    std::array<uint32_t, 12> raceFactions{};
    std::vector<LocalInstanceState> instances;
    uint32_t nextInstanceId = 1;
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
    float regionTimer=1;
    // Said once per realm, the first time anything asks what portals exist. See
    // the scan in LocalGameplay::portals().
    bool portalScanLogged=false;
    mutable std::unordered_map<uint64_t,uint32_t> portalExitLatch;
    void rebuild() {
        grid.clear();npcs.clear();crewSpawns.clear();respawnAt.clear();periodicDamage.clear();vendorInventory.clear();regionTimer=1;
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
    void kill(LocalRealmNpc& n,LocalRealmPlayer& killer,const std::vector<LocalRealmPlayer*>& players) {
        const auto* def=content->npc(n.entry);if(!def)return;
        n.health=0;n.dead=true;n.targetGuid=0;n.lootable=!def->loot.empty()||def->money;
        if(!n.lootOwner)n.lootOwner=killer.guid;
        n.respawnTimer=def->respawnSeconds;respawnAt[n.guid]=now+def->respawnSeconds;
        // Local co-op: nearby living members share quest kill credit and XP.
        for(auto* p:players)if(!p->dead&&distance2(*p,n)<=60*60) {
            experience(*p,*content,def->xp);objectiveCredit(*p,*content,LocalQuestObjective::Type::Kill,n.entry);
            if(p->attackTarget==n.guid)p->attackTarget=0;
        }
    }
    void damageNpc(LocalRealmNpc& n,LocalRealmPlayer& attacker,uint32_t damage,const std::vector<LocalRealmPlayer*>& players,bool physical=true) {
        if(n.dead)return;
        const auto* def=content->npc(n.entry);if(!def)return;
        if(physical) damage=std::max(1U,damage>def->armor/4?damage-def->armor/4:1U);
        if (attacker.resourceType == LocalResourceType::Rage) attacker.mana = std::min(attacker.maxMana, attacker.mana + std::min(15U, damage / 3 + 1));
        if(!n.lootOwner)n.lootOwner=attacker.guid;
        n.targetGuid=attacker.guid;
        if(damage>=n.health)kill(n,attacker,players);else n.health-=damage;
    }
};

LocalGameplay::LocalGameplay():impl_(std::make_unique<Impl>()){}
LocalGameplay::~LocalGameplay()=default;
LocalGameplay::LocalGameplay(LocalGameplay&&) noexcept=default;
LocalGameplay& LocalGameplay::operator=(LocalGameplay&&) noexcept=default;
const LocalWorldContent& LocalGameplay::content()const{return *impl_->content;}
std::shared_ptr<LocalWorldContent> LocalGameplay::sharedContent()const{return impl_->content;}
void LocalGameplay::useContent(std::shared_ptr<LocalWorldContent> c){impl_->content=std::move(c);impl_->rebuild();}
const std::vector<LocalRealmNpc>& LocalGameplay::npcs()const{return impl_->npcs;}
void LocalGameplay::setRemoteNpcs(std::vector<LocalRealmNpc> n){impl_->npcs=std::move(n);}

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
    impl_->rebuild();
    return true;
}
bool LocalGameplay::setStarterSpells(const std::vector<LocalSpellDefinition>& spells,
        const std::string& diagnostic, std::string& error) {
    // Was twenty hand-picked starters; it is now the whole class progression
    // the client's SkillLineAbility.dbc describes, bounded by the import's own
    // documented cap (kLocalMaxImportedClassAbilities) plus the starters.
    if(spells.size()>2048) {error="Too many client spells";return false;}
    std::set<uint32_t> ids;
    for(const auto& d:spells) if(!d.id||!ids.insert(d.id).second||!d.clientSpell||!d.allowableClasses||
            d.name.size()>96||d.unsupportedReason.size()>256) {error="Invalid client starter spell";return false;}
    auto& c=*impl_->content;
    // Hash only deterministic simulation metadata; locale text/icon paths and
    // diagnostic wording cannot split otherwise compatible LAN clients.
    const auto hash=[&](uint32_t value) {for(unsigned b=0;b<4;++b)c.fingerprint=(c.fingerprint^uint8_t(value>>(b*8)))*16777619U;};
    auto sorted=spells;std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    hash(0x42313153);hash(uint32_t(sorted.size()));
    for(const auto& d:sorted) {
        const uint32_t values[]={d.id,d.allowableClasses,d.resourceType,d.mana,d.manaPercent,d.cooldownMs,
            d.castTimeMs,d.globalCooldownMs,d.durationMs,d.baseLevel,d.maxLevel,d.damage,d.damageMax,
            d.heal,d.healMax,d.periodicDamage,d.periodicIntervalMs,uint32_t(d.unsupportedReason.empty()),
            d.supercededBySpell,d.mountCreatureId,d.mountDisplayId,d.mountSpeedPercent,
            d.runeCost[0],d.runeCost[1],d.runeCost[2],d.runicPowerGain};
        for(auto value:values) hash(value);
        for(float value:{d.range,d.minRange,d.damagePerLevel,d.healPerLevel}) {uint32_t bits;std::memcpy(&bits,&value,4);hash(bits);}
        const auto existing=std::find_if(c.spells.begin(),c.spells.end(),[&](const auto& old){return old.id==d.id;});
        if(existing==c.spells.end())c.spells.push_back(d);else *existing=d;
    }
    std::sort(c.spells.begin(),c.spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});
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
    return impl_->travel.destinationsFrom(fromNode, player.knownTaxiNodes);
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
    impl_->instances = instances; impl_->nextInstanceId = next; error.clear(); return true;
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
            (npcFlag == kLocalNpcFlagTrainerClass && npc.classTrainer) ||
            (npcFlag == kLocalNpcFlagTrainerProfession && npc.professionTrainer) ||
            (npcFlag == kLocalNpcFlagInnkeeper && npc.innkeeper) ||
            (npcFlag == kLocalNpcFlagAuctioneer && npc.auctioneer);
        if (!offers) continue;
        // A merchant that is hostile to this character is not open for
        // business, the same way a hostile quest contact refuses to talk.
        if (isAggressive(p, npc)) continue;
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
namespace {
/// The level at which the client's own data makes an ability available.
/// Spell.dbc leaves spellLevel at zero for a great many rows, and those are
/// available from the start rather than never.
uint8_t spellUnlockLevel(const LocalSpellDefinition& d) {
    return uint8_t(std::clamp<uint32_t>(d.baseLevel, 1, 80));
}
/// Is a higher rank of this ability already within this character's reach?
/// Retail replaces a rank rather than stacking it; keeping both would double
/// the ability in the spellbook and let a trainer sell one already outgrown.
bool supercededHere(const LocalWorldContent& c, const LocalSpellDefinition& d, uint8_t level) {
    const auto* next = d.supercededBySpell ? c.spell(d.supercededBySpell) : nullptr;
    return next && next->clientSpell && next->unsupportedReason.empty() && spellUnlockLevel(*next) <= level;
}
}
std::vector<uint32_t> LocalGameplay::trainableSpells(const LocalRealmPlayer& p) const {
    std::vector<uint32_t> result;
    const auto* trainer = serviceNpc(p, kLocalNpcFlagTrainerClass);
    if (!trainer || !trainer->trainerClass || trainer->trainerClass != p.classId) return result;
    for (const auto& spell : content().spells) {
        // Only abilities imported from the player's own Spell.dbc, only for
        // this class, and only ones the ruleset can actually cast: offering an
        // ability whose import failed would sell a spell that refuses to fire.
        if (!spell.clientSpell || spell.mountDisplayId || !spell.unsupportedReason.empty()) continue;
        if (!spell.allowableClasses || !(spell.allowableClasses & (1u << (p.classId - 1)))) continue;
        if (spellUnlockLevel(spell) > p.level) continue;
        if (std::find(p.knownSpells.begin(), p.knownSpells.end(), spell.id) != p.knownSpells.end()) continue;
        // Only the highest rank this character has reached. Retail replaces a
        // rank rather than stacking it, so offering rank three to a character
        // who could have rank five would sell them the wrong ability.
        if (supercededHere(content(), spell, p.level)) continue;
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
            r.reagents.size() > 8 || r.name.empty() || r.name.size() > 96 || !seen.insert(r.spellId).second) {
            error = "Invalid client recipe " + std::to_string(r.spellId); return false;
        }
        for (const auto& reagent : r.reagents)
            if (!reagent.itemId || !reagent.count) { error = "Invalid recipe reagent"; return false; }
    }
    auto& c = *impl_->content;
    // Recipes are simulation input like the spell set, so they take part in the
    // content fingerprint: two consoles whose clients produced different
    // recipes must not share a realm and silently disagree about a craft.
    const auto hash = [&](uint32_t value) {
        for (unsigned b = 0; b < 4; ++b) c.fingerprint = (c.fingerprint ^ uint8_t(value >> (b * 8))) * 16777619U;
    };
    hash(0x52435031); hash(uint32_t(recipes.size()));
    for (const auto& r : recipes) {
        for (auto value : {r.spellId, uint32_t(r.skillId), uint32_t(r.requiredSkill), uint32_t(r.trivialHigh),
                           uint32_t(r.trivialLow), r.createdItemId, uint32_t(r.createdCount),
                           uint32_t(r.reagents.size())}) hash(value);
        for (const auto& reagent : r.reagents) { hash(reagent.itemId); hash(reagent.count); }
    }
    c.recipes = std::move(recipes);
    error.clear(); return true;
}
std::vector<uint32_t> LocalGameplay::trainableRecipes(const LocalRealmPlayer& p) const {
    std::vector<uint32_t> result;
    const auto* trainer = serviceNpc(p, kLocalNpcFlagTrainerProfession);
    if (!trainer || !trainer->trainerSkill) return result;
    const auto known = std::find_if(p.professions.begin(), p.professions.end(),
        [&](const LocalProfessionSkill& s) { return s.skillId == trainer->trainerSkill; });
    if (known == p.professions.end()) return result;
    for (const auto& recipe : content().recipes) {
        if (recipe.skillId != trainer->trainerSkill) continue;
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
        if (!recipe) continue;
        bool haveAll = true;
        for (const auto& reagent : recipe->reagents)
            haveAll = haveAll && totalItem(p, reagent.itemId) >= reagent.count;
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
bool LocalGameplay::canAttack(const LocalRealmPlayer& p, const LocalRealmNpc& n) const {
    const auto* d = content().npc(n.entry);
    if (!d || (d->unitFlags & (0x2U | 0x100U | 0x02000000U))) return false;
    const auto* npcFaction = definition(impl_->factions, d->faction);
    const auto* playerFaction = p.race < impl_->raceFactions.size() ? definition(impl_->factions, impl_->raceFactions[p.race]) : nullptr;
    if (!npcFaction || !playerFaction) return n.hostile && !n.questGiver;
    const int relation = factionRelation(*npcFaction, *playerFaction);
    if (relation > 0) return true;
    if (relation < 0 || factionRelation(*playerFaction, *npcFaction) < 0) return false;
    // Neutral mobs can be attacked; quest contacts remain protected unless
    // their actual faction is hostile to this race.
    return !n.questGiver;
}
bool LocalGameplay::isAggressive(const LocalRealmPlayer& p, const LocalRealmNpc& n) const {
    const auto* d = content().npc(n.entry); if (!d || !canAttack(p, n)) return false;
    const auto* npcFaction = definition(impl_->factions, d->faction);
    const auto* playerFaction = p.race < impl_->raceFactions.size() ? definition(impl_->factions, impl_->raceFactions[p.race]) : nullptr;
    return npcFaction && playerFaction ? factionRelation(*npcFaction, *playerFaction) > 0 : n.hostile && d->aggroRadius > 0;
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
        error.clear();return true;
    } catch(const std::exception& e){error="Local world content: "+std::string(e.what());return false;}
}

bool LocalGameplay::validatePlayer(const LocalRealmPlayer& p, std::string& error) const {
    if (!validCharacterOptions(p.race, p.classId, p.gender)) { error = "Invalid saved race/class profile"; return false; }
    if (p.instanceId) {
        bool found = false;
        for (const auto& i : impl_->instances) if (i.id == p.instanceId && i.mapId == p.mapId) found = true;
        if (!found) { error = "Saved character references a missing instance binding; save preserved"; return false; }
    }
    if (!p.gameplayInitialized) return true; // B1 is migrated with its original position/identity.
    const auto& c = content();
    const auto invalid = [&](const std::string& reason) { error = "Saved character " + p.name + " is incompatible with world content: " + reason + ". Original save preserved."; return false; };
    if (p.inventory.size() > MaxInventory || p.quests.size() > MaxQuests || p.knownSpells.size() > MaxSpells ||
        p.knownRecipes.size() > MaxRecipes || p.cooldowns.size() > MaxCooldowns) return invalid("collection limit exceeded");
    for (const auto& s : p.inventory) {
        const auto* d = c.item(s.itemId);
        if (!d || !s.count || s.count > d->stack) return invalid("unknown item or changed stack limit for item " + std::to_string(s.itemId));
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

void LocalGameplay::initializePlayer(LocalRealmPlayer& p, bool fresh) {
    const auto& c = content();
    const auto oldResource = p.resourceType;
    if (c.classResources) p.resourceType = classResource(p.classId);
    if (!p.gameplayInitialized) {
        if (fresh) {
            p.mapId = c.start.mapId; p.x = c.start.x; p.y = c.start.y; p.z = c.start.z; p.orientation = c.start.orientation;
            if (c.catalog) for (const auto& start : c.catalog->starts()) if (start.race == p.race && start.classId == p.classId) {
                p.mapId = start.mapId; p.x = start.x; p.y = start.y; p.z = start.z;
                p.orientation = start.orientation; p.level = start.level;
                break;
            }
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
            for(const auto& spell:c.spells) if(spell.clientSpell && !spell.mountDisplayId &&
                (spell.allowableClasses & (1u << (p.classId-1))) && spellUnlockLevel(spell) <= p.level &&
                !supercededHere(c, spell, p.level) && p.knownSpells.size()<MaxSpells)
                p.knownSpells.push_back(spell.id);
        } else for (auto id : c.start.knownSpells) if (const auto* spell = c.spell(id))
            if (!spell->allowableClasses || (spell->allowableClasses & (1u << (p.classId - 1)))) p.knownSpells.push_back(id);
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
        stats(p, c, false);
        if (p.resourceType != oldResource) p.mana = p.resourceType == LocalResourceType::Rage || p.resourceType == LocalResourceType::RunicPower ? 0 : p.maxMana;
    }
    clearCast(p,LocalCastStatus::None);p.globalCooldownMs=0;
    p.castRevision=0;p.lastCastSpellId=0;p.lastCastTarget=0;
    p.attackTarget = 0; p.attackTimer = 0; p.deadTimer = 0;
    // A character that logs in, is created or is migrated is not mid-fall,
    // whatever the last thing to touch these was.
    p.mountSpellId = 0;
    p.movementState = 0; p.falling = false; p.fallStartZ = p.z; p.fallRevision = p.positionRevision;
    questStatus(p, c);
}

bool LocalGameplay::execute(LocalRealmPlayer& p,const LocalRealmCommand& cmd,const std::vector<LocalRealmPlayer*>& players,std::string& result) {
    auto& g=*impl_;const auto& c=content();
    // Takes a string rather than a literal: the merchant and trainer refusals
    // below have to name the item, profession or rank they are refusing, and a
    // refusal a player cannot act on is barely better than none.
    const auto reject=[&](const std::string& reason){result=reason;return false;};
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
    auto* n=g.npc(cmd.target);
    if(cmd.action==LocalAction::Respawn) {
        if(!p.dead)return reject("You are alive");
        if(p.deadTimer<3)return reject("Revive is available after 3 seconds");
        p.dead=false;p.attackTarget=0;p.deadTimer=0;
        if (p.instanceId && p.hasInstanceReturn) {
            p.mapId=p.returnMapId;p.instanceId=p.returnInstanceId;p.x=p.returnX;p.y=p.returnY;p.z=p.returnZ;p.orientation=p.returnOrientation;p.hasInstanceReturn=false;
        } else {
            p.instanceId=0;p.mapId=c.start.mapId;p.x=c.start.x;p.y=c.start.y;p.z=c.start.z;p.orientation=c.start.orientation;
            if (c.catalog) for (const auto& start : c.catalog->starts()) if (start.race == p.race && start.classId == p.classId) {
                p.mapId=start.mapId;p.x=start.x;p.y=start.y;p.z=start.z;p.orientation=start.orientation;break;
            }
        }
        p.portalCooldown=2;++p.positionRevision;
        stats(p,c,true);result="Revived at the starting sanctuary";return true;
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
    if(p.dead)return reject("You are dead; choose Revive");
    if (cmd.action == LocalAction::LeaveInstance) {
        if (!p.instanceId || !p.hasInstanceReturn) return reject("You are not inside a local instance");
        p.mapId=p.returnMapId;p.instanceId=p.returnInstanceId;p.x=p.returnX;p.y=p.returnY;p.z=p.returnZ;p.orientation=p.returnOrientation;
        p.hasInstanceReturn=false;p.attackTarget=0;p.portalCooldown=2;++p.positionRevision;
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
            g.portalExitLatch[p.guid]=destination->id;
            result=std::string("Travelled to ")+pad->name;
            LOG_INFO("[LOCAL_ACHERUS] pad=",pad->helperEntry," destination=",destination->helperEntry,
                " map=",p.mapId," xyz=",p.x,",",p.y,",",p.z);
            return true;
        }
        if (!c.catalog || !insidePortal(cmd.id, p)) return reject("Stand inside the actual entrance trigger");
        if (p.portalCooldown > 0) return reject("Wait before using another portal");
        const LocalCatalogDestination* destination=nullptr;
        for(const auto& d:c.catalog->destinations())if(d.id==cmd.id){destination=&d;break;}
        if(!destination)return reject("This trigger has no supported teleport destination");
        uint32_t instanceId=0;
        // Either the catalog's instance_template or the client's own Map.dbc is
        // enough to make this an instance. That is what lets a dungeon the
        // upstream dump did not carry still open as one.
        if(instanceMap(destination->mapId)) {
            const uint64_t group=cmd.target ? p.guid : 0;
            for(const auto& instance:g.instances)if(instance.mapId==destination->mapId && instance.groupId==group){instanceId=instance.id;break;}
            if(!instanceId) {
                if(g.instances.size()>=MaxInstances || g.nextInstanceId>65535)return reject("Local instance binding limit reached (128)");
                instanceId=g.nextInstanceId++;g.instances.push_back({instanceId,destination->mapId,group});
            }
            if(!p.instanceId) {
                p.returnMapId=p.mapId;p.returnInstanceId=p.instanceId;p.returnX=p.x;p.returnY=p.y;p.returnZ=p.z;p.returnOrientation=p.orientation;p.hasInstanceReturn=true;
            }
        } else p.hasInstanceReturn=false;
        p.mapId=destination->mapId;p.instanceId=instanceId;p.x=destination->x;p.y=destination->y;p.z=destination->z;p.orientation=destination->orientation;
        p.attackTarget=0;p.portalCooldown=2;++p.positionRevision;g.regionTimer=1;
        const auto* targetMap = clientMap(destination->mapId);
        const std::string where = destination->name.empty() && targetMap ? targetMap->name : destination->name;
        result = instanceId ? (targetMap && targetMap->instanceType == 2 ? "Entered raid instance: " : "Entered instance: ") + where
                            : "Travelled to " + where;
        return true;
    }

    // --- Travel ------------------------------------------------------------
    if (cmd.action == LocalAction::TakeFlight) {
        if (p.flight.active) return reject("You are already in flight");
        if (p.transportEntry) return reject("Step off the transport first");
        if (!g.travel.loaded()) return reject("No taxi data; client DBCs are not loaded");
        // The flight is bought from a flight master, so one has to be here.
        // The node the player departs from is the one that NPC serves, never a
        // node the client asked for - which is what stops a guest from
        // departing anywhere it likes.
        const LocalRealmNpc* master = nullptr;
        for (const auto& npc : g.npcs) {
            if (!npc.flightMaster || npc.dead) continue;
            if (npc.mapId != p.mapId || npc.instanceId != p.instanceId) continue;
            if (distance2(p, npc) > 8 * 8) continue;
            master = &npc;
            break;
        }
        if (!master) return reject("Speak to a flight master");
        if (master->taxiNodeId == cmd.id) return reject("You are already here");
        // Standing at the flight master is how a node becomes known, so the
        // departure node is discovered even if the player walked here.
        discoverTaxiNode(p, master->taxiNodeId);
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
        p.money -= route->cost;
        p.flight = flight;
        p.attackTarget = 0;
        if (p.castingSpellId) clearCast(p, LocalCastStatus::Interrupted);
        const LocalTaxiNode* destination = g.travel.node(cmd.id);
        result = "Flying to " + (destination ? destination->name : std::to_string(cmd.id));
        return true;
    }
    if (cmd.action == LocalAction::LeaveTransport) {
        if (!p.transportEntry) return reject("You are not on a transport");
        p.transportEntry = 0;
        p.transportOffsetX = p.transportOffsetY = p.transportOffsetZ = 0;
        result = "Stepped off the transport";
        return true;
    }
    if (cmd.action == LocalAction::BoardTransport) {
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
    // None of these takes the NPC from the client. The authority finds the one
    // the player is standing at, exactly as TakeFlight does, so a guest can
    // never trade with a merchant on the far side of the world, buy training
    // from something that is not a trainer, or bind its home to thin air.
    if (cmd.action == LocalAction::SellToVendor || cmd.action == LocalAction::BuyFromVendor) {
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
            const auto price = localVendorBuyPrice(*item, count);
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
        const auto paid = localVendorSellPrice(*item, count);
        removeItem(p, cmd.id, count);
        p.money = uint32_t(std::min(uint64_t(p.money) + paid, uint64_t(1000000000)));
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
    if (cmd.action == LocalAction::LearnSpell) {
        if (cmd.target) return reject("Training takes only an ability");
        const auto* trainer = serviceNpc(p, kLocalNpcFlagTrainerClass);
        if (!trainer) return reject("Stand at a trainer of your own class");
        if (!trainer->trainerClass) return reject("This trainer teaches something this realm does not model");
        if (trainer->trainerClass != p.classId) return reject("This trainer does not teach your class");
        const auto teachable = trainableSpells(p);
        if (std::find(teachable.begin(), teachable.end(), cmd.id) == teachable.end())
            return reject("This trainer cannot teach you that yet");
        if (p.knownSpells.size() >= MaxSpells) return reject("Your spellbook is full (" + std::to_string(MaxSpells) + " abilities)");
        const auto* spell = c.spell(cmd.id);
        if (!spell) return reject("No such ability");
        const auto cost = localTrainerSpellCost(*spell);
        if (cost > p.money) return reject("You cannot afford that training");
        p.money -= cost;
        p.knownSpells.push_back(cmd.id);
        // The rank this one replaces goes with it, the way retail overwrites a
        // rank rather than leaving both in the book. Its cooldown goes too:
        // an entry for a spell nobody knows fails the save's own validation.
        const auto superceded = std::find_if(p.knownSpells.begin(), p.knownSpells.end(), [&](uint32_t id) {
            const auto* previous = c.spell(id);
            return previous && previous->supercededBySpell == cmd.id;
        });
        if (superceded != p.knownSpells.end()) {
            const auto old = *superceded;
            p.knownSpells.erase(superceded);
            p.cooldowns.erase(std::remove_if(p.cooldowns.begin(), p.cooldowns.end(),
                [&](const LocalCooldown& cd) { return cd.spellId == old; }), p.cooldowns.end());
        }
        result = "Learned " + spell->name;
        return true;
    }
    if (cmd.action == LocalAction::LearnRecipe) {
        if (cmd.target) return reject("Training takes only a recipe");
        const auto* recipe = c.recipe(cmd.id);
        if (!recipe) return reject("Your client's data describes no such recipe");
        const auto teachable = trainableRecipes(p);
        if (std::find(teachable.begin(), teachable.end(), cmd.id) == teachable.end())
            return reject("This trainer cannot teach you " + recipe->name + " yet");
        if (p.knownRecipes.size() >= MaxRecipes)
            return reject("Your recipe book is full (" + std::to_string(MaxRecipes) + ")");
        const auto cost = localRecipeCost(*recipe);
        if (cost > p.money) return reject("You cannot afford that recipe");
        p.money -= cost;
        p.knownRecipes.push_back(cmd.id);
        std::sort(p.knownRecipes.begin(), p.knownRecipes.end());
        result = "Learned " + recipe->name;
        return true;
    }
    if (cmd.action == LocalAction::CraftItem) {
        // One at a time, and no trainer needed: a craft is the character's own
        // work. Keeping it to a single item keeps the whole thing atomic, which
        // is what stops a half-finished batch eating reagents for nothing.
        if (cmd.target) return reject("Crafting takes only a recipe");
        if (std::find(p.knownRecipes.begin(), p.knownRecipes.end(), cmd.id) == p.knownRecipes.end())
            return reject("You have not learned that recipe");
        const auto* recipe = c.recipe(cmd.id);
        if (!recipe) return reject("Your client's data no longer describes that recipe");
        auto skill = std::find_if(p.professions.begin(), p.professions.end(),
            [&](const LocalProfessionSkill& s) { return s.skillId == recipe->skillId; });
        if (skill == p.professions.end()) return reject("You no longer have that profession");
        if (recipe->requiredSkill > skill->current)
            return reject("Requires skill " + std::to_string(recipe->requiredSkill));
        for (const auto& reagent : recipe->reagents) {
            if (totalItem(p, reagent.itemId) >= reagent.count) continue;
            const auto* item = c.item(reagent.itemId);
            return reject("You need " + std::to_string(reagent.count) + "x " +
                          (item ? item->name : std::to_string(reagent.itemId)));
        }
        // Build the finished character before committing any of it: reagents
        // must not be consumed by a craft whose product has nowhere to go.
        auto candidate = p;
        for (const auto& reagent : recipe->reagents) removeItem(candidate, reagent.itemId, reagent.count);
        if (!addItem(candidate, c, recipe->createdItemId, recipe->createdCount))
            return reject("Inventory full; free space before crafting");
        // The client's own trivial ranks say what this is worth. Accumulated
        // rather than rolled - see localCraftSkillChance about why.
        auto& learned = *std::find_if(candidate.professions.begin(), candidate.professions.end(),
            [&](const LocalProfessionSkill& s) { return s.skillId == recipe->skillId; });
        std::string gained;
        if (learned.current < learned.max) {
            learned.progress = uint16_t(learned.progress + localCraftSkillChance(*recipe, learned.current));
            while (learned.progress >= 1000 && learned.current < learned.max) {
                learned.progress = uint16_t(learned.progress - 1000);
                ++learned.current;
                gained = " (skill " + std::to_string(learned.current) + "/" + std::to_string(learned.max) + ")";
            }
            if (learned.current >= learned.max) learned.progress = 0;
        }
        p = std::move(candidate);
        stats(p, c, false); questStatus(p, c);
        const auto* product = c.item(recipe->createdItemId);
        result = "Crafted " + (product ? product->name : recipe->name) + gained;
        return true;
    }
    if (cmd.action == LocalAction::LearnProfession || cmd.action == LocalAction::TrainProfessionRank) {
        if (cmd.target) return reject("Training takes only a profession");
        const auto* trainer = serviceNpc(p, kLocalNpcFlagTrainerProfession);
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
                    return reject("You already have two primary professions; unlearning is not implemented");
            }
            // Retail gates the primary professions on character level and lets
            // anyone pick up a secondary skill; so does this.
            if (line->category == kLocalSkillCategoryProfession && p.level < ranks.front().level)
                return reject("Come back at level " + std::to_string(ranks.front().level));
            if (ranks.front().cost > p.money) return reject("You cannot afford that training");
            p.money -= ranks.front().cost;
            p.professions.push_back({uint16_t(cmd.id), 1, ranks.front().cap});
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
        if (cmd.target || cmd.id) return reject("Binding takes no argument");
        const auto* innkeeper = serviceNpc(p, kLocalNpcFlagInnkeeper);
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
        result = "Returned to the inn";
        return true;
    }

    if(cmd.action==LocalAction::CancelCast) {if(!p.castingSpellId)return reject("No spell is being cast");clearCast(p,LocalCastStatus::Interrupted);result="Cast cancelled";return true;}
    if(cmd.action==LocalAction::StopAttack){p.attackTarget=0;if(p.castingSpellId)clearCast(p,LocalCastStatus::Interrupted);result="Attack and casting stopped";return true;}
    if(cmd.action==LocalAction::Attack) {
        if(!n||n->dead||!canAttack(p,*n))return reject("Choose a living enemy");
        if(distance2(p,*n)>30*30)return reject("Target is too far away");
        p.mountSpellId=0;p.attackTarget=n->guid;result="Attacking "+n->name;return true;
    }
    if(cmd.action==LocalAction::CastSpell) return executeCastSpell(p,cmd,players,result,false);
    if(cmd.action==LocalAction::EquipItem) {
        if (!equipItem(p, c, cmd.id, cmd.target)) return reject("Item cannot be equipped in that slot");
        stats(p,c,false);result="Equipped "+c.item(cmd.id)->name;return true;
    }
    if (cmd.action == LocalAction::UnequipItem) {
        if (cmd.target || cmd.id >= p.equipment.size() || !p.equipment[cmd.id] || !validEquipment(p, c))
            return reject("Equipment slot cannot be cleared");
        const auto* item = c.item(p.equipment[cmd.id]);
        p.equipment[cmd.id] = 0;
        stats(p,c,false);result="Unequipped "+item->name;return true;
    }
    if(cmd.action==LocalAction::UseItem) {
        if(const auto* metadata=localAuctionMetadata(cmd.id);metadata && metadata->mountSpell) {
            if(!totalItem(p,cmd.id))return reject("Mount item is not in your inventory");
            if(!localMountSupported(c,cmd.id))return reject("This mount's flight or scripted effects are not supported locally");
            if(p.level<metadata->requiredLevel)return reject("Level too low to learn this mount");
            if(metadata->allowableRaces && !(metadata->allowableRaces&(1u<<(p.race-1))))return reject("This mount cannot be learned by your race");
            if(metadata->allowableClasses && !(metadata->allowableClasses&(1u<<(p.classId-1))))return reject("This mount cannot be learned by your class");
            if(std::find(p.knownSpells.begin(),p.knownSpells.end(),metadata->mountSpell)!=p.knownSpells.end())return reject("You already know this mount");
            if(p.knownSpells.size()>=MaxSpells)return reject("Your spellbook is full");
            p.knownSpells.push_back(metadata->mountSpell);removeItem(p,cmd.id,1);questStatus(p,c);
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
        if(n->lootOwner!=p.guid)return reject("Loot belongs to the player who engaged this enemy");
        const auto* def=c.npc(n->entry);if(!def)return reject("Missing loot definition");
        auto candidate=p;for(const auto& s:def->loot)if(!addItem(candidate,c,s.itemId,s.count))return reject("Inventory full; free space before looting");
        candidate.money=uint32_t(std::min(uint64_t(candidate.money)+def->money,uint64_t(1000000000)));p=std::move(candidate);n->lootable=false;questStatus(p,c);result="Loot received";return true;
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
        if (questRewarded(p, cmd.id)) return reject("Quest reward already claimed");
        auto candidate=p;
        questStatus(candidate,c);
        const auto progress=std::find_if(candidate.quests.begin(),candidate.quests.end(),[&](const LocalQuestProgress& q){return q.id==cmd.id;});
        if(progress==candidate.quests.end()||progress->status!=LocalQuestStatus::Complete)return reject("Quest objectives are not complete, or reward already claimed");
        if (candidate.completedQuestIds.size() >= MaxCompletedQuests) return reject("Completed quest history storage limit reached; reward remains unclaimed");
        for(const auto& obj:def->objectives)if(obj.type==LocalQuestObjective::Type::Collect)removeItem(candidate,obj.entry,obj.count);
        if(def->rewardItem&&!addItem(candidate,c,def->rewardItem,def->rewardCount))return reject("Inventory full; free space for the reward");
        candidate.money=uint32_t(std::min(uint64_t(candidate.money)+def->money,uint64_t(1000000000)));experience(candidate,c,def->xp);
        candidate.quests.erase(progress);
        candidate.completedQuestIds.insert(std::lower_bound(candidate.completedQuestIds.begin(), candidate.completedQuestIds.end(), cmd.id), cmd.id);
        p=std::move(candidate);stats(p,c,false);questStatus(p,c);result="Quest rewarded: "+def->title;return true;
    }
    return reject("Unsupported local action");
}

bool LocalGameplay::executeCastSpell(LocalRealmPlayer& p,const LocalRealmCommand& cmd,
        const std::vector<LocalRealmPlayer*>& players,std::string& result,bool finishing) {
    auto& g=*impl_;const auto& c=content();
    const auto reject=[&](const std::string& reason){result=reason;if(finishing)clearCast(p,LocalCastStatus::Failed);return false;};
    const auto* d=c.spell(cmd.id);
    if(!d||std::find(p.knownSpells.begin(),p.knownSpells.end(),cmd.id)==p.knownSpells.end())return reject("Spell is not learned");
    if(p.dead)return reject("Cannot cast while dead");
    if(d->mountDisplayId) {
        if(!finishing && p.mountSpellId==d->id) {p.mountSpellId=0;result="Dismounted";return true;}
        if(p.flight.active || p.transportEntry || p.instanceId || (p.movementState&kLocalMovementInLiquid))
            return reject("Cannot mount while swimming, travelling or inside an instance");
        if(p.attackTarget || std::any_of(g.npcs.begin(),g.npcs.end(),[&](const auto& npc){return !npc.dead && npc.targetGuid==p.guid;}))
            return reject("Cannot mount during combat");
    }
    if(!d->unsupportedReason.empty())return reject(d->name+": "+d->unsupportedReason);
    if(!finishing&&p.castingSpellId)return reject("A spell is already being cast; move or stop to cancel");
    if(!finishing&&p.globalCooldownMs)return reject("Global cooldown is active");
    if(d->allowableClasses&&!(d->allowableClasses&(1u<<(p.classId-1))))return reject("This ability is not available to your class");
    if(d->resourceType==5 ? p.classId!=6 : d->resourceType!=255&&d->resourceType!=uint8_t(p.resourceType))
        return reject("Ability uses a different resource type");
    const auto runeMask=selectLocalRunes(p.classId,p.runeCooldownMs,d->runeCost);
    if(!runeMask)return reject("Not enough ready runes");
    const auto cost=d->resourceType==5?0u:spellResourceCost(p,*d);
    if(p.mana<cost)return reject("Not enough resource");
    for(const auto& cd:p.cooldowns)if(cd.spellId==cmd.id&&cd.remainingMs)return reject("Spell is on cooldown");
    auto* n=g.npc(cmd.target);LocalRealmPlayer* healed=nullptr;
    if(d->damage||d->periodicDamage) {
        if(!n||n->dead||!canAttack(p,*n))return reject("Choose a living enemy");
        const auto range=distance2(p,*n);
        if(range>d->range*d->range||range<d->minRange*d->minRange)return reject("Spell target out of range");
    }
    if(d->heal) {
        healed=cmd.target?g.player(cmd.target,players):&p;if(!healed&&d->damage)healed=&p;
        if(!healed||healed->dead)return reject("Choose a living player");
        const auto range=distance2(p.x,p.y,p.z,healed->x,healed->y,healed->z);
        if(healed->mapId!=p.mapId||healed->instanceId!=p.instanceId||range>d->range*d->range||range<d->minRange*d->minRange)
            return reject("Healing target out of range");
    }
    if(!finishing&&d->castTimeMs) {
        p.castingSpellId=d->id;p.castTarget=cmd.target;p.castRemainingMs=p.castTotalMs=d->castTimeMs;
        p.castOriginX=p.x;p.castOriginY=p.y;p.castOriginZ=p.z;p.castOriginMap=p.mapId;p.castOriginInstance=p.instanceId;
        p.castStatus=LocalCastStatus::Casting;p.globalCooldownMs=d->globalCooldownMs;
        result="Casting "+d->name;return true;
    }
    p.mountSpellId=d->mountDisplayId?d->id:0;
    p.mana-=cost;
    consumeLocalRunes(p.runeCooldownMs,*runeMask);
    if(p.resourceType==LocalResourceType::RunicPower)
        p.mana=uint32_t(std::min(uint64_t(p.maxMana),uint64_t(p.mana)+d->runicPowerGain));
    if(!finishing)p.globalCooldownMs=d->globalCooldownMs;
    const auto cooldown=d->clientSpell?d->cooldownMs:std::max(500U,d->cooldownMs);
    if(cooldown) {
        auto cd=std::find_if(p.cooldowns.begin(),p.cooldowns.end(),[&](const auto& e){return e.spellId==cmd.id;});
        if(cd!=p.cooldowns.end())cd->remainingMs=cooldown;
        else if(p.cooldowns.size()<MaxCooldowns)p.cooldowns.push_back({cmd.id,cooldown});
    }
    if(d->damage)g.damageNpc(*n,p,spellAmount(p,*d,false),players,!d->clientSpell);
    if(healed)healed->health=uint32_t(std::min(uint64_t(healed->maxHealth),uint64_t(healed->health)+spellAmount(p,*d,true)));
    if(d->periodicDamage&&n&&!n->dead) {
        auto found=std::find_if(g.periodicDamage.begin(),g.periodicDamage.end(),[&](const auto& e){return e.owner==p.guid&&e.target==n->guid&&e.spell==d->id;});
        Impl::PeriodicDamage aura{p.guid,n->guid,d->id,d->durationMs,d->periodicIntervalMs,d->periodicIntervalMs,d->periodicDamage};
        if(found!=g.periodicDamage.end())*found=aura;else if(g.periodicDamage.size()<MaxNpcs*8)g.periodicDamage.push_back(aura);
    }
    if (!++p.castRevision) ++p.castRevision;
    p.lastCastSpellId=d->id;p.lastCastTarget=healed?healed->guid:cmd.target;
    clearCast(p,LocalCastStatus::Finished);result="Cast "+d->name;return true;
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
    auto& g=*impl_;if(!std::isfinite(seconds)||seconds<0)return false;
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
    bool changed=false;
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
        if (!p || !std::isfinite(p->z) || p->z > kLocalWorldFloorZ) continue;
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
        p->castingSpellId = 0; p->castRemainingMs = 0; p->castTotalMs = 0;
        p->castStatus = LocalCastStatus::None;
        p->attackTarget = 0; p->attackTimer = 0;
        p->flight = LocalFlightState{};
        p->transportEntry = 0;
        ++p->positionRevision;
        changed = true;
        LOG_WARNING("[LOCAL_RESCUE] recovered a character from below the world: guid=",
                    p->guid, " to map=", p->mapId, " xyz=", p->x, ",", p->y, ",", p->z,
                    " via=", hadReturn ? "instance return" : (p->hasHome ? "inn" : "world start"));
    }
    const uint32_t elapsedMs=uint32_t(dt*1000+.5f);
    for(auto& aura:g.periodicDamage) {
        auto* owner=g.player(aura.owner,players);auto* target=g.npc(aura.target);
        if(!owner||owner->dead||!target||target->dead||owner->mapId!=target->mapId||owner->instanceId!=target->instanceId) {aura.remaining=0;continue;}
        auto elapsed=std::min(elapsedMs,aura.remaining);
        aura.remaining-=elapsed;
        while(aura.next<=elapsed&&aura.interval&&!target->dead) {
            elapsed-=aura.next;aura.next=aura.interval;
            g.damageNpc(*target,*owner,aura.damage,players,false);changed=true;
        }
        aura.next=elapsed<aura.next?aura.next-elapsed:0;
    }
    g.periodicDamage.erase(std::remove_if(g.periodicDamage.begin(),g.periodicDamage.end(),[](const auto& aura){return !aura.remaining;}),g.periodicDamage.end());
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
                        p->health = 0; p->dead = true; p->mountSpellId=0; p->deadTimer = 0; p->attackTarget = 0;
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
        const uint32_t ms=uint32_t(dt*1000+0.5f);
        // Travel can return early below; riding state and rune recovery still
        // advance so landing cannot restore a mount that was cleared in flight.
        if(p->mountSpellId && (p->dead || p->flight.active || p->transportEntry || p->instanceId || (p->movementState&kLocalMovementInLiquid))) {
            p->mountSpellId=0;changed=true;
        }
        changed=advanceLocalRunes(p->runeCooldownMs,ms)||changed;
        // A flight owns the character's position for its duration. Combat,
        // casting and NPC aggro are all suppressed by the same rule that
        // suppresses them for a dead player: nothing else runs for them below.
        if (p->flight.active) {
            uint32_t mapId = p->mapId;
            float x = p->x, y = p->y, z = p->z, orientation = p->orientation;
            g.travel.advanceFlight(p->flight, dt, mapId, x, y, z, orientation);
            const bool moved = mapId != p->mapId || x != p->x || y != p->y || z != p->z;
            p->mapId = mapId; p->x = x; p->y = y; p->z = z; p->orientation = orientation;
            if (moved) ++p->positionRevision;
            if (!p->flight.active) {
                // Arriving is how the destination becomes a known node.
                discoverTaxiNode(*p, p->flight.destinationNode);
                p->flight.destinationNode = 0;
            }
            p->attackTarget = 0;
            changed = true;
            continue;
        }
        // A passenger rides with the hull. Storing the offset rather than
        // re-seating the player each tick is what lets them walk around on
        // deck while the ship moves under them.
        if (p->transportEntry) {
            const LocalTransportState* hull = nullptr;
            for (const auto& t : g.transports) if (t.entry == p->transportEntry) { hull = &t; break; }
            if (!hull) {
                // The route went away (content reload). Leave the player where
                // they are rather than at the origin.
                p->transportEntry = 0;
            } else {
                const float c = std::cos(hull->orientation), s = std::sin(hull->orientation);
                const float x = hull->x + c * p->transportOffsetX - s * p->transportOffsetY;
                const float y = hull->y + s * p->transportOffsetX + c * p->transportOffsetY;
                // Smooth travel is handled by the client's live deck transform.
                // Only a seam invalidates the client's current map/camera position.
                if (p->mapId != hull->mapId || std::hypot(x - p->x, y - p->y) > 200.0f)
                    ++p->positionRevision;
                p->orientation = std::remainder(p->orientation + hull->orientation - p->transportLastYaw, 6.28318530718f);
                if (p->orientation < 0) p->orientation += 6.28318530718f;
                p->transportLastYaw = hull->orientation;
                p->mapId = hull->mapId;
                p->x = x; p->y = y;
                p->z = hull->z + p->transportOffsetZ;
                changed = true;
            }
        }
        for(auto& cd:p->cooldowns)if(cd.remainingMs){cd.remainingMs=cd.remainingMs>ms?cd.remainingMs-ms:0;changed=true;}
        if(p->globalCooldownMs){p->globalCooldownMs=p->globalCooldownMs>ms?p->globalCooldownMs-ms:0;changed=true;}
        if(p->castingSpellId) {
            changed=true;
            if(p->dead||p->mapId!=p->castOriginMap||p->instanceId!=p->castOriginInstance||
               distance2(p->x,p->y,p->z,p->castOriginX,p->castOriginY,p->castOriginZ)>.01f)
                clearCast(*p,LocalCastStatus::Interrupted);
            else if(p->castRemainingMs<=ms) {
                const LocalRealmCommand command{LocalAction::CastSpell,p->castTarget,p->castingSpellId};
                std::string outcome;executeCastSpell(*p,command,players,outcome,true);
            } else p->castRemainingMs-=ms;
        }
        if(p->dead){p->deadTimer+=dt;continue;}
        p->attackTimer=std::max(0.0f,p->attackTimer-dt);
        if(p->attackTarget) {
            auto* n=g.npc(p->attackTarget);
            if(!n||n->dead||n->mapId!=p->mapId||n->instanceId!=p->instanceId||distance2(*p,*n)>60*60){p->attackTarget=0;changed=true;}
            else if(!p->castingSpellId&&distance2(*p,*n)<=4.5f*4.5f&&p->attackTimer<=0) {
                p->attackTimer=1.6f;g.damageNpc(*n,*p,8+uint32_t(p->level)*2+equipmentValue(*p,content(),1),players);changed=true;
            }
        }
        bool combat=p->attackTarget!=0||p->castingSpellId!=0;
        for(const auto& n:g.npcs)if(n.targetGuid==p->guid){combat=true;break;}
        p->regenerationTimer+=dt;
        if(p->regenerationTimer>=1) {
            p->regenerationTimer=0;
            const uint32_t oldHealth=p->health,oldMana=p->mana;
            if(!combat)p->health=std::min(p->maxHealth,p->health+std::max(1U,p->maxHealth/20));
            if (p->resourceType == LocalResourceType::Energy) p->mana=std::min(p->maxMana,p->mana+10);
            else if (p->resourceType == LocalResourceType::Mana) p->mana=std::min(p->maxMana,p->mana+(combat?1U:std::max(1U,p->maxMana/15)));
            else if (!combat && p->mana) --p->mana;
            changed=changed||p->health!=oldHealth||p->mana!=oldMana;
        }
    }
    for(auto& n:g.npcs) {
        const auto* def=content().npc(n.entry);if(!def)continue;
        if(n.dead) {
            n.respawnTimer-=dt;
            if(n.respawnTimer<=0){n.dead=false;n.health=n.maxHealth;n.x=n.homeX;n.y=n.homeY;n.z=n.homeZ;n.lootable=false;n.lootOwner=0;n.targetGuid=0;changed=true;}
            continue;
        }
        // Crew stays in authored deck-local positions. Ordinary ground pursuit
        // would send it through the hull or back to the dock as the ship moves.
        if (n.transportEntry) { n.targetGuid=0; continue; }
        if(!n.targetGuid) {
            float nearest=std::numeric_limits<float>::max();
            for(auto* p:players)if(!p->dead && isAggressive(*p,n)) {
                const float radius=def->aggroRadius>0?def->aggroRadius:std::clamp(20.0f+float(n.level)-float(p->level),5.0f,45.0f);
                const float d=distance2(*p,n);if(d<radius*radius && d<nearest){nearest=d;n.targetGuid=p->guid;}
            }
        }
        auto* target=g.player(n.targetGuid,players);
        if(n.targetGuid&&(!target||target->dead||distance2(*target,n)>70*70||distance2(n.x,n.y,n.z,n.homeX,n.homeY,n.homeZ)>60*60)) {
            n.targetGuid=0;n.lootOwner=0;n.health=n.maxHealth;n.x=n.homeX;n.y=n.homeY;n.z=n.homeZ;changed=true;continue;
        }
        if(!target||!n.targetGuid)continue;
        const float d2=distance2(*target,n);
        n.orientation=std::atan2(target->y-n.y,target->x-n.x);
        if(d2>3*3) {
            const float dx=target->x-n.x,dy=target->y-n.y,dz=target->z-n.z;
            const float length=std::sqrt(d2),step=std::min(4.0f*dt,std::max(0.0f,length-2.5f));
            if(length>0){n.x+=dx/length*step;n.y+=dy/length*step;n.z+=dz/length*step;changed=changed||step>0;}
        }
        n.attackTimer=std::max(0.0f,n.attackTimer-dt);
        if(d2<=4*4&&n.attackTimer<=0) {
            n.attackTimer=2;const uint32_t armor=equipmentValue(*target,content(),2);const uint32_t damage=std::max(1U,def->damage>armor/4?def->damage-armor/4:1U);
            if(damage>=target->health){target->health=0;target->dead=true;target->mountSpellId=0;target->deadTimer=0;target->attackTarget=0;clearCast(*target,LocalCastStatus::Interrupted);n.targetGuid=0;n.lootOwner=0;n.health=n.maxHealth;n.x=n.homeX;n.y=n.homeY;n.z=n.homeZ;}
            else {
                target->health-=damage;
                if (target->resourceType == LocalResourceType::Rage) target->mana=std::min(target->maxMana,target->mana+std::min(10U,damage/4+1));
            }
            changed=true;
        }
    }
    return changed;
}
} // namespace wowee::game
