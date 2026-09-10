#pragma once
#include "core/intro_stream_warmup.hpp"
#include "core/retained_guid_set.hpp"

#include "core/window.hpp"
#include "ui/unit_portrait.hpp"
#include "ui/local_status_notice.hpp"
#include "addons/local_framexml.hpp"
#include "ui/widget_renderer.hpp"
#include "core/input.hpp"
#include "core/entity_spawner.hpp"
#include "core/appearance_composer.hpp"
#include "core/world_loader.hpp"
#include "game/character.hpp"
#include "game/world_packets.hpp"
#include "game/game_services.hpp"
#include "pipeline/blp_loader.hpp"
#include <memory>
#include <chrono>
#include <map>
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <optional>
#include <future>
#include <mutex>
#include <thread>
#include <atomic>

namespace wowee {

// Forward declarations
namespace rendering { class Renderer; }
namespace ui { class UIManager; }
namespace auth { class AuthHandler; }
namespace game { class GameHandler; class World; class ExpansionRegistry; class LocalRealm; struct LocalSpellImport; struct ExpansionProfile; }
namespace pipeline { class AssetManager; class DBCLayout; struct M2Model; struct WMOModel; }
namespace audio { enum class VoiceType; class AudioCoordinator; }
namespace addons { class AddonManager; }

namespace core {

// Handler forward declarations
class NPCInteractionCallbackHandler;
class AudioCallbackHandler;
class EntitySpawnCallbackHandler;
class AnimationCallbackHandler;
class TransportCallbackHandler;
class WorldEntryCallbackHandler;
class UIScreenCallbackHandler;
class CharacterIntro;

enum class AppState {
    AUTHENTICATION,
    REALM_SELECTION,
    CHARACTER_CREATION,
    CHARACTER_SELECTION,
    IN_GAME,
    DISCONNECTED
};

class Application {
    friend class WorldLoader;

public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool initialize();
    void run();
    void shutdown();

    /// Tell the stall watchdog we are alive. Call from long synchronous work that
    /// keeps presenting frames itself (e.g. the world load) so it is not mistaken
    /// for a hung main loop.
    void beatWatchdog();

    // State management
    [[nodiscard]] AppState getState() const { return state; }
    void setState(AppState newState);
    /// The world connection dropped: tear the world down and say so.
    void handleWorldDisconnect();

    // Accessors
    Window* getWindow() { return window.get(); }
    rendering::Renderer* getRenderer() { return renderer.get(); }
    ui::UIManager* getUIManager() { return uiManager.get(); }
    auth::AuthHandler* getAuthHandler() { return authHandler.get(); }
    game::GameHandler* getGameHandler() { return gameHandler.get(); }
    game::World* getWorld() { return world.get(); }
    pipeline::AssetManager* getAssetManager() { return assetManager.get(); }
    addons::AddonManager* getAddonManager() { return addonManager_.get(); }
    game::ExpansionRegistry* getExpansionRegistry() { return expansionRegistry_.get(); }
    pipeline::DBCLayout* getDBCLayout() { return dbcLayout_.get(); }
    bool setAssetExpansionOverride(const std::string& id);
    [[nodiscard]] const std::string& getAssetExpansionOverride() const { return assetExpansionOverrideId_; }
    void reloadExpansionData();
    /// The opcode table, update fields, packet parsers and DBC layouts a
    /// protocol profile brings with it. Loaded at startup and on every
    /// expansion change, which used to be two copies.
    void loadExpansionTables(const game::ExpansionProfile& profile); // Reload DBC layouts, opcodes, etc. after expansion change

    // Singleton access
    static Application& getInstance() { return *instance; }



    // Logout to login screen
    void logoutToLogin();
    // 1=single player, 2=LAN host, 3=LAN join; deferred out of the UI render pass.
    void requestLocalRealm(int mode, const std::string& name, const std::string& host,
                           uint8_t gender = 0, uint8_t race = 1, uint8_t classId = 1,
                           uint8_t characterSlot = 0, const std::string& realmName = "LAN Realm", uint16_t realmPort = 3725);
    bool localRealmBusy() const;
    /// Whether the next local realm runs playerbots. Set from the host screen
    /// before the realm starts; the realm reads it once, because bots are part
    /// of a world rather than a setting to flip inside one.
    void setLocalPlayerbotsEnabled(bool enabled) { localPlayerbotsEnabled_ = enabled; }
    [[nodiscard]] bool localPlayerbotsEnabled() const { return localPlayerbotsEnabled_; }
    void cancelLocalRealm();

