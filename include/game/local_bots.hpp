#pragma once

// Optional walking playerbots and an independent, bounded local auction board.
// Market sellers are records only; turning playerbots off never closes the
// auction service, removes player listings or stops escrow settlement.

#include "game/local_gameplay.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace wowee::game {

/// One listing on the local auction house.
struct LocalAuction {
    uint32_t id = 0;
    uint32_t itemId = 0;
    uint16_t count = 1;
    /// Copper. buyout is what the item sells for outright; bid is where the
    /// bidding starts. Cap-priced collectibles have the same bid and buyout.
    uint32_t bid = 0;
    uint32_t buyout = 0;
    /// Whose listing this is. A bot's guid, or a player's when they list one.
    uint64_t seller = 0;
    std::string sellerName;
    /// Seconds left before the listing expires and is removed.
    float remainingSeconds = 0;
    /// The highest bid so far and who made it; zero when nobody has bid.
    uint32_t highestBid = 0;
    uint64_t highestBidder = 0;
};

// Durable escrow deliveries. A full bag or offline owner keeps its delivery
// pending; removing an auction must never destroy its item or its bid money.
struct LocalAuctionDelivery {
    uint64_t recipient = 0;
    uint32_t itemId = 0, money = 0;
    uint16_t count = 0;
};

/// What a bot is currently doing. Deliberately few states: a bot that looks
/// busy without being understandable is worse than one that plainly wanders.
enum class LocalBotActivity : uint8_t {
    Idle = 0,
    Wandering,
    Fighting,
    Selling,     ///< walking a listing to the auction house
};

/// A bot's own state, beside the LocalRealmPlayer that represents it.
struct LocalBotState {
    uint64_t guid = 0;
    LocalBotActivity activity = LocalBotActivity::Idle;
    /// Where it started, and how far it is allowed to stray. A bot that walks
    /// out of its level range would be a bot dying in a ditch.
    float homeX = 0, homeY = 0, homeZ = 0;
    float roamRadius = 45.0f;
    /// Where it is heading right now.
    float targetX = 0, targetY = 0, targetZ = 0;
    float decisionTimer = 0;
    /// Seconds until this bot next considers listing something.
    float auctionTimer = 0;
    /// Deterministic per-bot stream, so a host replays the same wandering after
    /// a reload rather than resimulating a different world.
    uint32_t randomState = 0;
};

/// Overflow-safe local market prices in copper. Each new simulated seller
/// draws its own 4..10 multiplier, then applies rarity and a small price spread.
class LocalAuctionPricing {
public:
    static constexpr uint32_t MoneyCap = 1000000000u;
    static constexpr uint32_t MinMultiplier = 4;
    static constexpr uint32_t MaxMultiplier = 10;
    /// The existing suggested quote for the player's posting form. Simulated
    /// sellers always supply an independently drawn multiplier instead.
    static constexpr uint32_t ReferenceMultiplier = 10;
    static uint32_t buyoutFor(const LocalItemDefinition& item, uint16_t count,
                             float variation, uint32_t multiplier = ReferenceMultiplier);
    static uint32_t bidFor(uint32_t buyout);
    static uint32_t rarityPremiumBasisPoints(uint16_t knownDropChanceBp);
};

/// Player simulation remains optional; the auction service always advances.
class LocalBotDirector {
public:
    /// How many bots a realm runs. Bounded like every other list in the local
    /// simulation - these are simulated players, and each one costs what a
    /// player costs.
    static constexpr size_t MaxBots = 16;
    static constexpr size_t MaxAuctions = 256;
    static constexpr size_t MaxMarketAuctions = 224; // 32 places reserved for players
    static constexpr size_t MaxDeliveries = 1024;
    static bool isMarketSeller(uint64_t guid);
    /// How long a listing stands before it expires, in seconds. Short compared
    /// to retail's twelve hours: a standalone session is not twelve hours long,
    /// and a board that never turns over is a board nobody watches.
    static constexpr float AuctionDurationSeconds = 1800.0f;

    LocalBotDirector() = default;

    /// Turn bots on or off. Off is the default and removes every bot; this is
    /// the checkbox the host screen offers before a realm starts.
    void setEnabled(bool enabled) { enabled_ = enabled; }
    [[nodiscard]] bool enabled() const { return enabled_; }

    /// How many bots to run when enabled.
    void setBotCount(size_t count);
    [[nodiscard]] size_t botCount() const { return botCount_; }

