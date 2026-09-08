#pragma once

// Local realm services. Merchant offers and prices are generated from the
// same pinned AzerothCore SQL snapshot as the world catalog. They are static,
// bounded tables: SQL is never loaded by the console. No item is inferred from
// an NPC's name, level or general vendor flag. See VENDOR_IMPORT_REPORT.json
// for excluded currency/condition/reputation offers and provenance.

#include "game/local_gameplay.hpp"

#include <cstdint>
#include <map>
#include <utility>
#include <string>
#include <vector>

namespace wowee::game {

// --- Service NPCs ----------------------------------------------------------

/// One row of the transcribed npcflag table. Sorted by entry.
struct LocalServiceNpcRecord {
    uint32_t entry = 0;
    uint32_t npcFlags = 0;
    /// The SkillLine this NPC trains, resolved from its subname, or 0 when the
    /// subname named something this realm does not model (Riding, Pet, Portal).
    uint16_t trainerSkill = 0;
    /// The class this NPC trains, 1-11, or 0.
    uint8_t trainerClass = 0;
};

/// The transcribed record for a creature entry, or nullptr.
const LocalServiceNpcRecord* localServiceNpcRecord(uint32_t entry);

/// The npcflag this realm should act on for a definition: the catalog's own
/// value when it has one, otherwise the transcribed fallback. A catalog that
/// carries the field therefore overrides every row of the built-in table,
/// including a row that disagrees with it.
uint32_t localEffectiveNpcFlags(const LocalNpcDefinition& definition);

// --- Merchants -------------------------------------------------------------

/// Vendor service flags, packed in the existing LAN representation.
enum class LocalVendorCategory : uint8_t {
    General = 0, Food = 1, Ammunition = 2, Reagent = 3, Poison = 4
};

struct LocalVendorOffer {
    uint32_t entry = 0, itemId = 0, maxCount = 0, restockSeconds = 0;
};
struct LocalVendorPrice {
    uint32_t itemId = 0, buyPrice = 0, buyCount = 1, sellPrice = 0;
};

/// Only genuine purchasable rows from this NPC's npc_vendor list. An explicit
/// catalog override remains authoritative; an unknown NPC has an empty shop.
std::vector<uint32_t> localVendorStockForNpc(uint32_t entry,
                                            const std::vector<uint32_t>& catalogStock,
                                            const LocalWorldContent& content);
const LocalVendorOffer* localVendorOffer(uint32_t entry, uint32_t itemId);
const LocalVendorPrice* localVendorPrice(uint32_t itemId);
uint32_t localVendorBuyCount(uint32_t itemId);

/// Service categories only: they identify a merchant, never determine stock.
uint8_t localVendorCategories(uint32_t npcFlags);
uint32_t localVendorBuyPrice(const LocalItemDefinition& item, uint32_t count);
uint32_t localVendorSellPrice(const LocalItemDefinition& item, uint32_t count);

/// Shared authoritative limited stock. Only depleted offers need a record;
/// full records expire after restocking. The cap rejects new depletion instead
/// of silently resetting another merchant's stock to make room. Independent of
/// render/streaming lifetime and of walking player bots.
class LocalVendorInventory {
public:
    static constexpr size_t MaxDepletedOffers = 4096;
    uint32_t available(uint64_t npcGuid, const LocalVendorOffer& offer,
                       uint32_t buyCount, double now) const;
    bool consume(uint64_t npcGuid, const LocalVendorOffer& offer, uint32_t count,
                 uint32_t buyCount, double now);
    void clear() { depleted_.clear(); }
    size_t retainedOffers() const { return depleted_.size(); }
private:
    struct Stock { uint32_t remaining = 0, maximum = 0, bundle = 1, interval = 1; double since = 0; };
    static uint32_t restocked(const Stock& stock, double now);
    std::map<std::pair<uint64_t, uint32_t>, Stock> depleted_;
};

// --- Professions -----------------------------------------------------------

/// The fourteen professions of 3.3.5a, with their SkillLine.dbc ids. Used only
/// until the application installs the client's own SkillLine rows, and checked
/// against them when it does.
const std::vector<LocalSkillLine>& localBuiltinProfessions();

const LocalSkillLine* localProfession(const std::vector<LocalSkillLine>& lines, uint32_t skillId);

/// A trainable rank. The caps are the ones the client's own skill window shows;
/// the character levels are the retail requirements for reaching them. The
/// price is not retail - trainer prices live in npc_trainer, which is server
/// data - it is a documented local schedule that rises with the rank.
struct LocalProfessionRank {
    uint16_t cap = 0;
    uint8_t level = 0;
    uint32_t cost = 0;
    const char* name = "";
};

/// The rank ladder, lowest first. Learning a profession grants the first entry.
const std::vector<LocalProfessionRank>& localProfessionRanks();

/// The rank a character with this cap may buy next, or nullptr when the ladder
/// is finished.
const LocalProfessionRank* localNextProfessionRank(uint16_t cap);

/// Retail's two-primary-profession rule. Secondary skills are unlimited.
inline constexpr size_t kLocalMaxPrimaryProfessions = 2;

/// The chance, in thousandths, that crafting `recipe` at `skill` grants a skill
/// point - the orange/yellow/green/grey curve, taken from the recipe's own
/// SkillLineAbility trivial ranks rather than from a table written here.
///
/// Below TrivialSkillLineRankLow the recipe is orange and always grants;
/// between Low and High it tapers linearly to nothing; at or above High it is
/// grey and grants nothing. That is the shape retail uses.
///
/// The caller accumulates this rather than rolling it, and that is deliberate.
/// Crafting runs on the authority and is replicated, so a roll would work - but
/// accumulating makes a crafting session reproducible, keeps a character
/// reloaded mid-session on exactly the same curve, and needs no per-character
/// random stream in the save file or on the wire. Over any run of crafts it
/// yields precisely the chance above.
uint32_t localCraftSkillChance(const LocalRecipe& recipe, uint16_t skill);
/// What a profession trainer charges to teach a recipe. Like the class
/// trainer's price this is a documented local schedule, not a retail claim:
/// npc_trainer holds the real ones and this console does not have it.
inline constexpr uint32_t kLocalRecipeCostPerSkill = 20;
uint32_t localRecipeCost(const LocalRecipe& recipe);

// --- Class trainers --------------------------------------------------------

/// What a class trainer charges for an ability.
///
/// Trainer prices are npc_trainer rows - server data this console does not
/// have - so this is a documented local schedule rather than a retail claim:
/// one silver per level of the ability being taught, which keeps an early
/// ability affordable to the character who has just earned it and a late one
/// worth saving for.
inline constexpr uint32_t kLocalTrainerCostPerLevel = 100;
uint32_t localTrainerSpellCost(const LocalSpellDefinition& spell);

} // namespace wowee::game