    // Single player and LAN host go through the character screens, as the
    // client does: the console's saved characters are listed, one is chosen
    // or a new one created, and the realm starts for that character's slot.
    // mode: 1=single player, 2=LAN host.
    void beginLocalCharacterFlow(int mode, size_t playerLimit = 8, const std::string& realmName = "LAN Realm");
    [[nodiscard]] bool localCharacterFlowActive() const { return localCharacterFlow_; }
    void beginLanCharacterFlow(const std::string& host, const std::string& realmName, uint16_t port, uint64_t realmId);
    [[nodiscard]] bool lanCharacterFlowActive() const { return localCharacterFlow_ && localCharacterMode_ == 3; }
    void refreshLanCharacters();
    void enterLocalCharacter(uint64_t guid);
    void createLocalCharacter(const game::CharCreateData& data);
    void deleteLocalCharacter(uint64_t guid);
    void leaveLocalCharacterFlow();
    void refreshLocalCharacterList();

    // Render bounds lookup (for click targeting / selection) - delegates to EntitySpawner
    bool getRenderBoundsForGuid(uint64_t guid, glm::vec3& outCenter, float& outRadius) const;
    bool getRenderFootZForGuid(uint64_t guid, float& outFootZ) const;
    bool getRenderPositionForGuid(uint64_t guid, glm::vec3& outPos) const;

    // Character skin composite state - delegated to AppearanceComposer
    [[nodiscard]] const std::string& getBodySkinPath() const { return appearanceComposer_ ? appearanceComposer_->getBodySkinPath() : emptyString_; }
    [[nodiscard]] const std::vector<std::string>& getUnderwearPaths() const { return appearanceComposer_ ? appearanceComposer_->getUnderwearPaths() : emptyStringVec_; }
    [[nodiscard]] uint32_t getSkinTextureSlotIndex() const { return appearanceComposer_ ? appearanceComposer_->getSkinTextureSlotIndex() : 0; }
    [[nodiscard]] uint32_t getCloakTextureSlotIndex() const { return appearanceComposer_ ? appearanceComposer_->getCloakTextureSlotIndex() : 0; }
    [[nodiscard]] uint32_t getGryphonDisplayId() const { return entitySpawner_ ? entitySpawner_->getGryphonDisplayId() : 0; }
    [[nodiscard]] uint32_t getWyvernDisplayId() const { return entitySpawner_ ? entitySpawner_->getWyvernDisplayId() : 0; }

    // Entity spawner access
    EntitySpawner* getEntitySpawner() { return entitySpawner_.get(); }

    // Appearance composer access
    AppearanceComposer* getAppearanceComposer() { return appearanceComposer_.get(); }

    // World loader access
    WorldLoader* getWorldLoader() { return worldLoader_.get(); }

    // Audio coordinator access
    audio::AudioCoordinator* getAudioCoordinator() { return audioCoordinator_.get(); }

private:
    void update(float deltaTime);

    /// One frame of being in the world - the largest arm of update()'s state
    /// switch. updateCheckpoint travels by reference because the caller's catch
    /// reports it: an exception here has to say where here was.
    void updateInGame(float deltaTime, const char*& updateCheckpoint);

    /// Everything the server says about how the player moves - speeds, rooting,
    /// gravity, feather fall, water walking - handed to the camera that owns it.
    void applyServerMovementState(float deltaTime);

