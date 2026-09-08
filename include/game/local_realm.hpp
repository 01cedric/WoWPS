#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "game/local_bots.hpp"
#include "game/local_gameplay.hpp"

namespace wowee::game {

// Standalone authority for singleplayer and trusted local LAN sessions.
enum class LocalRealmState { Stopped, SinglePlayer, Hosting, Browsing, Connecting, Connected, Error };

/// A character this console saved in a realm directory: the slot whose
/// identity file owns it, and the player as last saved.
struct LocalSavedCharacter {
    uint8_t slot = 0;
    LocalRealmPlayer player;
    bool online = false;
};

class LocalRealm {
public:
    static constexpr uint16_t DefaultPort = 3725;
    static constexpr size_t MinPlayers = 2;
    static constexpr size_t DefaultPlayers = 8;
    static constexpr size_t MaxPlayers = 100;
    static constexpr uint8_t MaxCharacterSlots = 10;

    // The character screen's view of a save directory, without starting a
    // realm: every slot with an identity file that owns a saved player.
    static std::vector<LocalSavedCharacter> savedCharacters(const std::string& saveDirectory);
    // The lowest slot that owns no saved player, or -1 when all ten are taken.
    static int freeCharacterSlot(const std::string& saveDirectory);
    // Remove a slot's player from the realm save and forget its identity.
    static bool deleteSavedCharacter(const std::string& saveDirectory, uint8_t slot);
    LocalRealm();
    ~LocalRealm();
    LocalRealm(const LocalRealm&) = delete;
    LocalRealm& operator=(const LocalRealm&) = delete;

    // A host and singleplayer share their saved realm. Each joining console keeps
    // its identity in profileDirectory; its character is saved on the host.
    bool loadContent(const std::string& path);
    bool loadCatalog(const std::string& directory);
    bool setStarterSpells(const std::vector<LocalSpellDefinition>& spells, const std::string& diagnostic);
    bool setAreaTriggers(const std::vector<LocalAreaTriggerVolume>& volumes);
    bool setCharacterSlot(uint8_t slot);
    bool setFactionTemplates(const std::vector<LocalFactionTemplate>& rows,
                             const std::array<uint32_t, 12>& raceTemplates);
    bool setCharacterOptions(uint8_t race, uint8_t classId, uint8_t gender);
    // Appearance for a character this start creates (an existing slot keeps its own).
    bool setCharacterAppearance(uint8_t skin, uint8_t face, uint8_t hairStyle, uint8_t hairColor,
                                uint8_t facialHair, bool useFemaleModel);
    bool setRealmName(const std::string& name);
    /// Run playerbots on this realm. Must be set before it starts: the roster
    /// is built with the world, and bots that appeared mid-session would be
    /// characters nobody logged in.
    void setPlayerbots(bool enabled, size_t count = 6);
    [[nodiscard]] bool playerbotsEnabled() const;
    // Host configuration; guests receive authoritative listing prices.
    void setAuctionPriceMultiplier(uint32_t multiplier);
    [[nodiscard]] uint32_t auctionPriceMultiplier() const;
    /// The auction house, as the interface needs to read it.
    const std::vector<LocalAuction>& auctions() const;
    /// Buy, bid and list. Host-authoritative like every other action here.
    bool buyoutAuction(uint32_t auctionId, uint64_t npcGuid = 0);
    bool bidAuction(uint32_t auctionId, uint32_t amount, uint64_t npcGuid = 0);
    bool listAuction(uint32_t itemId, uint16_t count);
    bool listAuction(uint32_t itemId, uint16_t count, uint32_t bid, uint32_t buyout, uint32_t minutes);
    bool listAuctionStacks(uint32_t itemId, uint16_t count, uint16_t stacks,
                           uint32_t bid, uint32_t buyout, uint32_t minutes, uint64_t npcGuid = 0);
    bool cancelAuction(uint32_t auctionId, uint64_t npcGuid = 0);
    bool startSinglePlayer(const std::string& saveDirectory, const std::string& name);
    bool startHost(const std::string& saveDirectory, const std::string& name,
                   uint16_t port = DefaultPort, size_t playerLimit = DefaultPlayers);
    bool joinHost(const std::string& ipv4, const std::string& profileDirectory,
                  const std::string& name, uint16_t port = DefaultPort);
    // Lobby credentials remain local; all character records and progress stay on
    // the selected host. Browsing never spawns an avatar or reserves a player slot.
    bool browseHost(const std::string& ipv4, const std::string& profileDirectory,
                    uint16_t port = DefaultPort, uint64_t expectedRealmId = 0);
    const std::vector<LocalSavedCharacter>& remoteCharacters() const;
    bool characterListReady() const;
    bool characterOperationPending() const;
    uint64_t characterListRevision() const;
    bool refreshRemoteCharacters();
    bool createRemoteCharacter(uint8_t slot, const std::string& name);
    bool deleteRemoteCharacter(uint8_t slot, uint64_t guid);
    bool connectCharacter(uint8_t slot);
    void setWorldLoading(bool loading);
    // Pump from the main thread, including while connecting/loading. No worker
    // thread, blocking receive, DNS lookup, or unbounded per-frame packet loop.
    void update(float deltaTime);
    /// Report where this console's character is and how it is moving. The
    /// movement bits are kLocalMovementFalling/kLocalMovementInLiquid, taken
    /// from the client's own collision and water - the authority has neither -
    /// and are what let the host measure this character's falls. Omitting them
    /// reports a character that is never in the air, which takes no fall damage.
    bool setLocalPosition(uint32_t mapId, float x, float y, float z, float orientation,
                          uint8_t movement = 0);
    bool setLocalTransportOffset(uint32_t entry, float x, float y, float z, float heading);
    bool attack(uint64_t targetGuid);
    bool stopAttack();
    bool castSpell(uint32_t spellId, uint64_t targetGuid);
    bool cancelCast();
    // Complete or skip this owner's first-world intro; persists with the hero.
    bool completeIntro();
    bool acceptQuest(uint32_t questId, uint64_t npcGuid);
    bool turnInQuest(uint32_t questId, uint64_t npcGuid);
    bool abandonQuest(uint32_t questId);
    bool loot(uint64_t npcGuid);
    bool equipItem(uint32_t itemId, uint8_t slot = 255);
    bool unequipItem(uint8_t slot);
    bool useItem(uint32_t itemId);
    bool dismount();
    bool respawn();
    bool interact(uint64_t npcGuid);
    bool enterPortal(uint32_t portalId, bool privateInstance = false);
    bool leaveInstance();
    std::vector<LocalRealmPortal> availablePortals() const;

