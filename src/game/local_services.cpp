#include "game/local_services.hpp"
#include "game/local_world_catalog.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace wowee::game {
namespace {

constexpr LocalServiceNpcRecord kServiceNpcs[] = {
#include "game/local_service_npcs_generated.inc"
};

constexpr LocalVendorOffer kVendorOffers[] = {
#include "game/local_vendor_stock_generated.inc"
};
constexpr LocalVendorPrice kVendorPrices[] = {
#include "game/local_vendor_prices_generated.inc"
};
// Above the largest upstream gold-only merchant, also bounded for overrides.
constexpr size_t kMaxVendorStock = 256;

} // namespace

const LocalServiceNpcRecord* localServiceNpcRecord(uint32_t entry) {
    if (!entry) return nullptr;
    const auto* begin = std::begin(kServiceNpcs);
    const auto* end = std::end(kServiceNpcs);
    const auto* found = std::lower_bound(begin, end, entry,
        [](const LocalServiceNpcRecord& row, uint32_t key) { return row.entry < key; });
    return found != end && found->entry == entry ? found : nullptr;
}

uint32_t localEffectiveNpcFlags(const LocalNpcDefinition& definition) {
    // The catalog is the authority whenever it says anything at all. A catalog
    // that carries npcflag disagreeing with the transcription is a re-import of
    // newer upstream data, and the newer answer is the right one.
    if (definition.npcFlags) return definition.npcFlags;
    const auto* row = localServiceNpcRecord(definition.id);
    return row ? row->npcFlags : 0;
}

uint8_t localVendorCategories(uint32_t npcFlags) {
    uint8_t categories = 0;
    if (npcFlags & kLocalNpcFlagVendor) categories |= 1u << uint8_t(LocalVendorCategory::General);
    if (npcFlags & kLocalNpcFlagVendorFood) categories |= 1u << uint8_t(LocalVendorCategory::Food);
    if (npcFlags & kLocalNpcFlagVendorAmmo) categories |= 1u << uint8_t(LocalVendorCategory::Ammunition);
    if (npcFlags & kLocalNpcFlagVendorReagent) categories |= 1u << uint8_t(LocalVendorCategory::Reagent);
    if (npcFlags & kLocalNpcFlagVendorPoison) categories |= 1u << uint8_t(LocalVendorCategory::Poison);
    return categories;
}

const LocalVendorOffer* localVendorOffer(uint32_t entry, uint32_t itemId) {
    const auto* first = std::lower_bound(std::begin(kVendorOffers), std::end(kVendorOffers), entry,
        [](const LocalVendorOffer& row, uint32_t id) { return row.entry < id; });
    for (auto* row = first; row != std::end(kVendorOffers) && row->entry == entry; ++row)
        if (row->itemId == itemId) return row;
    return nullptr;
}

const LocalVendorPrice* localVendorPrice(uint32_t itemId) {
    const auto* row = std::lower_bound(std::begin(kVendorPrices), std::end(kVendorPrices), itemId,
        [](const LocalVendorPrice& price, uint32_t id) { return price.itemId < id; });
    return row != std::end(kVendorPrices) && row->itemId == itemId ? row : nullptr;
}

uint32_t localVendorBuyCount(uint32_t itemId) {
    const auto* price = localVendorPrice(itemId);
    return price ? std::max(1u, price->buyCount) : 1;
}

std::vector<uint32_t> localVendorStockForNpc(uint32_t entry,
                                            const std::vector<uint32_t>& catalogStock,
                                            const LocalWorldContent& content) {
    std::vector<uint32_t> stock;
    if (!entry) return stock;
    if (!catalogStock.empty()) {
        for (uint32_t item : catalogStock) {
            if (stock.size() == kMaxVendorStock) break;
            if (content.item(item) && std::find(stock.begin(), stock.end(), item) == stock.end())
                stock.push_back(item);
        }
        return stock;
    }
    auto* row = std::lower_bound(std::begin(kVendorOffers), std::end(kVendorOffers), entry,
        [](const LocalVendorOffer& offer, uint32_t id) { return offer.entry < id; });
    for (; row != std::end(kVendorOffers) && row->entry == entry; ++row) {
        if (stock.size() == kMaxVendorStock) break;
        if (content.item(row->itemId)) stock.push_back(row->itemId);
    }
    return stock;
}