    /// Keep render instances on top of what the server says. A model is placed
    /// once at spawn and would otherwise stay there while its target circle
    /// follows the entity, which reads as a ring sliding off a still NPC.
    void syncRenderInstancesToEntities(float deltaTime);
    void render();
    void performLogoutToLogin();
    bool rebuildSessionRenderer();
    bool shutdownStarted_ = false;
    void processDeferredLogoutToLogin();
    void updateLocalRealm(float deltaTime);
    /// Put the local realm's zeppelins and ships on screen, through the same
    /// TransportManager a real server drives in online play.
    void syncLocalRealmTransports(const game::LocalRealmPlayer& self);
    /// Read the taxi network out of the player's own client DBCs. Without it
    /// the local realm has no flight masters and no transports.
    void loadLocalTravelNetwork();
    void stopLocalRealm();
    void renderLocalRealmOverlay();
    /// The service windows a player opens by talking to the character who runs
    /// them: the auction house, a merchant's stock, and a trainer's list. They
    /// live in their own translation unit because the overlay above is already
    /// the whole heads-up display and these are three self-contained panels.
    ///
    /// Each takes the scale the overlay computed rather than working it out
    /// again, so every panel on screen agrees about how big a pixel is.
    void renderLocalAuctionHouse(float scale);
    void renderLocalVendorPanel(float scale);
    void renderLocalTrainerPanel(float scale);
    void updateCharacterIntro(float deltaTime);
    void stopCharacterIntro(bool completed);
    void renderCharacterIntroOverlay();
    [[nodiscard]] bool characterIntroOwnsView() const;
    bool enterLocalRealmPortal(uint32_t portalId, bool privateInstance);
    /// How long the player has been standing in the entrance they are in, and
    /// which one. Walking into a dungeon entrance takes you in, the way it does
    /// in the real client - but not instantly: a trigger volume is wide enough
    /// to clip while running past one, and being teleported for walking near a
    /// door is worse than having to stand still for a moment.
    uint32_t localRealmPortalDwellId_ = 0;
    float localRealmPortalDwell_ = 0.0f;

    /// Take the interface down, because the character it belongs to is leaving.
    ///
    /// The frames hold that character - their bags, their spells, their saved
    /// layout - and whoever logs in next must not find any of it still there.
    /// Beyond the data, the widget tree appends rather than replaces, so an
    /// interface left standing is one the next world entry builds a second
    /// copy on top of: every frame twice over, one sitting exactly on the
    /// other. Clearing addonsLoaded_ without this is what made a second login
    /// in one session come up doubled.
    void unloadInterface();
    /// Records how long one named stage of a frame took.
    ///
    /// The per-stage timings only ever spoke up above 50ms, which says which
    /// stage stalled and nothing about where a frame's time normally goes. On
    /// a phone the whole budget is 33ms, so every stage was silent and the
    /// client was slow for reasons nothing reported. This keeps a running
    /// average and worst case per stage and prints the breakdown periodically.
    void noteStageTime(const char* stage, float milliseconds);
    void reportStageTimes();

    struct StageStat {
        double totalMs = 0.0;
        float worstMs = 0.0f;
        int frames = 0;
    };
    /// WOWEE_FRAME_PROFILE. The breakdown below is reported at info, which the
    /// log a bug report arrives with does not carry - so the one measurement
    /// that answers "where does the frame go" was never in the logs it was
    /// built for. With the flag set it is reported at warning instead.
    bool frameProfileEnabled_ = false;
    std::map<std::string, StageStat> stageStats_;
    std::chrono::steady_clock::time_point stageStatsSince_{};
    int stageStatFrames_ = 0;

    void setupUICallbacks();
    void spawnPlayerCharacter();
    // Re-spawn the in-world player model in place after a live appearance change
    // (barber shop), so the new hair/facial hair shows without a restart.
    void refreshPlayerCharacterModel();
    void buildFactionHostilityMap(uint8_t playerRace);
    void setupTestTransport();  // Test transport boat for development

    static Application* instance;

