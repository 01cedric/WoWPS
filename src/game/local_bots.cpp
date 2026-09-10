#include "game/local_bots.hpp"
#include "game/local_inventory_layout.hpp"
#include "game/local_auction_catalog.hpp"
#include "game/local_mount.hpp"

#include <algorithm>
#include <cmath>

namespace wowee::game {
namespace {

/// Names for the bot roster. Ordinary given names rather than anything that
/// reads as a label: a roster of "Bot 1".."Bot 16" tells a player they are
/// alone more loudly than an empty roster would.
constexpr const char* kBotNames[] = {
    "Aldren",  "Brynja",  "Corvin",  "Dalia",   "Eirik",   "Fenna",
    "Gorim",   "Halvar",  "Ilka",    "Joren",   "Kesta",   "Lorin",
    "Mirren",  "Nadia",   "Oskar",   "Perrin",
};
constexpr size_t kBotNameCount = sizeof(kBotNames) / sizeof(kBotNames[0]);

bool humanRecipient(uint64_t guid) {
    return guid && (guid & 0xffff000000000000ULL) != kLocalBotGuidPrefix &&
        !LocalBotDirector::isMarketSeller(guid);
}

float distanceSq(const LocalRealmPlayer& a, float x, float y, float z) {
    const float dx = a.x - x, dy = a.y - y, dz = a.z - z;
    return dx * dx + dy * dy + dz * dz;
}

/// Total of an item across a player's bags.
uint16_t carried(const LocalRealmPlayer& player, uint32_t itemId) {
    uint32_t total = 0;
    for (const auto& stack : player.inventory) {
        if (stack.itemId == itemId) total += stack.count;
    }
    return static_cast<uint16_t>(std::min<uint32_t>(total, 65535));
}

/// Take `count` of an item out of a player's bags. Returns what was actually
/// removed, which is what a caller must credit rather than what it asked for.
uint16_t removeCarried(LocalRealmPlayer& player, uint32_t itemId, uint16_t count) {
    normalizeLocalInventory(player);
    uint16_t removed = 0;
    for (auto it = player.inventory.begin(); it != player.inventory.end() && removed < count;) {
        if (it->itemId != itemId) { ++it; continue; }
        const uint16_t take = static_cast<uint16_t>(std::min<uint32_t>(count - removed, it->count));
        it->count = static_cast<uint16_t>(it->count - take);
        removed = static_cast<uint16_t>(removed + take);
        if (it->count == 0) it = player.inventory.erase(it);
        else ++it;
    }
    return removed;
}

/// Put items into a player's bags, respecting the stack size and the bag
/// limit. Returns false and changes nothing when they will not fit - a partial
/// delivery would take the buyer's gold for half an order.
bool giveCarried(LocalRealmPlayer& player, const LocalItemDefinition& item, uint16_t count) {
    auto candidate = player.inventory;
    const auto layout=localInventoryLayout(player);for(size_t i=0;i<candidate.size();++i)candidate[i].bagSlot=layout[i];
    uint16_t remaining = count;
    const uint16_t stackSize = std::max<uint16_t>(1, item.stack);
    for (auto& stack : candidate) {
        if (remaining == 0) break;
        if (stack.itemId != item.id || stack.count >= stackSize) continue;
        const uint16_t room = static_cast<uint16_t>(stackSize - stack.count);
        const uint16_t put = std::min(room, remaining);
        stack.count = static_cast<uint16_t>(stack.count + put);
        remaining = static_cast<uint16_t>(remaining - put);
    }
    while (remaining > 0) {
        if (candidate.size() >= LocalGameplay::MaxInventory) return false;
        const uint16_t put = std::min(stackSize, remaining);
        candidate.push_back({item.id, put});
        remaining = static_cast<uint16_t>(remaining - put);
    }
    player.inventory = std::move(candidate);
    normalizeLocalInventory(player);
    return true;
}

} // namespace

// --- Pricing ---------------------------------------------------------------

uint32_t LocalAuctionPricing::rarityPremiumBasisPoints(uint16_t chance) {
    if (!chance || chance >= 10000) return 10000;
    // A bounded logarithmic premium: halving a documented drop probability
    // adds half the base price. Unknown reference/group probabilities are not
    // substituted with invented global rates.
    return uint32_t(std::min(80000.0, 10000.0 + 5000.0 * std::log2(10000.0 / chance)));
}

uint32_t LocalAuctionPricing::buyoutFor(const LocalItemDefinition& item, uint16_t count,
                                        float variation, uint32_t multiplier) {
    if (!count) return 0;
    const auto* metadata = localAuctionMetadata(item.id);
    if (metadata && metadata->has(LocalAuctionItemMetadata::TcgMount)) return MoneyCap;
    uint64_t unit = metadata ? metadata->sellPrice : item.value;
    if (!unit && metadata && metadata->has(LocalAuctionItemMetadata::Material))
        unit = std::max<uint64_t>(100u,metadata->buyPrice / 4u);
    if (metadata && metadata->has(LocalAuctionItemMetadata::Mount))
        unit = std::max<uint64_t>({unit, metadata->buyPrice / 4u, 100000u});
    if (!unit) return 0; // unsellable/quest items must not acquire a fabricated vendor value
    constexpr uint32_t qualityBp[] = {10000, 10000, 15000, 30000, 60000, 100000, 100000};
    const auto quality = metadata ? std::min<unsigned>(metadata->quality, 6) : 1u;
    uint64_t base = unit * uint64_t(count) * std::clamp(multiplier, MinMultiplier, MaxMultiplier);
    // Clamp between factors, keeping every multiplication within uint64_t.
    base = std::min<uint64_t>(base, MoneyCap);
    base = std::min<uint64_t>(base * qualityBp[quality] / 10000u, MoneyCap);
    base = std::min<uint64_t>(base * rarityPremiumBasisPoints(metadata ? metadata->dropChanceBp : 0) / 10000u, MoneyCap);
    if (metadata && metadata->has(LocalAuctionItemMetadata::Mount))
        base = std::min<uint64_t>(base * 25u, MoneyCap);
    if (base >= MoneyCap) return MoneyCap;
    if (!std::isfinite(variation)) variation = .5f;
    // Two percent, capped at two gold per stack: cheap materials move by
    // copper/silver, never by an arbitrary multi-gold surcharge.
    const int64_t spread = static_cast<int64_t>(std::max<uint64_t>(1, std::min<uint64_t>(base / 50u, 20000u)));
    const int64_t shift = static_cast<int64_t>((std::clamp(variation, 0.f, 1.f) * 2.0 - 1.0) * double(spread));
    return uint32_t(std::clamp<int64_t>(int64_t(base) + shift, 1, MoneyCap));
}

uint32_t LocalAuctionPricing::bidFor(uint32_t buyout) {
    if (buyout == MoneyCap) return MoneyCap; // cap-priced collectibles do not auction below the cap
    const uint32_t bid = uint32_t(uint64_t(buyout) * 3u / 4u);
    return std::max<uint32_t>(bid, 1);
}

// --- Director --------------------------------------------------------------

uint32_t LocalBotDirector::nextRandom(uint32_t& state) {
    // xorshift32. Reproducible and cheap; this drives wandering and price
    // variation, not anything that needs statistical quality.
    if (state == 0) state = 0x9e3779b9u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float LocalBotDirector::randomUnit(uint32_t& state) {
    return static_cast<float>(nextRandom(state) >> 8) / 16777216.0f;
}

uint32_t LocalBotDirector::randomPriceMultiplier(uint32_t& state) {
    constexpr auto choices = LocalAuctionPricing::MaxMultiplier - LocalAuctionPricing::MinMultiplier + 1u;
    return LocalAuctionPricing::MinMultiplier + nextRandom(state) % choices;
}

void LocalBotDirector::setBotCount(size_t count) {
    botCount_ = std::min(count, MaxBots);
}

bool LocalBotDirector::isBot(uint64_t guid) const {
    return (guid & 0xffff000000000000ULL) == kLocalBotGuidPrefix;
}

bool LocalBotDirector::isMarketSeller(uint64_t guid) {
    return (guid & 0xffff000000000000ULL) == 0x0A11000000000000ULL;
}

bool LocalBotDirector::refreshMarket(const LocalWorldContent& content) {
    struct Pools {
        std::vector<const LocalAuctionItemMetadata*> materials, other, drops, tcg;
        Pools() {
            for (const auto& item : kLocalAuctionItems) {
                if (!item.has(LocalAuctionItemMetadata::Supply)) continue;
                auto& pool = item.has(LocalAuctionItemMetadata::TcgMount) ? tcg :
                    item.has(LocalAuctionItemMetadata::DropMount) ? drops :
                    item.has(LocalAuctionItemMetadata::Material) ? materials : other;
                pool.push_back(&item);
            }
        }
    };
    static const Pools pools;
    size_t marketCount = 0, materials = 0, mounts = 0;
    bool changed = false;
    for (const auto& listing : auctions_) {
        if (!isMarketSeller(listing.seller) && !isBot(listing.seller)) continue;
        ++marketCount;
        const auto* data = localAuctionMetadata(listing.itemId);
        if (data && data->has(LocalAuctionItemMetadata::Material)) ++materials;
        if (data && data->has(LocalAuctionItemMetadata::Mount)) ++mounts;
    }
    // Posted prices stay fixed for the life of a listing, including saved
    // listings and auctions with bids. Competition comes from each new seller's
    // independent price, not rerolling the whole board on every refill pass.
    // At most twelve supply candidate reads per restock, never a full MPQ/catalog
    // sweep. The first board fills over several seconds; later updates are one
    // minute apart and leave at least 32 slots for player-owned auctions.
    for (unsigned attempt = 0; attempt < 12 && marketCount < MaxMarketAuctions &&
         auctions_.size() < MaxAuctions && nextAuctionId_ != UINT32_MAX; ++attempt) {
        const bool material = materials < 48 || (nextRandom(marketRandom_) % 2u == 0);
        const std::vector<const LocalAuctionItemMetadata*>* pool = material ? &pools.materials : &pools.other;
        const uint32_t rareRoll = nextRandom(marketRandom_) % 100u;
        if (marketCount >= 48 && mounts < 2 && rareRoll < 3 && !pools.drops.empty()) pool = &pools.drops;
        if (marketCount >= 48 && mounts < 2 && rareRoll == 99 && !pools.tcg.empty()) pool = &pools.tcg;
        if (pool->empty()) continue;
        const auto* data = (*pool)[nextRandom(marketRandom_) % pool->size()];
        // Competing sellers sometimes restock an existing commodity. Limit
        // duplicates so one popular item cannot crowd out the whole market.
        if (data->has(LocalAuctionItemMetadata::Material) && !auctions_.empty() && nextRandom(marketRandom_) % 3u == 0) {
            const auto& rival = auctions_[nextRandom(marketRandom_) % auctions_.size()];
            if (const auto* candidate = localAuctionMetadata(rival.itemId);
                candidate && candidate->has(LocalAuctionItemMetadata::Material) && candidate->has(LocalAuctionItemMetadata::Supply))
                data = candidate;
        }
        if (std::count_if(auctions_.begin(), auctions_.end(), [&](const LocalAuction& a) {return a.itemId == data->id;}) >= 3) continue;
        // Only supported mount spells may enter the market: a flying/vehicle
        // mount must not consume the player's gold while local use is missing.
        if (data->has(LocalAuctionItemMetadata::Mount) && !localMountSupported(content,data->id)) continue;
        const auto* item = content.item(data->id);
        if (!item) continue; // a custom/small catalog need not contain the shipped market data
        LocalAuction listing;
        listing.id = nextAuctionId_;
        listing.itemId = data->id;
        const bool isMaterial = data->has(LocalAuctionItemMetadata::Material);
        listing.count = uint16_t(isMaterial ? std::min<unsigned>(item->stack, 20u) : 1u);
        if (!listing.count) continue;
        // Draw separately in a defined order; sharing the RNG in two function
        // arguments would make their evaluation order compiler-dependent.
        const auto multiplier = randomPriceMultiplier(marketRandom_);
        const auto variation = randomUnit(marketRandom_);
        listing.buyout = LocalAuctionPricing::buyoutFor(*item, listing.count, variation, multiplier);
        if (!listing.buyout) continue;
        listing.bid = LocalAuctionPricing::bidFor(listing.buyout);
        const auto seller = nextRandom(marketRandom_) % kBotNameCount;
        listing.seller = 0x0A11000000000000ULL | (seller + 1u);
        listing.sellerName = kBotNames[seller];
        listing.remainingSeconds = AuctionDurationSeconds;
        auctions_.push_back(std::move(listing));
        ++nextAuctionId_; ++marketCount;
        if (isMaterial) ++materials;
        if (data->has(LocalAuctionItemMetadata::Mount)) ++mounts;
        changed = true;
    }
    // Virtual buyers can buy sensibly priced player listings, so selling is
    // useful even with no other consoles or walking bots online. Their item
    // exits the market and proceeds/refunds use the same durable escrow.
    if (nextRandom(marketRandom_) % 4u == 0) {
        const auto buyerMultiplier = randomPriceMultiplier(marketRandom_);
        for (auto it = auctions_.begin(); it != auctions_.end(); ++it) {
            if (!humanRecipient(it->seller) || !it->buyout || it->remainingSeconds <= 0) continue;
            const size_t needed = 1 + size_t(humanRecipient(it->highestBidder));
            if (needed > MaxDeliveries - deliveries_.size()) continue;
            const auto* item = content.item(it->itemId);
            if (!item) continue;
            const auto fair = LocalAuctionPricing::buyoutFor(*item, it->count, .5f, buyerMultiplier);
            if (!fair || uint64_t(it->buyout) > uint64_t(fair) * 105u / 100u) continue;
            deliveries_.reserve(deliveries_.size() + needed);
            deliveries_.push_back({it->seller, 0, it->buyout, 0});
            if (humanRecipient(it->highestBidder)) deliveries_.push_back({it->highestBidder, 0, it->highestBid, 0});
            auctions_.erase(it); changed = true; break;
        }
    }
    marketTimer_ = marketCount < MaxMarketAuctions && auctions_.size() < MaxAuctions ? 1.f : 60.f;
    return changed;
}

void LocalBotDirector::clear(std::vector<LocalRealmPlayer>& players) {
    players.erase(std::remove_if(players.begin(), players.end(),
                                 [this](const LocalRealmPlayer& p) { return isBot(p.guid); }),
                  players.end());
    bots_.clear();
}

void LocalBotDirector::populate(const LocalWorldContent& content,
                                const LocalRealmPlayer& reference,
                                std::vector<LocalRealmPlayer>& players) {
    if (!enabled_) {
        clear(players);
        return;
    }
    (void)content;
    // A dungeon coordinate is not a valid open-world spawn location.
    if (reference.instanceId || reference.flight.active || reference.dead) return;

    // Trim first: lowering the count must remove bots rather than leave
    // orphans wandering with no state behind them.
    while (bots_.size() > botCount_) {
        const uint64_t guid = bots_.back().guid;
        players.erase(std::remove_if(players.begin(), players.end(),
                                     [guid](const LocalRealmPlayer& p) { return p.guid == guid; }),
                      players.end());
        bots_.pop_back();
    }

    for (size_t i = bots_.size(); i < botCount_; ++i) {
        LocalBotState bot;
        bot.guid = kLocalBotGuidPrefix | (i + 1);
        // Each bot's stream is seeded from the realm seed and its own index, so
        // the roster is the same on every load of the same realm and the bots
        // do not all walk in step.
        bot.randomState = seed_ * 2654435761u + static_cast<uint32_t>(i + 1) * 40503u;
        if (bot.randomState == 0) bot.randomState = 1;

        LocalRealmPlayer player;
        player.guid = bot.guid;
        player.name = kBotNames[i % kBotNameCount];
        player.mapId = reference.mapId;
        player.instanceId = 0;   // bots stay in the open world
        // Scattered around the reference rather than stacked on it.
        const float angle = randomUnit(bot.randomState) * 6.2831853f;
        const float radius = 12.0f + randomUnit(bot.randomState) * 40.0f;
        player.x = reference.x + std::cos(angle) * radius;
        player.y = reference.y + std::sin(angle) * radius;
        player.z = reference.z;
        player.orientation = angle;
        player.race = reference.race;
        player.classId = reference.classId;
        player.gender = static_cast<uint8_t>(nextRandom(bot.randomState) & 1u);
        player.useFemaleModel = player.gender != 0;
        player.skin = static_cast<uint8_t>(nextRandom(bot.randomState) % 8u);
        player.face = static_cast<uint8_t>(nextRandom(bot.randomState) % 8u);
        player.hairStyle = static_cast<uint8_t>(nextRandom(bot.randomState) % 8u);
        player.hairColor = static_cast<uint8_t>(nextRandom(bot.randomState) % 8u);
        player.facialHair = static_cast<uint8_t>(nextRandom(bot.randomState) % 6u);
        // Around the host's own level, so the world they populate is the one
        // the player is actually in rather than a crowd of level ones.
        const int spread = static_cast<int>(nextRandom(bot.randomState) % 5u) - 2;
        player.level = static_cast<uint8_t>(
            std::clamp(static_cast<int>(reference.level) + spread, 1, 80));
        player.introSeen = true;
        player.gameplayInitialized = false;   // the realm gives it stats

        bot.homeX = player.x; bot.homeY = player.y; bot.homeZ = player.z;
        bot.targetX = player.x; bot.targetY = player.y; bot.targetZ = player.z;
        bot.decisionTimer = randomUnit(bot.randomState) * 6.0f;
        bot.auctionTimer = 20.0f + randomUnit(bot.randomState) * 90.0f;

        bots_.push_back(bot);
        players.push_back(std::move(player));
    }
}

void LocalBotDirector::listFromBot(LocalBotState& bot, LocalRealmPlayer& player,
                                   const LocalWorldContent& content) {
    if (auctions_.size() >= MaxAuctions || nextAuctionId_==UINT32_MAX) return;
    if (player.dead || player.attackTarget || player.castingSpellId || player.flight.active || player.instanceId) return;
    const size_t simulated = size_t(std::count_if(auctions_.begin(), auctions_.end(),
        [](const LocalAuction& a) { return !humanRecipient(a.seller); }));
    if (simulated >= MaxMarketAuctions || player.inventory.empty()) return;

    // Pick one stack the bot is carrying. Only what it actually farmed goes up:
    // a bot listing items it never had is a shop, not a player.
    const size_t index = nextRandom(bot.randomState) % player.inventory.size();
    const auto stack = player.inventory[index];
    const auto* item = content.item(stack.itemId);
    if (!item) return;
    if (const auto* data = localAuctionMetadata(stack.itemId); data &&
        (!data->tradeable() || (data->has(LocalAuctionItemMetadata::Mount) && !localMountSupported(content,stack.itemId)))) return;
    if (!LocalAuctionPricing::buyoutFor(*item, 1, .5f)) return;
    // Equipment the bot is wearing is not for sale.
    for (uint32_t worn : player.equipment) {
        if (worn == stack.itemId) return;
    }

    const uint16_t count = static_cast<uint16_t>(
        std::max<uint16_t>(1, std::min<uint16_t>(stack.count,
            static_cast<uint16_t>(1 + nextRandom(bot.randomState) % 5u))));
    const uint16_t removed = removeCarried(player, stack.itemId, count);
    if (removed == 0) return;

    LocalAuction listing;
    listing.id = nextAuctionId_++;
    listing.itemId = stack.itemId;
    listing.count = removed;
    const auto multiplier = randomPriceMultiplier(bot.randomState);
    const auto variation = randomUnit(bot.randomState);
    listing.buyout = LocalAuctionPricing::buyoutFor(*item, removed, variation, multiplier);
    listing.bid = LocalAuctionPricing::bidFor(listing.buyout);
    listing.seller = player.guid;
    listing.sellerName = player.name;
    listing.remainingSeconds = AuctionDurationSeconds;
    auctions_.push_back(std::move(listing));
}

bool LocalBotDirector::tick(float seconds, const LocalWorldContent& content,
                            std::vector<LocalRealmPlayer>& players) {
    bool changed = false;

    if (!std::isfinite(seconds) || seconds < 0) return false;
    for (auto it = auctions_.begin(); it != auctions_.end();) {
        it->remainingSeconds = std::max(0.0f, it->remainingSeconds - seconds);
        const auto& a = *it;
        const auto recipient = a.highestBidder ? a.highestBidder : a.seller;
        const bool itemDelivery = humanRecipient(recipient);
        const bool proceeds = a.highestBidder && humanRecipient(a.seller);
        const size_t needed = size_t(itemDelivery) + size_t(proceeds);
        if (a.remainingSeconds > 0 || needed > MaxDeliveries - deliveries_.size()) { ++it; continue; }
        deliveries_.reserve(deliveries_.size() + needed);
        if (itemDelivery) deliveries_.push_back({recipient, a.itemId, 0, a.count});
        if (proceeds) deliveries_.push_back({a.seller, 0, a.highestBid, 0});
        it = auctions_.erase(it);
        changed = true;
    }

    marketTimer_ -= seconds;
    if (seconds > 0 && marketTimer_ <= 0) changed = refreshMarket(content) || changed;
    if (!enabled_) return changed;

    for (auto& bot : bots_) {
        auto it = std::find_if(players.begin(), players.end(),
                               [&](const LocalRealmPlayer& p) { return p.guid == bot.guid; });
        if (it == players.end()) continue;
        LocalRealmPlayer& player = *it;

        if (player.dead || player.castingSpellId || player.flight.active || player.instanceId) {
            // Respawn, casting and travel belong to the realm; the bot waits.
            bot.activity = LocalBotActivity::Idle;
            continue;
        }

        // Whether it is fighting is not the bot's decision - it is whatever is
        // attacking it. The realm's NPC AI sets attackTarget on both sides.
        bot.activity = player.attackTarget ? LocalBotActivity::Fighting
                                           : LocalBotActivity::Wandering;

        if (bot.activity == LocalBotActivity::Wandering) {
            bot.decisionTimer -= seconds;
            if (bot.decisionTimer <= 0.0f || distanceSq(player, bot.targetX, bot.targetY,
                                                        bot.targetZ) < 4.0f) {
                // Somewhere else within the home radius. Staying inside it is
                // what keeps a bot in the level range it was created for.
                const float angle = randomUnit(bot.randomState) * 6.2831853f;
                const float radius = randomUnit(bot.randomState) * bot.roamRadius;
                bot.targetX = bot.homeX + std::cos(angle) * radius;
                bot.targetY = bot.homeY + std::sin(angle) * radius;
                bot.targetZ = bot.homeZ;
                bot.decisionTimer = 4.0f + randomUnit(bot.randomState) * 8.0f;
            }

            const float dx = bot.targetX - player.x;
            const float dy = bot.targetY - player.y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length > 0.5f) {
                // Walking pace. Bots are scenery with agency, not racers.
                const float step = std::min(2.5f * seconds, length);
                player.x += dx / length * step;
                player.y += dy / length * step;
                // The realm has no navmesh, so height follows the home plane.
                // A bot that walked off a cliff would be a bot falling forever.
                player.z = bot.homeZ;
                player.orientation = std::atan2(dy, dx);
                ++player.positionRevision;
                changed = true;
            }
        }

        if (player.attackTarget) continue; // never sell inventory during combat
        bot.auctionTimer -= seconds;
        if (bot.auctionTimer <= 0.0f) {
            bot.auctionTimer = 60.0f + randomUnit(bot.randomState) * 180.0f;
            const size_t listings = auctions_.size();
            listFromBot(bot, player, content);
            if (auctions_.size() != listings) changed = true;
        }
    }
    return changed;
}

bool LocalBotDirector::listItem(LocalRealmPlayer& seller, uint32_t itemId, uint16_t count,
                                const LocalWorldContent& content, std::string& result) {
    const auto* item = content.item(itemId);
    if (!item) { result = "Unknown item"; return false; }
    const auto price = LocalAuctionPricing::buyoutFor(*item, count, 0.5f);
    return listItemPriced(seller, itemId, count, LocalAuctionPricing::bidFor(price), price,
                         720, content, result);
}

bool LocalBotDirector::listItemPriced(LocalRealmPlayer& seller, uint32_t itemId, uint16_t count,
                                     uint32_t bid, uint32_t buyout, uint32_t durationMinutes,
                                     const LocalWorldContent& content, std::string& result) {
    if (auctions_.size() >= MaxAuctions || nextAuctionId_ == UINT32_MAX) {
        result = "The auction house is full"; return false;
    }
    const auto* item = content.item(itemId);
    if (const auto* metadata = localAuctionMetadata(itemId); metadata && !metadata->tradeable()) {
        result = "Bound and quest items cannot be auctioned"; return false;
    }
    if (const auto* metadata = localAuctionMetadata(itemId); metadata && metadata->has(LocalAuctionItemMetadata::Mount) && !localMountSupported(content,itemId)) {
        result = "Mount auctions require local mount learning and riding support"; return false;
    }
    if (!item || !count || count > item->stack || !bid || bid > 1000000000u ||
        buyout > 1000000000u || (buyout && buyout < bid) ||
        (durationMinutes != 720 && durationMinutes != 1440 && durationMinutes != 2880)) {
        result = "Invalid item, stack, price or auction duration"; return false;
    }
    const uint32_t worn = uint32_t(std::count(seller.equipment.begin(), seller.equipment.end(), itemId));
    if (uint32_t(carried(seller, itemId)) < uint32_t(count) + worn) {
        result = "Not enough unequipped items"; return false;
    }
    LocalRealmPlayer candidate = seller;
    if (removeCarried(candidate, itemId, count) != count) return false;
    LocalAuction a;
    a.id = nextAuctionId_; a.itemId = itemId; a.count = count;
    a.bid = bid; a.buyout = buyout; a.seller = seller.guid; a.sellerName = seller.name;
    a.remainingSeconds = float(durationMinutes) * 60.0f;
    result = "Auction posted: " + item->name;
    auctions_.push_back(std::move(a)); // allocate before moving the seller's inventory
    seller.inventory.swap(candidate.inventory);
    ++nextAuctionId_;
    return true;
}

bool LocalBotDirector::buyout(uint32_t auctionId, LocalRealmPlayer& buyer,
                              const LocalWorldContent& content, std::string& result) {
    auto it = std::find_if(auctions_.begin(), auctions_.end(),
                          [=](const LocalAuction& a) { return a.id == auctionId; });
    if (it == auctions_.end() || it->remainingSeconds <= 0 || !it->buyout) {
        result = "Auction unavailable for buyout"; return false;
    }
    if (it->seller == buyer.guid) { result = "That is your own auction"; return false; }
    const uint32_t held = it->highestBidder == buyer.guid ? it->highestBid : 0;
    const uint32_t due = it->buyout - std::min(it->buyout, held);
    if (buyer.money < due) { result = "You cannot afford that"; return false; }
    const auto* item = content.item(it->itemId);
    if (!item) { result = "Unknown item"; return false; }
    if (const auto* metadata = localAuctionMetadata(it->itemId); metadata && metadata->has(LocalAuctionItemMetadata::Mount) && !localMountSupported(content,it->itemId)) {
        result = "Mount purchases are unavailable until local mount riding is supported"; return false;
    }
    const bool proceeds = humanRecipient(it->seller);
    const bool refund = humanRecipient(it->highestBidder) && it->highestBidder != buyer.guid;
    const size_t needed = 1 + size_t(proceeds) + size_t(refund);
    if (needed > MaxDeliveries - deliveries_.size()) { result = "Auction delivery queue is full"; return false; }
    deliveries_.reserve(deliveries_.size() + needed);
    result = "Bought " + item->name + "; collect your mail at an innkeeper";
    deliveries_.push_back({buyer.guid,it->itemId,0,it->count});
    if (proceeds) deliveries_.push_back({it->seller, 0, it->buyout, 0});
    if (refund)
        deliveries_.push_back({it->highestBidder, 0, it->highestBid, 0});
    buyer.money -= due;
    auctions_.erase(it);
    return true;
}

bool LocalBotDirector::listStacksPriced(LocalRealmPlayer& seller, uint32_t itemId,
        uint16_t count, uint16_t stacks, uint32_t bid, uint32_t buyout, uint32_t durationMinutes,
        const LocalWorldContent& content, std::string& result) {
    if (!stacks || stacks > LocalGameplay::MaxInventory ||
        auctions_.size() + stacks > MaxAuctions || uint64_t(nextAuctionId_) + stacks >= UINT32_MAX) {
        result = "Invalid stack count or insufficient auction space"; return false;
    }
    if (stacks == 1) return listItemPriced(seller,itemId,count,bid,buyout,durationMinutes,content,result);
    // All stacks are accepted and persisted as one command. A failure cannot
    // leave half a requested batch sold or require many synchronous saves.
    LocalBotDirector candidate = *this;
    LocalRealmPlayer inventoryCandidate = seller;
    for (uint16_t i=0;i<stacks;++i)
        if (!candidate.listItemPriced(inventoryCandidate,itemId,count,bid,buyout,durationMinutes,content,result)) return false;
    result = "Posted " + std::to_string(stacks) + " auction stacks";
    *this = std::move(candidate); seller = std::move(inventoryCandidate);
    return true;
}

bool LocalBotDirector::placeBid(uint32_t auctionId, LocalRealmPlayer& bidder,
                                uint32_t amount, std::string& result) {
    auto it = std::find_if(auctions_.begin(), auctions_.end(),
                          [=](const LocalAuction& a) { return a.id == auctionId; });
    if (it == auctions_.end() || it->remainingSeconds <= 0) { result = "That auction has ended"; return false; }
    if (it->seller == bidder.guid) { result = "That is your own auction"; return false; }
    const uint64_t minimum = it->highestBid ? uint64_t(it->highestBid) + std::max(1u, it->highestBid / 20u) : it->bid;
    if (amount < minimum || amount > 1000000000u || (it->buyout && amount >= it->buyout)) {
        result = "Invalid bid; use buyout for the buyout price"; return false;
    }
    const uint32_t held = it->highestBidder == bidder.guid ? it->highestBid : 0;
    if (bidder.money < amount - held) { result = "You cannot afford that bid"; return false; }
    const bool refund = humanRecipient(it->highestBidder) && it->highestBidder != bidder.guid;
    if (refund && deliveries_.size() >= MaxDeliveries) { result = "Auction delivery queue is full"; return false; }
    deliveries_.reserve(deliveries_.size() + size_t(refund));
    result = "Bid placed";
    if (refund)
        deliveries_.push_back({it->highestBidder, 0, it->highestBid, 0});
    bidder.money -= amount - held;
    it->highestBid = amount; it->highestBidder = bidder.guid;
    return true;
}

bool LocalBotDirector::cancelAuction(uint32_t id, LocalRealmPlayer& seller, std::string& result) {
    auto it = std::find_if(auctions_.begin(), auctions_.end(), [=](const LocalAuction& a) {return a.id == id;});
    if (it == auctions_.end() || it->seller != seller.guid || it->highestBidder) {
        result = "Only your own unbid auction can be cancelled"; return false;
    }
    if (deliveries_.size() >= MaxDeliveries) {result = "Auction delivery queue is full"; return false;}
    result = "Auction cancelled; item returned when there is bag space";
    deliveries_.push_back({seller.guid, it->itemId, 0, it->count});
    auctions_.erase(it); return true;
}

bool LocalBotDirector::restoreDeliveries(const std::vector<LocalAuctionDelivery>& entries) {
    if (entries.size() > MaxDeliveries) return false;
    for (const auto& d : entries)
        if (!d.recipient || (!d.itemId && !d.money) || (bool(d.itemId) != bool(d.count)) || d.money > 1000000000u)
            return false;
    deliveries_ = entries; return true;
}

bool LocalBotDirector::deliver(LocalRealmPlayer& player, const LocalWorldContent& content) {
    bool changed = false;
    for (auto it = deliveries_.begin(); it != deliveries_.end();) {
        // Bot auctions are a bounded local economy; retired bot proceeds do
        // not need to keep an offline human's delivery queue occupied.
        if (isBot(it->recipient) || isMarketSeller(it->recipient)) {it = deliveries_.erase(it); changed = true; continue;}
        if (it->recipient != player.guid || player.money > LocalAuctionPricing::MoneyCap) {++it; continue;}
        // Money-only escrow can drain as wallet space becomes available. Keep
        // the exact unpaid remainder; mixed item/money records remain atomic.
        const uint32_t room = LocalAuctionPricing::MoneyCap - player.money;
        if (!it->itemId && it->money > room) {
            if (room) { player.money += room; it->money -= room; changed = true; }
            ++it; continue;
        }
        if (it->money > room) {++it; continue;}
        if (it->itemId) {
            const auto* item = content.item(it->itemId);
            if (!item || !giveCarried(player, *item, it->count)) {++it; continue;}
        }
        player.money += it->money;
        it = deliveries_.erase(it); changed = true;
    }
    return changed;
}

bool LocalBotDirector::restoreAuctionSequence(uint32_t next, std::string& error) {
    if (!next || next < nextAuctionId_) {
        error = "Invalid saved auction sequence"; return false;
    }
    nextAuctionId_ = next;
    error.clear(); return true;
}

bool LocalBotDirector::restoreAuctions(const std::vector<LocalAuction>& auctions,
                                       std::string& error) {
    if (auctions.size() > MaxAuctions) {
        error = "Saved auction count exceeds the limit";
        return false;
    }
    uint32_t next = 1;
    for (const auto& listing : auctions) {
        if (!listing.id || listing.id==UINT32_MAX || !listing.itemId || !listing.count || !listing.seller ||
            listing.sellerName.empty() || listing.sellerName.size()>48 ||
            bool(listing.highestBid)!=bool(listing.highestBidder) ||
            listing.highestBidder==listing.seller || listing.highestBid>1000000000u ||
            (listing.highestBid && (listing.highestBid<listing.bid || (listing.buyout && listing.highestBid>=listing.buyout)))) {
            error = "Invalid saved auction";
            return false;
        }
        if (!listing.bid || listing.bid > 1000000000u || listing.buyout > 1000000000u || (listing.buyout && listing.bid > listing.buyout)) {
            error = "Invalid saved auction price";
            return false;
        }
        if (!std::isfinite(listing.remainingSeconds) || listing.remainingSeconds < 0.0f ||
            listing.remainingSeconds > 172800.0f) {
            error = "Invalid saved auction duration";
            return false;
        }
        for (const auto& other : auctions) {
            if (&other != &listing && other.id == listing.id) {
                error = "Duplicate saved auction id";
                return false;
            }
        }
        next = std::max(next, listing.id + 1);
    }
    auctions_ = auctions;
    nextAuctionId_ = next;
    error.clear();
    return true;
}

} // namespace wowee::game