uint64_t localVendorBuyTotal(const LocalItemDefinition& item, uint32_t count) {
    if (!count) return 0;
    const auto* source = localVendorPrice(item.id);
    if (source && item.value == source->sellPrice) {
        // BuyPrice covers BuyCount units. Round a partial bundle upward so a
        // sub-copper arrow cannot be bought for zero. Normal UI buys bundles.
        const uint64_t total = uint64_t(source->buyPrice) * count;
        const uint64_t bundle=std::max(1u,source->buyCount);
        return (total + bundle - 1) / bundle;
    }
    // A user-authored catalog can reprice an item. Retain its explicit value
    // under the legacy local rule rather than force a foreign source price.
    const uint64_t unit = std::max<uint64_t>(uint64_t(item.value) * 5, 1);
    // Public helper accepts uint32 quantities; saturate only at uint64 overflow.
    if (unit > UINT64_MAX / count) return UINT64_MAX;
    return unit * count;
}
uint32_t localVendorBuyPrice(const LocalItemDefinition& item, uint32_t count) {
    return uint32_t(std::min<uint64_t>(localVendorBuyTotal(item,count),1000000000ULL));
}

uint32_t LocalVendorInventory::restocked(const Stock& stock, double now) {
    if (!std::isfinite(now) || now <= stock.since) return stock.remaining;
    const double periods = std::floor((now - stock.since) / std::max(1u, stock.interval));
    const uint32_t missing = stock.maximum - std::min(stock.maximum, stock.remaining);
    if (periods >= std::ceil(double(missing) / std::max(1u, stock.bundle))) return stock.maximum;
    return stock.remaining + uint32_t(periods) * stock.bundle;
}

uint32_t LocalVendorInventory::available(uint64_t npcGuid, const LocalVendorOffer& offer,
                                         uint32_t, double now) const {
    if (!offer.maxCount) return std::numeric_limits<uint32_t>::max();
    const auto found = depleted_.find({npcGuid, offer.itemId});
    return found == depleted_.end() ? offer.maxCount : restocked(found->second, now);
}

bool LocalVendorInventory::consume(uint64_t npcGuid, const LocalVendorOffer& offer, uint32_t count,
                                   uint32_t buyCount, double now) {
    if (!count || !npcGuid || !std::isfinite(now)) return false;
    if (!offer.maxCount) return true;
    if (!offer.restockSeconds || count > offer.maxCount) return false;
    const auto key = std::make_pair(npcGuid, offer.itemId);
    auto found = depleted_.find(key);
    const uint32_t remaining = found == depleted_.end() ? offer.maxCount : restocked(found->second, now);
    if (count > remaining) return false;
    if (found == depleted_.end()) {
        if (depleted_.size() >= MaxDepletedOffers) {
            for (auto it = depleted_.begin(); it != depleted_.end();) {
                if (restocked(it->second, now) == it->second.maximum) it = depleted_.erase(it);
                else ++it;
            }
        }
        if (depleted_.size() >= MaxDepletedOffers) return false;
        found = depleted_.emplace(key, Stock{remaining, offer.maxCount,
            std::max(1u, buyCount), offer.restockSeconds, now, offer.entry}).first;
    } else if (remaining == offer.maxCount) {
        found->second.since = now;
    } else if (remaining > found->second.remaining) {
        // Keep the residual fraction of a restock interval after buying again.
        const auto periods = std::floor((now - found->second.since) / found->second.interval);
        found->second.since += periods * found->second.interval;
    }
    found->second.remaining = remaining - count;
    return true;
}

std::vector<LocalVendorStockRecord> LocalVendorInventory::snapshot(double now) const {
    if (!std::isfinite(now)) throw std::invalid_argument("Invalid merchant snapshot time");
    std::vector<LocalVendorStockRecord> records;
    records.reserve(depleted_.size());
    for (const auto& [key, stock] : depleted_) {
        const auto remaining = restocked(stock, now);
        if (remaining == stock.maximum) continue;
        const double elapsed = std::fmod(std::max(0.0, now - stock.since), double(stock.interval));
        const auto elapsedMs = std::min(uint64_t(elapsed * 1000.0), uint64_t(stock.interval) * 1000 - 1);
        records.push_back({key.first, stock.entry, key.second, remaining, elapsedMs});
    }
    return records;
}