    game::GameServices gameServices_;
    std::unique_ptr<Window> window;
    std::unique_ptr<rendering::Renderer> renderer;
    std::unique_ptr<ui::UIManager> uiManager;
    std::unique_ptr<auth::AuthHandler> authHandler;
    std::unique_ptr<game::GameHandler> gameHandler;
    std::unique_ptr<game::LocalRealm> localRealm_;
    addons::LocalFrameXml localFrameXml_;
    void prepareLocalFrameXml();
    std::unique_ptr<game::LocalSpellImport> localSpellImport_;
    struct LocalRealmRequest {
        int mode; std::string name; std::string host; uint8_t gender, race, classId, characterSlot;
        // Appearance for a character this start creates, from the create screen.
        uint8_t skin = 0, face = 0, hairStyle = 0, hairColor = 0, facialHair = 0;
        bool useFemaleModel = false;
        // Start the realm only to create and save the character, then return
        // to the character list rather than entering the world.
        bool createOnly = false;
        size_t playerLimit = 8;
        std::string realmName = "LAN Realm";
        uint16_t realmPort = 3725;
        uint64_t expectedRealmId = 0;
    };
    std::optional<LocalRealmRequest> pendingLocalRealm_;
    bool localRealmEntered_ = false;
    std::unique_ptr<CharacterIntro> characterIntro_;
    uint64_t introAttemptedGuid_ = 0;
    uint64_t introEligibilityGuid_ = 0;
    uint32_t introPositionRevision_ = 0;
    bool introReturning_ = false;
    bool introCompleteOnReturn_ = false;
    bool introBuffering_ = false;
    bool introSkipRequested_ = false;
    float introWaitSeconds_ = 0.0f;
    IntroStreamWarmup introWarmup_;
    float introUiElapsed_ = 0.0f;
    float introPendingAdvanceSeconds_ = 0.0f;
    std::chrono::steady_clock::time_point introLastUpdate_{};
    float introSavedFov_ = 45.0f;
    glm::vec3 introSavedCameraPosition_{0.0f};
    glm::vec3 introSpawnPosition_{0.0f};
    size_t introNarrationShot_ = static_cast<size_t>(-1);
    bool introNarrationStarted_ = false;
    float localQuestMarkerRefresh_ = 0.0f;
    // The character screens showing this console's saved characters.
    bool localCharacterFlow_ = false;
    int localCharacterMode_ = 1;
    uint64_t localRosterRevision_ = 0;
    std::string localCreatedName_;
    size_t localHostPlayerLimit_ = 8;
    std::string localHostRealmName_ = "LAN Realm";
    struct LocalSlotCharacter { uint8_t slot; uint64_t guid; std::string name; uint8_t race, classId, gender; };
    std::vector<LocalSlotCharacter> localSlotCharacters_;
    void localRealmStatus(const std::string& message, bool isError);
    RetainedGuidSet localRealmRemoteGuids_;
    RetainedGuidSet localRealmNpcGuids_;
    RetainedGuidSet localRealmPresentScratch_;
    RetainedGuidSet localRealmMeleeScratch_;
    std::shared_ptr<game::LocalRealmPlayer> localRealmPresentationSnapshot_;
    /// Transports the local realm currently has on screen, by guid.
    ///
    /// These are the same TransportManager objects a real server drives in
    /// online play: the local realm works out where each hull is and pushes it
    /// through updateServerTransport, exactly as the packet path does. The set
    /// is what tells a hull that has left the player's map to be despawned.
    RetainedGuidSet localRealmTransportGuids_;
    bool localPlayerbotsEnabled_ = false;
    /// Consecutive frames renderer->update has failed to allocate in. Reset by
    /// the first frame that builds; a long enough run gives up rather than
    /// spinning on a client that cannot draw anything.
    unsigned rendererUpdateOomFrames_ = 0;
    unsigned localPresentationOomFrames_ = 0;
    uint32_t localRealmPositionRevision_ = 0;
    uint32_t localRealmInstanceId_ = 0;
    bool localRealmWmoOnly_ = false;
    std::string localRealmTravelNotice_;
    uint64_t localRealmTarget_ = 0;
    std::unordered_map<uint32_t, std::string> localRealmSpellIconPaths_;
    std::unordered_map<uint32_t, VkDescriptorSet> localRealmSpellIconCache_;
    std::unordered_map<std::string, VkDescriptorSet> localRealmUiArt_;
    bool localRealmMenuOpen_ = false;
    bool localRealmPopupOpen_ = false; // Nested popup state before ImGui handles Circle.
    bool localRealmJournalOpen_ = false;
    bool localRealmInventoryOpen_ = false;
    bool localRealmNpcPanelOpen_ = false;
    /// The auction house window, and what is typed into it. The bid box holds
    /// one auction at a time because the window only ever bids on the selected
    /// row - carrying a value per listing would let a stale number be submitted
    /// against a listing the player is no longer looking at.
    bool localRealmAuctionOpen_ = false;
    uint32_t localRealmAuctionSelected_ = 0;
    int localRealmAuctionBid_ = 0;
    int localRealmAuctionTab_ = 0;
    /// The merchant and trainer windows, opened from the conversation with the
    /// character who runs them and closed when the player walks away - the
    /// authority drops the service the moment they leave its eight-yard reach,
    /// so a window that stayed open would offer goods that cannot be bought.
    bool localRealmVendorOpen_ = false;
    bool localRealmTrainerOpen_ = false;
    /// Which item the sell half of the merchant window is asking about, so a
    /// confirmation cannot be answered against a different stack than the one
    /// the player clicked.
    uint32_t localRealmVendorSell_ = 0;
    uint64_t localRealmDialogueNpc_ = 0;
    uint32_t localRealmDialogueQuest_ = 0;
    bool localRealmDialogueFocus_ = true;
    ui::LocalStatusNotice localRealmNotice_;
    std::unique_ptr<game::World> world;
    std::unique_ptr<pipeline::AssetManager> assetManager;
    std::unique_ptr<addons::AddonManager> addonManager_;
    // Set by ReloadUI() from inside Lua, acted on between frames - the reload
    // destroys the state that asked for it.
    bool reloadUiPending_ = false;
    /// Draws the widget tree addons build through CreateFrame/CreateTexture.
    /// Holds the texture cache for Interface\ art, so it lives as long as the app.
    ui::WidgetRenderer widgetRenderer_;
    /// The live head-and-shoulders view the interface's portrait draws.
    ui::UnitPortrait unitPortrait_;
    /// Where the portrait's widget was found last time. Ids are stable, so
    /// this saves a scan of every widget by name on every frame; the name is
    /// still checked, because reloading the interface rebuilds the tree and
    /// the id could then belong to something else.
    uint32_t portraitWidgetId_ = 0;
    /// The FrameXML frame the real minimap is drawn into, when the original
    /// interface owns it. Looked up by name and remembered, the same as the
    /// portrait.
    uint32_t minimapWidgetId_ = 0;
    /// The FrameXML frame the world map is drawn into, when the original
    /// interface owns it. WorldMapDetailFrame rather than WorldMapFrame: the
    /// first is the map area, the second is the panel around it.
    uint32_t worldMapWidgetId_ = 0;
    /// The target's face, on the same terms as the player's. A third offscreen
    /// pass because all three are on screen at once and each holds a different
    /// model - the alternative is reloading a model per frame, which is what a
    /// shared view would amount to.
    ui::UnitPortrait targetPortrait_;
    /// And the focus, which is deliberate enough to be worth its own pass.
    ui::UnitPortrait focusPortrait_;
    /// The dressing room's figure, and its frame. The player plus whatever
    /// has been tried on, which is the paperdoll with an overlay.
    ui::UnitPortrait dressUpModel_;
    uint32_t dressUpWidgetId_ = 0;
    /// The auction house's own dressing room, which is a second frame with a
    /// second try-on list rather than the same one reused.
    ui::UnitPortrait auctionDressUpModel_;
    uint32_t auctionDressUpWidgetId_ = 0;
    /// The pet tab's figure. A creature by display id, like its portrait.
    ui::UnitPortrait petModel_;
    uint32_t petModelWidgetId_ = 0;
    /// The stable's preview, which shows whichever slot the window selected
    /// rather than the pet that is out.
    ui::UnitPortrait stableModel_;
    uint32_t stableModelWidgetId_ = 0;
    /// The mount or critter the companion tab has selected.
    ui::UnitPortrait companionModel_;
    uint32_t companionModelWidgetId_ = 0;
    /// The inspect window's figure, and the frame it is drawn into. Whole
    /// body at the paperdoll's size, because that is what it is: someone
    /// else's paperdoll.
    ui::UnitPortrait inspectModel_;
    uint32_t inspectModelWidgetId_ = 0;
    /// And whoever this client is dealing with - the face in the gossip,
    /// quest, merchant, flight master and trade panels. One view for all of
    /// them because only one such window is open at a time.
    ui::UnitPortrait npcPortrait_;
    /// And the four party members. Affordable now that a portrait is sized for
    /// the circle it is drawn into rather than for the paperdoll: at 160x200 a
    /// party face is a sixteenth of the pixels the paperdoll's target is, and
    /// four of them together cost a quarter of one of it.
    std::array<ui::UnitPortrait, 4> partyPortraits_;
    /// And the pet's, which is always a creature and so always has one.
    ui::UnitPortrait petPortrait_;