    /// Deterministic seed. A realm replays the same bots from the same seed,
    /// so a reload does not produce a different set of characters.
    void setSeed(uint32_t seed) { seed_ = seed ? seed : 1u; marketRandom_ = seed_ ^ 0x74db9103u; }

    /// Create the bot roster around a starting point. Existing bots are kept:
    /// this is called whenever the realm starts, including after a reload.
    void populate(const LocalWorldContent& content, const LocalRealmPlayer& reference,
                  std::vector<LocalRealmPlayer>& players);

    /// Remove every bot from the roster. Called when bots are switched off.
    void clear(std::vector<LocalRealmPlayer>& players);

    /// One simulation step. `players` is the full roster including the bots.
    /// Returns true when something changed that is worth replicating.
    bool tick(float seconds, const LocalWorldContent& content,
              std::vector<LocalRealmPlayer>& players);

    [[nodiscard]] const std::vector<LocalBotState>& bots() const { return bots_; }
    [[nodiscard]] const std::vector<LocalAuction>& auctions() const { return auctions_; }

    /// Is this guid one of ours? The realm uses it to keep a bot out of the
    /// character list and off the LAN player-slot count.
    [[nodiscard]] bool isBot(uint64_t guid) const;

    /// Buy a listing outright. Moves the item to the buyer, the gold to the
    /// seller, and removes the listing. Returns false with a reason when the
    /// buyer cannot afford it, has no room, or the listing is gone.
    bool buyout(uint32_t auctionId, LocalRealmPlayer& buyer,
                const LocalWorldContent& content, std::string& result);

    /// Place a bid; the prior bidder's money is kept in durable refund escrow.
    bool placeBid(uint32_t auctionId, LocalRealmPlayer& bidder, uint32_t amount,
                  std::string& result);

    /// List one stack from a player's own bags.
    bool listItem(LocalRealmPlayer& seller, uint32_t itemId, uint16_t count,
                  const LocalWorldContent& content, std::string& result);

    /// Restore listings from a save. Rejects a malformed set wholesale rather
    /// than importing half of it.
    bool restoreAuctions(const std::vector<LocalAuction>& auctions, std::string& error);
    uint32_t nextAuctionId() const { return nextAuctionId_; }
    bool restoreAuctionSequence(uint32_t next, std::string& error);
    bool listItemPriced(LocalRealmPlayer& seller, uint32_t itemId, uint16_t count,
                        uint32_t bid, uint32_t buyout, uint32_t durationMinutes,
                        const LocalWorldContent& content, std::string& result);
    bool listStacksPriced(LocalRealmPlayer& seller, uint32_t itemId, uint16_t count,
                         uint16_t stacks, uint32_t bid, uint32_t buyout, uint32_t durationMinutes,
                         const LocalWorldContent& content, std::string& result);
    bool cancelAuction(uint32_t id, LocalRealmPlayer& seller, std::string& result);
    const std::vector<LocalAuctionDelivery>& deliveries() const { return deliveries_; }
    bool restoreDeliveries(const std::vector<LocalAuctionDelivery>& deliveries);
    bool deliver(LocalRealmPlayer& player, const LocalWorldContent& content);

private:
    /// Advance one bot's own random stream. A tiny xorshift rather than a
    /// std::mt19937 per bot: this needs to be reproducible and cheap, not
    /// statistically excellent.
    static uint32_t nextRandom(uint32_t& state);
    static float randomUnit(uint32_t& state);
    static uint32_t randomPriceMultiplier(uint32_t& state);

    void listFromBot(LocalBotState& bot, LocalRealmPlayer& player,
                     const LocalWorldContent& content);

    bool refreshMarket(const LocalWorldContent& content);
    uint32_t marketRandom_ = 0x74db9103u;
    float marketTimer_ = 0;
    bool enabled_ = false;
    size_t botCount_ = 6;
    uint32_t seed_ = 1;
    uint32_t nextAuctionId_ = 1;
    std::vector<LocalBotState> bots_;
    std::vector<LocalAuction> auctions_;
    std::vector<LocalAuctionDelivery> deliveries_;
};

/// The guid prefix every bot carries. High enough not to collide with a
/// console's own saved characters, and recognisable in a log.
inline constexpr uint64_t kLocalBotGuidPrefix = 0x0B07000000000000ULL;

} // namespace wowee::game