bool LocalVendorInventory::restore(const std::vector<LocalVendorStockRecord>& records, double now) {
    if (!std::isfinite(now) || records.size() > MaxDepletedOffers) return false;
    decltype(depleted_) candidate;
    for (const auto& row : records) {
        const auto* offer = localVendorOffer(row.entry, row.itemId);
        if (!row.npcGuid || !offer || !offer->maxCount || !offer->restockSeconds ||
            row.remaining >= offer->maxCount || row.elapsedMs >= uint64_t(offer->restockSeconds) * 1000)
            return false;
        Stock stock{row.remaining, offer->maxCount, localVendorBuyCount(row.itemId),
                    offer->restockSeconds, now - double(row.elapsedMs) / 1000.0, row.entry};
        if (!candidate.emplace(std::make_pair(row.npcGuid, row.itemId), stock).second) return false;
    }
    depleted_.swap(candidate);
    return true;
}

uint32_t localVendorSellPrice(const LocalItemDefinition& item, uint32_t count) {
    // Exactly the value the catalog states, which is item_template.SellPrice.
    return uint32_t(std::min<uint64_t>(uint64_t(item.value) * count, 1000000000ULL));
}

const std::vector<LocalSkillLine>& localBuiltinProfessions() {
    // SkillLine.dbc ids and categories. These are client constants, not server
    // data: the same numbers appear in the player's own SkillLine.dbc, which is
    // what setSkillLines() replaces them with when the MPQs are open.
    static const std::vector<LocalSkillLine> lines = {
        {129, kLocalSkillCategorySecondary,  "First Aid"},
        {164, kLocalSkillCategoryProfession, "Blacksmithing"},
        {165, kLocalSkillCategoryProfession, "Leatherworking"},
        {171, kLocalSkillCategoryProfession, "Alchemy"},
        {182, kLocalSkillCategoryProfession, "Herbalism"},
        {185, kLocalSkillCategorySecondary,  "Cooking"},
        {186, kLocalSkillCategoryProfession, "Mining"},
        {197, kLocalSkillCategoryProfession, "Tailoring"},
        {202, kLocalSkillCategoryProfession, "Engineering"},
        {333, kLocalSkillCategoryProfession, "Enchanting"},
        {356, kLocalSkillCategorySecondary,  "Fishing"},
        {393, kLocalSkillCategoryProfession, "Skinning"},
        {755, kLocalSkillCategoryProfession, "Jewelcrafting"},
        {773, kLocalSkillCategoryProfession, "Inscription"},
    };
    return lines;
}

const LocalSkillLine* localProfession(const std::vector<LocalSkillLine>& lines, uint32_t skillId) {
    for (const auto& line : lines) if (line.id == skillId) return &line;
    return nullptr;
}

const std::vector<LocalProfessionRank>& localProfessionRanks() {
    // Caps and character levels are the client's own: the skill window shows
    // 75/150/225/300/375/450, and those are the levels at which a trainer will
    // teach the next rank. The prices are a local schedule; see the header.
    static const std::vector<LocalProfessionRank> ranks = {
        { 75,  5,       0, "Apprentice"},
        {150, 10,   10000, "Journeyman"},
        {225, 20,   50000, "Expert"},
        {300, 35,  200000, "Artisan"},
        {375, 50,  500000, "Master"},
        {450, 65, 1000000, "Grand Master"},
    };
    return ranks;
}

const LocalProfessionRank* localNextProfessionRank(uint16_t cap) {
    for (const auto& rank : localProfessionRanks()) if (rank.cap > cap) return &rank;
    return nullptr;
}

uint32_t localTrainerSpellCost(const LocalSpellDefinition& spell) {
    const uint64_t level = std::max(1u, spell.baseLevel);
    return uint32_t(std::min(level * kLocalTrainerCostPerLevel, uint64_t(1000000000)));
}

uint32_t localCraftSkillChance(const LocalRecipe& recipe, uint16_t skill) {
    // A recipe whose own data states no trivial ranks is one the client never
    // meant to raise a skill. Granting a point for it would invent progress.
    if (!recipe.trivialHigh || skill >= recipe.trivialHigh) return 0;
    if (skill <= recipe.trivialLow || recipe.trivialHigh <= recipe.trivialLow) return 1000;
    const uint32_t span = uint32_t(recipe.trivialHigh - recipe.trivialLow);
    return uint32_t(uint64_t(recipe.trivialHigh - skill) * 1000 / span);
}

uint32_t localRecipeCost(const LocalRecipe& recipe) {
    const uint64_t skill = std::max<uint32_t>(recipe.requiredSkill, 1);
    return uint32_t(std::min(skill * kLocalRecipeCostPerSkill, uint64_t(1000000000)));
}