    /// The paperdoll's model view, and the frame it is drawn into. A second
    /// offscreen pass rather than a shared one: the portrait shows the face
    /// and this shows the whole figure, and they are on screen together.
    ui::UnitPortrait paperdollModel_;
    /// The facing already applied to the paperdoll, so only the change since
    /// last frame is turned.
    float paperdollFacing_ = 0.0f;
    uint32_t paperdollWidgetId_ = 0;
    bool addonsLoaded_ = false;
    std::unique_ptr<game::ExpansionRegistry> expansionRegistry_;
    // Empty means assets follow the active protocol profile. "legacy" selects
    // the root WOW_DATA_PATH manifest; otherwise this is an expansion id.
    std::string assetExpansionOverrideId_;
    std::unique_ptr<pipeline::DBCLayout> dbcLayout_;
    std::unique_ptr<EntitySpawner> entitySpawner_;
    std::unique_ptr<AppearanceComposer> appearanceComposer_;
    std::unique_ptr<WorldLoader> worldLoader_;
    std::unique_ptr<audio::AudioCoordinator> audioCoordinator_;

    // Callback handlers (extracted from setupUICallbacks)
    std::unique_ptr<NPCInteractionCallbackHandler> npcInteractionCallbacks_;
    std::unique_ptr<AudioCallbackHandler> audioCallbacks_;
    std::unique_ptr<EntitySpawnCallbackHandler> entitySpawnCallbacks_;
    std::unique_ptr<AnimationCallbackHandler> animationCallbacks_;
    std::unique_ptr<TransportCallbackHandler> transportCallbacks_;
    std::unique_ptr<WorldEntryCallbackHandler> worldEntryCallbacks_;
    std::unique_ptr<UIScreenCallbackHandler> uiScreenCallbacks_;