    // --- Instances ----------------------------------------------------------
    /// Install the client's own Map.dbc rows, so a dungeon or raid the world
    /// catalog never carried is still recognised as one. Additive: without them
    /// the catalog decides exactly as it did before.
    bool setClientMaps(std::vector<LocalMapDefinition> maps);
    const LocalMapDefinition* clientMap(uint32_t mapId) const;
    /// Which instances this realm currently holds, so the interface can show a
    /// character what they are saved to.
    const std::vector<LocalInstanceState>& instances() const;

    // --- Merchants, repair and trainers -------------------------------------
    /// Install the client's SkillLine.dbc rows. Only professions and secondary
    /// skills are kept; without them the documented built-in fourteen stand.
    bool setSkillLines(const std::vector<LocalSkillLine>& lines);
    const std::vector<LocalSkillLine>& skillLines() const;
    /// The merchant, blacksmith, trainer or innkeeper the player is standing
    /// at, or nullptr. Each is the same eight-yard reach as any conversation.
    const LocalRealmNpc* nearbyVendor(uint64_t npcGuid = 0) const;
    const LocalRealmNpc* nearbyRepairer(uint64_t npcGuid = 0) const;
    const LocalRealmNpc* nearbyClassTrainer() const;
    const LocalRealmNpc* nearbyProfessionTrainer() const;
    const LocalRealmNpc* nearbyInnkeeper() const;
    /// What that merchant sells, and what a stack costs either way. Prices come
    /// from the item's own catalog value; see local_services.hpp.
    std::vector<uint32_t> vendorStock(uint64_t npcGuid = 0) const;
    int32_t vendorRemaining(uint32_t itemId, uint64_t npcGuid = 0) const;
    uint32_t vendorBuyPrice(uint32_t itemId, uint16_t count) const;
    uint32_t vendorSellPrice(uint32_t itemId, uint16_t count) const;
    /// Abilities the nearby class trainer can teach this character right now.
    std::vector<uint32_t> trainableSpells() const;
    /// Host-authoritative like every other action here: each sends a command
    /// and the authority finds the NPC the player is standing at.
    bool sellToVendor(uint32_t itemId, uint16_t count, uint64_t npcGuid = 0);
    bool buyFromVendor(uint32_t itemId, uint16_t count, uint64_t npcGuid = 0);
    bool repairEquipment(uint64_t npcGuid = 0);
    bool learnSpell(uint32_t spellId);
    bool learnProfession(uint32_t skillId);
    bool trainProfessionRank(uint32_t skillId);
    bool setHome();
    bool returnHome();

    // --- Travel -----------------------------------------------------------
    /// Install the taxi rows read from the player's client DBCs. Without them
    /// there are no flights and no transports - not invented ones.
    bool setTravelNetwork(std::vector<LocalTaxiNode> nodes,
                          std::vector<LocalTaxiPath> paths,
                          std::vector<LocalTaxiWaypoint> waypoints);
    const LocalTravelNetwork& travel() const;
    /// Where every transport is, as of the last authority tick. A LAN guest
    /// gets these from the same deterministic schedule rather than over the
    /// wire, so a hull never lags the host by a round trip.
    const std::vector<LocalTransportState>& transports() const;
    /// The flight master the player is standing at, or nullptr.
    const LocalRealmNpc* nearbyFlightMaster() const;
    /// Nodes that flight master can sell a flight to, already filtered by what
    /// this character has discovered.
    std::vector<uint32_t> flightDestinations() const;
    /// Buy and board a flight to a node. The host validates it either way.
    bool takeFlight(uint32_t destinationNode);
    /// Board or leave a transport the player is standing on.
    bool boardTransport(uint32_t transportEntry);
    bool leaveTransport();
    std::vector<LocalQuestDefinition> questsForNpc(uint32_t entry) const;
    const std::vector<LocalRealmNpc>& npcs() const;
    const LocalWorldContent& content() const;
    const std::string& actionStatus() const;
    uint64_t actionStatusRevision() const;
    bool save();
    void stop();

    LocalRealmState state() const;
    bool ready() const;
    bool isHost() const;
    const LocalRealmPlayer* localPlayer() const;
    const std::vector<LocalRealmPlayer>& players() const;
    const std::string& error() const;
    const std::string& status() const;
    uint16_t port() const;
    size_t playerLimit() const; // Total connected consoles, including the host.
    float worldTimeHours() const; // Host-synchronized, advances between snapshots.

private:
    bool command(const LocalRealmCommand& command);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wowee::game