bool localRecipeAllows(const LocalRecipe& recipe, const LocalRealmPlayer& player) {
    if (!recipe.unsupportedReason.empty() || !player.race || player.race > 32 ||
        !player.classId || player.classId > 32) return false;
    const uint32_t race = 1u << (player.race - 1), cls = 1u << (player.classId - 1);
    return recipe.access.empty() || std::any_of(recipe.access.begin(), recipe.access.end(), [&](const auto& a) {
        return (!a.races || (a.races & race)) && (!a.classes || (a.classes & cls)) &&
            !(a.excludedRaces & race) && !(a.excludedClasses & cls);
    });
}

bool localRecipeHasTools(const LocalRecipe& recipe, const LocalRealmPlayer& player) {
    for (auto tool : recipe.tools) if (tool && std::none_of(player.inventory.begin(), player.inventory.end(),
        [&](const auto& stack) { return stack.itemId == tool && stack.count; })) return false;
    return true;
}

uint32_t localRecipeReagentCount(const LocalRecipe& recipe, const LocalRealmPlayer& player, uint32_t itemId) {
    uint32_t total = 0;
    for (const auto& stack : player.inventory) if (stack.itemId == itemId) total += stack.count;
    const auto equipped = uint32_t(std::count(player.equipment.begin(), player.equipment.end(), itemId));
    // An equipped copy can also satisfy the tool requirement; reserve it once.
    const uint32_t reserve = std::max(equipped, uint32_t(std::find(recipe.tools.begin(), recipe.tools.end(), itemId) != recipe.tools.end()));
    return total - std::min(total, reserve);
}

} // namespace wowee::game

namespace wowee::game {
const std::vector<LocalMailboxSite>& localMailboxSites(const LocalWorldContent& c,const LocalRealmPlayer& p) {
    if(!c.mailboxSitesReady || c.mailboxMap!=p.mapId || std::hypot(p.x-c.mailboxX,p.y-c.mailboxY)>32.f || std::abs(p.z-c.mailboxZ)>32.f){
        c.mailboxSites.clear();
        // Local service placements near the two reported starting hubs.
        // Shared by authority and renderer, so Square uses the visible object's position.
        for(const auto& site:std::array<LocalMailboxSite,2>{{
            {0x0A1C000000000001ULL,0,-8943.0f,-132.0f,83.6f,0.0f},
            {0x0A1C00000000000AULL,530,10345.0f,-6362.0f,33.4f,0.0f}}})
            if(site.mapId==p.mapId && std::hypot(site.x-p.x,site.y-p.y)<192.f)c.mailboxSites.push_back(site);
        std::vector<LocalNpcSpawn> spawns;
        if(c.catalog){std::string error;c.catalog->query3D(p.mapId,p.x,p.y,p.z,192.f,LocalWorldCatalog::MaxResults,spawns,error);}
        spawns.insert(spawns.end(),c.spawns.begin(),c.spawns.end());
        for(const auto& s:spawns)if(s.mapId==p.mapId && std::hypot(s.x-p.x,s.y-p.y)<192.f)
            if(const auto* d=c.npc(s.entry);d && (localEffectiveNpcFlags(*d)&kLocalNpcFlagInnkeeper)){
                const uint64_t guid=0x0A1B000000000000ULL|s.id;
                if(std::none_of(c.mailboxSites.begin(),c.mailboxSites.end(),[&](const auto& m){return m.guid==guid;}))
                    c.mailboxSites.push_back({guid,s.mapId,s.x+3.f*std::cos(s.orientation),s.y+3.f*std::sin(s.orientation),s.z,s.orientation});
            }
        c.mailboxSitesReady=true;c.mailboxMap=p.mapId;c.mailboxX=p.x;c.mailboxY=p.y;c.mailboxZ=p.z;
    }
    return c.mailboxSites;
}
const LocalMailboxSite* nearbyLocalMailbox(const LocalWorldContent& c,const LocalRealmPlayer& p,uint64_t guid) {
    if(p.dead || p.instanceId || p.flight.active || p.transportEntry || p.castingSpellId || p.attackTarget)return nullptr;
    const LocalMailboxSite* best=nullptr;float limit=25.f;
    for(const auto& m:localMailboxSites(c,p))if(m.mapId==p.mapId && (!guid || m.guid==guid)){
        const float dx=p.x-m.x,dy=p.y-m.y,dz=p.z-m.z;const float distance=dx*dx+dy*dy+dz*dz;
        if(distance<=limit){limit=distance;best=&m;}
    }
    return best;
}
}