    // Beat by the main loop each iteration; the watchdog treats a long silence as a
    // hang. Long but healthy work that renders its own frames (world load) must beat
    // it too, or the watchdog mistakes the load for a hang.
    std::atomic<int64_t> watchdogHeartbeatMs_{0};

    AppState state = AppState::AUTHENTICATION;
    bool running = false;
    bool renderingFrame_ = false;
    bool logoutToLoginPending_ = false;
    /// Why the player is back at the login screen, when it was not their idea.
    std::string disconnectNotice_;
    bool playerCharacterSpawned = false;
    bool npcsSpawned = false;
    bool spawnSnapToGround = true;
    float lastFrameTime = 0.0f;

    // Player character info (for model spawning)
    game::Race playerRace_ = game::Race::HUMAN;
    game::Gender playerGender_ = game::Gender::MALE;
    game::Class playerClass_ = game::Class::WARRIOR;
    uint64_t spawnedPlayerGuid_ = 0;
    uint32_t spawnedAppearanceBytes_ = 0;
    uint8_t spawnedFacialFeatures_ = 0;

    // Static empty values for null-safe delegation
    static inline const std::string emptyString_;
    static inline const std::vector<std::string> emptyStringVec_;

    float facingSendCooldown_ = 0.0f;        // Rate-limits MSG_MOVE_SET_FACING
    float lastSentCanonicalYaw_ = 1000.0f;   // Sentinel - triggers first send
    bool idleYawned_ = false;

    // M2 transport riding: last frame's locked (canonical) render position, used to
    // detect how far the player tried to walk this frame so that delta can be applied
    // on top of the fixed ride offset instead of either fully locking movement or
    // recomputing the offset from an absolute position (see application.cpp's "M2
    // transport riding" block for why the latter is a no-op identity).
    glm::vec3 lastM2RideLockedCanonical_ = glm::vec3(0.0f);
    bool hasM2RideLock_ = false;
    glm::vec3 lastWMORideLockedRender_ = glm::vec3(0.0f);
    bool hasWMORideLock_ = false;
    uint64_t lastWMORideTransportGuid_ = 0;
    uint32_t lastWMORideMapId_ = 0xFFFFFFFFu;
    // Set when a rider boards or transfers onto a WMO ship whose deck collision hasn't
    // finished loading yet - holds the boarding-time offset (and freezes camera follow)
    // until this exact transport instance's deck floor exists, instead of letting gravity
    // fold into the attachment and drop the rider through the hull.
    bool deckFloorPending_ = false;

    bool wasAutoAttacking_ = false;
    /// Whether the player was swimming last frame, so weapons are put away
    /// once on entering the water rather than every frame in it.
    bool wasSwimmingForSheath_ = false;

    // Quest marker billboard sprites (above NPCs)
    void loadQuestMarkerModels();  // Now loads BLP textures
    void updateQuestMarkers();     // Updates billboard positions
};

} // namespace core
} // namespace wowee
