#include "core/application.hpp"
#include "core/presentation_recovery.hpp"
#include <cstdio>
#include "core/entity_spawner.hpp"
#include "core/coordinates.hpp"
#include "core/config_paths.hpp"
#include "core/logger.hpp"
#include "game/local_realm.hpp"
#include "game/local_services.hpp"
#include "game/local_scripted_portals.hpp"
#include "rendering/spell_visual_system.hpp"
#include "rendering/character_renderer.hpp"
#include "addons/addon_manager.hpp"
#include "game/local_character_visuals.hpp"
#include "game/local_area_trigger_import.hpp"
#include "game/local_map_import.hpp"
#include "game/local_spell_import.hpp"
#include "game/local_world_catalog.hpp"
#include "game/local_graveyard_sites.hpp"
#include "game/local_pet.hpp"
#include "game/local_pet_bar.hpp"
#include "game/pet_action.hpp"
#include "game/game_handler.hpp"
#include "game/transport_manager.hpp"
#include "audio/audio_coordinator.hpp"
#include "game/expansion_profile.hpp"
#include "auth/auth_handler.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/wdt_loader.hpp"
#include "pipeline/wmo_loader.hpp"
#include "pipeline/wmo_group_path.hpp"
#include "rendering/wmo_renderer.hpp"
#include "rendering/renderer.hpp"
#include "rendering/camera_controller.hpp"
#include "rendering/terrain_manager.hpp"
#include "ui/ui_manager.hpp"
#include "ui/game_screen.hpp"
#include "rendering/animation_controller.hpp"
#include <fstream>
#include <filesystem>
#include <system_error>
#include <algorithm>
#include <imgui.h>
#ifdef WOWEE_PS4
#include "platform/ps4/input_ps4.hpp"
#include "platform/ps4/cpu_memory.hpp"
#include "platform/ps4/ps4_platform.hpp"
#endif
#include <cmath>
#include <cstdlib>

namespace wowee::core {
namespace {
game::Character localCharacter(const game::LocalRealmPlayer& player,
                               const game::LocalWorldContent* content = nullptr) {
    return game::localCharacterVisual(player, content);
}

std::string localContentPath() {
#ifdef WOWEE_PS4
    const std::string overrideContent = "/data/wow_ps/realm/world.json";
#else
    const std::string overrideContent = getConfigRoot() + "/realm/world.json";
#endif
    return std::ifstream(overrideContent).good()
        ? overrideContent : resolveRelativeAssetPath("assets/local_realm/world.json");
}
bool localMapAssetsReady(pipeline::AssetManager& assets, const game::LocalRealmPlayer& player, bool& wmoOnly) {
    std::string map = WorldLoader::mapIdToName(player.mapId);
    if (const auto dbc = assets.loadDBC("Map.dbc"); dbc && dbc->isLoaded() && dbc->getFieldCount() >= 2) {
        for (uint32_t row = 0; row < dbc->getRecordCount(); ++row)
            if (dbc->getUInt32(row, 0) == player.mapId) { map = dbc->getString(row, 1); break; }
    }
    if (map.empty()) { LOG_ERROR("[LOCAL_WORLD] map has no Map.dbc name: ", player.mapId); return false; }
    const std::string base = "World\\Maps\\" + map + "\\" + map;
    const auto wdt = assets.readFile(base + ".wdt");
    if (wdt.empty()) { LOG_ERROR("[LOCAL_WORLD] missing WDT: ", base); return false; }
    const auto info = pipeline::parseWDT(wdt);
    wmoOnly = info.isWMOOnly();
    bool geometry = false;
    if (wmoOnly && !info.rootWMOPath.empty()) {
        const auto rootBytes = assets.readFile(info.rootWMOPath);
        if (!rootBytes.empty()) {
            const auto root = pipeline::WMOLoader::load(rootBytes);
            geometry = root.nGroups > 0;
            for (uint32_t group = 0; geometry && group < root.nGroups; ++group) {
                bool found = false;
                for (const auto& path : pipeline::wmoGroupCandidates(info.rootWMOPath, group))
                    if (!assets.readFile(path).empty()) { found = true; break; }
                if (!found) {
                    LOG_ERROR("[LOCAL_WORLD] missing WMO group: ", info.rootWMOPath, " group=", group);
                    geometry = false;
                }
            }
        }
    }
    else {
        const auto position = coords::canonicalToRender(coords::serverToCanonical(glm::vec3(player.x, player.y, player.z)));
        const auto tile = coords::worldToTile(position.x, position.y);
        geometry = !assets.readFile(base + "_" + std::to_string(tile.first) + "_" + std::to_string(tile.second) + ".adt").empty();
    }
    const bool model = !assets.readFile(game::getPlayerModelPath(localCharacter(player))).empty();
    LOG_INFO("[LOCAL_WORLD] preflight map=", player.mapId, " name=", map, " instance=", player.instanceId,
             " WMO-only=", wmoOnly, " geometry=", geometry, " avatar=", model);
    return geometry && model;
}

std::string localSaveDirectory() {
#ifdef WOWEE_PS4
    return "/data/wow_ps/saves/local_realm";
#else
    return getConfigRoot() + "/saves/local_realm";
#endif
}

/// The marker that asks for the ten level-80 test characters.
///
/// The console has no keyboard, so the only way a player can ask for anything
/// is to put a file somewhere over FTP. The path is resolved through the
/// platform's own writable-root helper - the same one the boot log, the
/// settings and env.txt use, and the one that still answers with the legacy
/// /data/wow_ps/wowee tree on a console that has one - rather than through a
/// second hardcoded literal that would silently disagree with it. The client
/// never creates this file; it only ever consumes one.
std::string localTestCharacterMarker() {
#ifdef WOWEE_PS4
    return platform::ps4::writableRoot() + "/create_test_characters";
#else
    return getConfigRoot() + "/create_test_characters";
#endif
}
}

void Application::requestLocalRealm(int mode, const std::string& name, const std::string& host, uint8_t gender,
                                    uint8_t race, uint8_t classId, uint8_t characterSlot, const std::string& realmName, uint16_t realmPort) {
    if ((state != AppState::AUTHENTICATION && state != AppState::CHARACTER_SELECTION) || localRealmBusy()) return;
    if (mode < 1 || mode > 3 || characterSlot > 9 ||
        !game::isValidRaceClassCombo(static_cast<game::Race>(race), static_cast<game::Class>(classId))) return;
    pendingLocalRealm_ = LocalRealmRequest{mode, name, host, static_cast<uint8_t>(gender == 1 ? 1 : 0),
                                         race, classId, characterSlot};
    pendingLocalRealm_->playerLimit = localHostPlayerLimit_;
    pendingLocalRealm_->realmName = mode == 2 ? localHostRealmName_ : realmName;
    pendingLocalRealm_->realmPort = realmPort;
}

void Application::localRealmStatus(const std::string& message, bool isError) {
    if (!uiManager) return;
    if (localCharacterFlow_ && (state == AppState::CHARACTER_SELECTION || state == AppState::CHARACTER_CREATION)) {
        if (state == AppState::CHARACTER_CREATION) uiManager->getCharacterCreateScreen().setStatus(message, isError);
        else uiManager->getCharacterScreen().setStatus(message, isError);
        return;
    }
    uiManager->getAuthScreen().setStatus(message, isError);
}

void Application::refreshLocalCharacterList() {
    localSlotCharacters_.clear();
    std::vector<game::Character> list;
    const auto savedCharacters = lanCharacterFlowActive()
        ? (localRealm_ ? localRealm_->remoteCharacters() : std::vector<game::LocalSavedCharacter>{})
        : game::LocalRealm::savedCharacters(localSaveDirectory());
    game::LocalGameplay previewContent;
    const game::LocalWorldContent* content = nullptr;
    if (lanCharacterFlowActive() && localRealm_) content = &localRealm_->content();
    else if (!savedCharacters.empty()) {
        std::string error;
        // Read once for this list refresh. This does not start a realm or
        // initialize/migrate a saved player, and honors the same world override.
        if (previewContent.loadContent(localContentPath(), error)) content = &previewContent.content();
        else LOG_WARNING("[LOCAL_REALM] saved equipment preview unavailable: ", error);
    }
    for (const auto& saved : savedCharacters) {
        auto character = localCharacter(saved.player, content);
        const auto visibleItems = std::count_if(character.equipment.begin(), character.equipment.end(),
            [](const game::EquipmentItem& item) { return !item.isEmpty(); });
        LOG_INFO("[LOCAL_REALM] saved appearance slot=", static_cast<int>(saved.slot),
                 " visibleItems=", visibleItems, " bytes=", character.appearanceBytes);
        list.push_back(std::move(character));
        localSlotCharacters_.push_back({saved.slot, saved.player.guid, saved.player.name,
                                        saved.player.race, saved.player.classId, saved.player.gender});
    }
    LOG_INFO("[LOCAL_REALM] character roster source=", lanCharacterFlowActive() ? "host" : "local", " count=", list.size());
    if (gameHandler) gameHandler->setLocalCharacterList(std::move(list));
}

void Application::beginLocalCharacterFlow(int mode, size_t playerLimit, const std::string& realmName) {
    localHostRealmName_ = realmName;
    if (state != AppState::AUTHENTICATION || localRealmBusy()) return;
    if (mode != 1 && mode != 2) return;
    if (!assetManager || !assetManager->isInitialized()) {
        if (uiManager) uiManager->getAuthScreen().setStatus(
            "Game data is missing. Copy your WotLK 3.3.5a MPQs to /data/wow_ps/Data and restart.", true);
        return;
    }
    if (authHandler) authHandler->disconnect();
    localCharacterFlow_ = true;
    localCharacterMode_ = mode;
    localHostPlayerLimit_ = std::clamp(playerLimit, game::LocalRealm::MinPlayers, game::LocalRealm::MaxPlayers);
    // Ten level-80 test characters, one per class. There is no key for this, no
    // menu entry and no console command: the only trigger is a file the player
    // put under the writable root over FTP, so nothing in ordinary play can
    // reach it. The marker is consumed before anything is written, so it runs
    // at most once however it ends.
    std::error_code markerError;
    if (std::filesystem::exists(localTestCharacterMarker(), markerError)) {
        const auto& first = game::kLocalTestCharacters[0];
        LocalRealmRequest seed{mode, first.name, "", 0, first.race, first.classId, first.slot};
        seed.seedTestCharacters = true;
        seed.playerLimit = localHostPlayerLimit_;
        pendingLocalRealm_ = seed;
        LOG_WARNING("[LOCAL_SESSION] test character marker present at ", localTestCharacterMarker(),
                    "; seeding ", game::kLocalTestCharacterCount, " level-",
                    int(game::kLocalTestCharacterLevel), " characters on this entry");
    }
    refreshLocalCharacterList();
    if (uiManager) {
        auto& screen = uiManager->getCharacterScreen();
        screen.setLocalRealmMode(true);
        screen.reset();
        screen.setStatus(mode == 2 ? "LAN Host: the selected character opens the realm on this console." : "", false);
    }
    setState(AppState::CHARACTER_SELECTION);
}

void Application::beginLanCharacterFlow(const std::string& host, const std::string& realmName, uint16_t port, uint64_t realmId) {
    if (state != AppState::AUTHENTICATION || localRealmBusy()) return;
    if (!assetManager || !assetManager->isInitialized()) { localRealmStatus("Game data is not ready.", true); return; }
    if (authHandler) authHandler->disconnect();
    localCharacterFlow_ = true; localCharacterMode_ = 3; localRosterRevision_ = 0; localCreatedName_.clear();
    localHostRealmName_ = realmName; localSlotCharacters_.clear();
    if (gameHandler) gameHandler->setLocalCharacterList({});
    if (uiManager) { auto& screen = uiManager->getCharacterScreen(); screen.setLocalRealmMode(true); screen.reset(); }
    pendingLocalRealm_ = LocalRealmRequest{3, "Lobby", host, 0, 1, 1, 0};
    pendingLocalRealm_->realmName = realmName; pendingLocalRealm_->realmPort = port; pendingLocalRealm_->expectedRealmId = realmId;
    setState(AppState::CHARACTER_SELECTION);
    localRealmStatus("Reading characters from " + realmName + "...", false);
}
void Application::refreshLanCharacters() {
    if (!lanCharacterFlowActive() || !localRealm_ || !localRealm_->refreshRemoteCharacters()) return;
    localRealmStatus(localRealm_->status(), false);
}
void Application::enterLocalCharacter(uint64_t guid) {
    if (!localCharacterFlow_) return;
    for (const auto& c : localSlotCharacters_) {
        if (c.guid != guid) continue;
        if (lanCharacterFlowActive()) {
            const bool ok = localRealm_ && localRealm_->connectCharacter(c.slot);
            localRealmStatus(ok ? "Connecting: " + c.name + "..." : "Character unavailable. Refresh the list.", !ok);
            return;
        }
        requestLocalRealm(localCharacterMode_, c.name, "", c.gender, c.race, c.classId, c.slot);
        if (pendingLocalRealm_) localRealmStatus("Loading world: " + c.name + "...", false);
        return;
    }
    localRealmStatus("Character not found. The list has been refreshed.", true);
    refreshLocalCharacterList();
}

void Application::createLocalCharacter(const game::CharCreateData& data) {
    if (!localCharacterFlow_ || localRealmBusy()) return;
    int slot = -1;
    if (lanCharacterFlowActive()) {
        if (!localRealm_ || !localRealm_->characterListReady()) { localRealmStatus("Wait for the host character list.", true); return; }
        for (int candidate = 0; candidate < game::LocalRealm::MaxCharacterSlots; ++candidate) {
            if (std::none_of(localSlotCharacters_.begin(), localSlotCharacters_.end(), [candidate](const auto& c) { return c.slot == candidate; })) { slot = candidate; break; }
        }
    } else slot = game::LocalRealm::freeCharacterSlot(localSaveDirectory());
    if (slot < 0) {
        localRealmStatus("All ten character slots are occupied.", true);
        return;
    }
    if (!game::isValidRaceClassCombo(data.race, data.characterClass)) {
        localRealmStatus("This race and class combination is not available.", true);
        return;
    }
    if (lanCharacterFlowActive()) {
        localRealm_->setCharacterOptions(uint8_t(data.race), uint8_t(data.characterClass), data.gender == game::Gender::FEMALE ? 1 : 0);
        localRealm_->setCharacterAppearance(data.skin, data.face, data.hairStyle, data.hairColor, data.facialHair, data.useFemaleModel);
        const bool ok = localRealm_->createRemoteCharacter(uint8_t(slot), data.name);
        if (ok) localCreatedName_ = data.name;
        localRealmStatus(ok ? "Saving new character on host..." : "Host character creation could not start.", !ok);
        return;
    }
    LocalRealmRequest request{localCharacterMode_, data.name, "",
                              static_cast<uint8_t>(data.gender == game::Gender::FEMALE ? 1 : 0),
                              static_cast<uint8_t>(data.race), static_cast<uint8_t>(data.characterClass),
                              static_cast<uint8_t>(slot)};
    request.skin = data.skin; request.face = data.face; request.hairStyle = data.hairStyle;
    request.hairColor = data.hairColor; request.facialHair = data.facialHair;
    request.useFemaleModel = data.useFemaleModel;
    request.createOnly = true;
    request.playerLimit = localHostPlayerLimit_;
    request.realmName = localHostRealmName_;
    pendingLocalRealm_ = request;
    localRealmStatus("Creating character...", false);
}

void Application::deleteLocalCharacter(uint64_t guid) {
    if (!localCharacterFlow_ || localRealmBusy()) return;
    for (const auto& c : localSlotCharacters_) {
        if (c.guid != guid) continue;
        if (lanCharacterFlowActive()) {
            const bool ok = localRealm_ && localRealm_->deleteRemoteCharacter(c.slot, guid);
            localRealmStatus(ok ? "Deleting character on host..." : "Character is unavailable or online.", !ok); return;
        }
        const auto name = c.name; // refresh clears the owning vector.
        const bool ok = game::LocalRealm::deleteSavedCharacter(localSaveDirectory(), c.slot);
        refreshLocalCharacterList();
        if (uiManager) uiManager->getCharacterScreen().reset();
        localRealmStatus(ok ? "Character deleted: " + name : "Deletion failed; see the log.", !ok);
        return;
    }
    localRealmStatus("Character not found.", true);
}

void Application::leaveLocalCharacterFlow() {
    localCharacterFlow_ = false;
    stopLocalRealm();
    if (gameHandler) gameHandler->setLocalCharacterList({});
    if (uiManager) {
        uiManager->getCharacterScreen().setLocalRealmMode(false);
        uiManager->getCharacterScreen().reset();
        uiManager->getAuthScreen().setStatus("", false);
    }
    setState(AppState::AUTHENTICATION);
}

bool Application::localRealmBusy() const {
    if (localRealm_ && localRealm_->state() == game::LocalRealmState::Browsing)
        return pendingLocalRealm_.has_value() || localRealm_->characterOperationPending();
    return pendingLocalRealm_.has_value() ||
        (localRealm_ && localRealm_->state() != game::LocalRealmState::Stopped &&
         localRealm_->state() != game::LocalRealmState::Error);
}

void Application::cancelLocalRealm() {
    // Called while the menu is rendering: no GPU objects are destroyed here.
    pendingLocalRealm_.reset();
    disconnectNotice_ = "Connection cancelled. Select the realm to reconnect.";
    logoutToLogin();
}

void Application::stopLocalRealm() {
    pendingLocalRealm_.reset();
    if (gameHandler) gameHandler->resetLocalPresentation();
    if (localRealm_) {
        const auto* saved = localRealm_->localPlayer();
        if (localRealmEntered_ && state == AppState::IN_GAME && playerCharacterSpawned && renderer && gameHandler &&
            saved && (!saved->dead || saved->ghost) && !localWorldEntryGate_.waiting() && saved->positionRevision == localRealmPositionRevision_ &&
            saved->mapId == gameHandler->getCurrentMapId() && saved->instanceId == localRealmInstanceId_) {
            const auto p = coords::canonicalToServer(coords::renderToCanonical(renderer->getCharacterPosition()));
            localRealm_->setLocalPosition(gameHandler->getCurrentMapId(), p.x, p.y, p.z,
                coords::canonicalToServerYaw(coords::characterYawDegToCanonical(renderer->getCharacterYaw())));
        }
        localRealm_->stop();
        if (localRealmEntered_ && !localRealm_->error().empty() && disconnectNotice_.empty())
            disconnectNotice_ = localRealm_->error();
        // stop() closes networking and saves, but retains catalog/runtime caches.
        // Release them before the next character allocates another realm.
        localRealm_.reset();
    }
    localRealmEntered_ = false;
    localWorldEntryGate_.reset();
    localRealmInstanceId_ = 0;
    localRealmWmoOnly_ = false;
    localRealmTravelNotice_.clear();
    localRealmRemoteGuids_.release();
    localRealmNpcGuids_.release();
    localRealmPetGuids_.release();
    localRealmTransportGuids_.release();
    localRealmPresentScratch_.release();
    localRealmMeleeScratch_.release();
    localRealmPresentationSnapshot_.reset();
    localPresentationOomFrames_ = 0;
    localRealmTarget_ = 0;
    localRealmMenuOpen_ = false;
#ifdef WOWEE_PS4
    platform::ps4::setInputMenuNavigation(platform::ps4::MenuOwner::LocalRealm, false);
#endif
    localRealmJournalOpen_ = false;
    localRealmInventoryOpen_ = false;
    localRealmNpcPanelOpen_ = false;
    localRealmDialogueNpc_ = 0;
    localRealmDialogueQuest_ = 0;
    localRealmNotice_ = {};
    if (renderer && renderer->getCameraController()) renderer->getCameraController()->setMovementRooted(false);
    if (renderer && renderer->getAnimationController()) {
        renderer->getAnimationController()->setDead(false);
        renderer->getAnimationController()->resetCombatVisualState();
    }
}

bool Application::enterLocalRealmPortal(uint32_t portalId, bool privateInstance) {
    localRealmTravelNotice_.clear();
    if (!localRealm_ || !assetManager || !localRealm_->localPlayer()) return false;
    if (game::localScriptedPortal(portalId)) return localRealm_->enterPortal(portalId,false);
    if (!localRealm_->content().catalog) return false;
    for (const auto& destination : localRealm_->content().catalog->destinations()) {
        if (destination.id != portalId) continue;
        auto probe = *localRealm_->localPlayer();
        probe.mapId = destination.mapId; probe.x = destination.x; probe.y = destination.y;
        probe.z = destination.z; probe.orientation = destination.orientation;
        bool wmoOnly = false;
        if (!localMapAssetsReady(*assetManager, probe, wmoOnly)) {
            localRealmTravelNotice_ = "Destination map data is missing. Your character remains at the entrance.";
            LOG_ERROR("[LOCAL_WORLD] portal assets rejected before travel id=", portalId, " targetMap=", probe.mapId);
            return false;
        }
        return localRealm_->enterPortal(portalId, privateInstance);
    }
    return false;
}

void Application::prepareLocalFrameXml() {
    if (!localRealm_ || !addonManager_ || !gameHandler || !gameHandler->isLocalExploration()) return;
    localFrameXml_.install(*addonManager_->getLuaEngine(), [this]{return localRealm_.get();},
        [this]{return localRealmTarget_;}, [this]{logoutToLogin();}, [this](uint64_t guid){
            if(localRealm_ && gameHandler) for(const auto& n:localRealm_->npcs())
                if(n.guid==guid){gameHandler->greetLocalRealmNpc(n);break;}
        }, {}, gameHandler.get());
}

void Application::seedLocalTestCharacters(const std::string& saveDirectory) {
    const auto marker = localTestCharacterMarker();
    const auto consumed = marker + ".done";
    std::error_code ec;
    // Claim the marker BEFORE writing anything. A seeding run that is
    // interrupted - by a crash, by a full disk, by the player pulling the
    // power - must not be able to run again on the next boot and hand somebody
    // a second set of characters they did not ask for.
    std::filesystem::remove(consumed, ec);
    ec.clear();
    std::filesystem::rename(marker, consumed, ec);
    if (ec) {
        ec.clear();
        std::filesystem::remove(marker, ec);
    }
    ec.clear();
    if (std::filesystem::exists(marker, ec)) {
        LOG_ERROR("[LOCAL_SESSION] the test character marker ", marker,
                  " could not be renamed or removed; refusing to seed rather than seed on every boot");
        localRealm_->stop();
        localRealm_.reset();
        refreshLocalCharacterList();
        if (uiManager) uiManager->getCharacterScreen().reset();
        setState(AppState::CHARACTER_SELECTION);
        localRealmStatus("The test character marker could not be removed. Nothing was created.", true);
        return;
    }
    std::vector<game::LocalTestCharacterSpec> specs;
    specs.reserve(game::kLocalTestCharacterCount);
    for (const auto& row : game::kLocalTestCharacters)
        specs.push_back({row.slot, row.race, row.classId, 0, game::kLocalTestCharacterLevel, row.name});
    // The realm this is called with already carries the content, the catalog,
    // the faction templates and the imported spells; seedTestCharacters keeps
    // all of it (retainConfiguration) while replacing the session state, which
    // is exactly what startSinglePlayer does.
    const size_t created = localRealm_->seedTestCharacters(saveDirectory, specs);
    const auto failure = localRealm_->error();
    localRealm_->stop();
    localRealm_.reset();
    refreshLocalCharacterList();
    if (uiManager) uiManager->getCharacterScreen().reset();
    setState(AppState::CHARACTER_SELECTION);
    LOG_WARNING("[LOCAL_SESSION] seeded ", created, " test characters");
    if (created) {
        localRealmStatus("Seeded " + std::to_string(created) + " level-" +
                         std::to_string(int(game::kLocalTestCharacterLevel)) + " test characters.", false);
        return;
    }
    localRealmStatus(failure.empty() ? "No test characters were created; the ten slots are already in use."
                                     : failure, true);
}

void Application::updateLocalRealm(float deltaTime) {
    if(logoutToLoginPending_)return;
    if (pendingLocalRealm_) {
        const auto request = *pendingLocalRealm_;
        pendingLocalRealm_.reset();
        if (!assetManager || !assetManager->isInitialized() || !worldLoader_ || !entitySpawner_) {
            localRealmStatus(
                "Game data is missing. Copy your WotLK 3.3.5a MPQs to /data/wow_ps/Data and restart.", true);
            return;
        }
        // A remembered external login may have selected Classic/TBC tables.
        // The standalone realm uses only WotLK 3.3.5a maps and avatar metadata.
        if (!expansionRegistry_ || !expansionRegistry_->getActive() ||
            expansionRegistry_->getActive()->id != "wotlk") {
            if (!expansionRegistry_ || !expansionRegistry_->setActive("wotlk")) {
                localRealmStatus("The WotLK profile is missing.", true);
                return;
            }
            setAssetExpansionOverride({});
            reloadExpansionData();
        }
        if (authHandler) authHandler->disconnect();
        localRealm_ = std::make_unique<game::LocalRealm>();
        const std::string contentPath = localContentPath();
        if (!localRealm_->loadContent(contentPath) ||
            !localRealm_->setCharacterOptions(request.race, request.classId, request.gender) ||
            !localRealm_->setCharacterAppearance(request.skin, request.face, request.hairStyle,
                                                 request.hairColor, request.facialHair, request.useFemaleModel) ||
            !localRealm_->setCharacterSlot(request.characterSlot)) {
            LOG_ERROR("[LOCAL_GAMEPLAY] content/character rejected: ", localRealm_->error());
            localRealmStatus(localRealm_->error(), true);
            return;
        }
        LOG_INFO("[LOCAL_GAMEPLAY] content=", contentPath, " NPC spawns=", localRealm_->content().spawns.size(),
                 " quests=", localRealm_->content().quests.size());
#ifdef WOWEE_PS4
        const auto available=platform::ps4::queryAvailableCpuMemory();
        LOG_WARNING("[LOCAL_SESSION] before spell import freeMiB=",available.bytes/(1024*1024),
                    " measured=",available.measured," cached=",bool(localSpellImport_));
#endif
        if (!localSpellImport_) {
        LOG_WARNING("[LOCAL_SESSION] importing starter spells (first session only)");
        auto spellDb=assetManager->loadDBC("Spell.dbc");
        const auto rangeDb=assetManager->loadDBC("SpellRange.dbc");
        const auto castDb=assetManager->loadDBC("SpellCastTimes.dbc");
        const auto durationDb=assetManager->loadDBC("SpellDuration.dbc");
        const auto iconDb=assetManager->loadDBC("SpellIcon.dbc");
        const auto runeCostDb=assetManager->loadDBC("SpellRuneCost.dbc");
        const auto radiusDb=assetManager->loadDBC("SpellRadius.dbc");
        const auto abilities=assetManager->loadDBC("SkillLineAbility.dbc");
        const auto skills=assetManager->loadDBC("SkillLine.dbc");
        const auto talents=assetManager->loadDBC("Talent.dbc");
        const auto tabs=assetManager->loadDBC("TalentTab.dbc");
        auto importedSpells=game::importClientStarterSpells(spellDb.get(),rangeDb.get(),castDb.get(),durationDb.get(),iconDb.get(),
            abilities.get(),skills.get(),talents.get(),runeCostDb.get(),radiusDb.get());
        game::detail::importClientTalents(importedSpells,talents.get(),tabs.get(),spellDb.get(),rangeDb.get(),castDb.get(),durationDb.get(),iconDb.get(),runeCostDb.get(),radiusDb.get());
        for(const auto& row:importedSpells.audit)
            LOG_INFO("[CLASS_AUDIT] spell=",row.id," classMask=",row.classes," talent=",row.talent," result=",row.status);
        LOG_INFO("[CLASS_AUDIT] rows=",importedSpells.audit.size()," scope=starter/class-skill/talent ranks; decoder support is not full gameplay verification");
        importedSpells.audit.clear();importedSpells.audit.shrink_to_fit();
#ifdef WOWEE_PS4
        // Import owns its strings/effects. Release this 49,839-row source table
        // before terrain decoding competes for the console's CPU heap.
        spellDb.reset();
        assetManager->evictDBC("Spell.dbc");
        LOG_INFO("[LOCAL_SPELLS] released Spell.dbc import cache before terrain loading");
#endif
        if (importedSpells.spells.empty()) {
            localRealmStatus(importedSpells.diagnostic, true);stopLocalRealm();return;
        }
        localSpellImport_ = std::make_unique<game::LocalSpellImport>(std::move(importedSpells));
        } else LOG_WARNING("[LOCAL_SESSION] reusing compact starter spells; no Spell.dbc reload");
        const auto& importedSpells = *localSpellImport_;
        if(!localRealm_->setStarterSpells(importedSpells.spells,importedSpells.diagnostic)) {
            localRealmStatus(localRealm_->error(),true);return;
        }
        LOG_INFO("[LOCAL_SPELLS] ",importedSpells.diagnostic);
        for(const auto& spell:importedSpells.spells) {
            if(!spell.iconPath.empty())localRealmSpellIconPaths_[spell.iconId]=spell.iconPath;
            if(!spell.talentId)LOG_INFO("[LOCAL_SPELLS] id=",spell.id," classMask=",spell.allowableClasses," name=",spell.name,
                " castMs=",spell.castTimeMs," gcdMs=",spell.globalCooldownMs," internal=",spell.triggeredOnly," supported=",spell.unsupportedReason.empty(),
                " reason=",spell.unsupportedReason);
        }
        std::array<uint32_t, 12> raceFactions{};
        if (const auto dbc = assetManager->loadDBC("ChrRaces.dbc"); dbc && dbc->isLoaded() && dbc->getFieldCount() >= 3) {
            for (uint32_t row = 0; row < dbc->getRecordCount(); ++row) {
                const auto race = dbc->getUInt32(row, 0);
                if (race < raceFactions.size()) raceFactions[race] = dbc->getUInt32(row, 2);
            }
        }
        std::vector<game::LocalFactionTemplate> factions;
        if (const auto dbc = assetManager->loadDBC("FactionTemplate.dbc"); dbc && dbc->isLoaded() && dbc->getFieldCount() >= 14) {
            factions.reserve(dbc->getRecordCount());
            for (uint32_t row = 0; row < dbc->getRecordCount(); ++row) {
                game::LocalFactionTemplate value;
                value.id = dbc->getUInt32(row, 0); value.faction = dbc->getUInt32(row, 1);
                value.flags = dbc->getUInt32(row, 2); value.factionGroup = dbc->getUInt32(row, 3);
                value.friendGroup = dbc->getUInt32(row, 4); value.enemyGroup = dbc->getUInt32(row, 5);
                for (uint32_t i = 0; i < 4; ++i) {
                    value.enemies[i] = dbc->getUInt32(row, 6 + i); value.friends[i] = dbc->getUInt32(row, 10 + i);
                }
                factions.push_back(value);
            }
        }
        if (!localRealm_->setFactionTemplates(factions, raceFactions)) {
            localRealmStatus(localRealm_->error(), true);
            return;
        }
        LOG_INFO("[LOCAL_WORLD] client faction templates=", factions.size());
        const auto graveyards = game::buildPinnedLocalGraveyards();
        if (!localRealm_->setGraveyards(graveyards)) {
            localRealmStatus(localRealm_->error(), true);
            return;
        }
        LOG_INFO("[LOCAL_GRAVEYARDS] source-linked zone/faction sites=", graveyards.size());
        std::vector<game::LocalAreaTriggerVolume> portalVolumes;
        if (const auto dbc = assetManager->loadDBC("AreaTrigger.dbc"); dbc && dbc->isLoaded() && dbc->getFieldCount() >= 10) {
            portalVolumes.reserve(dbc->getRecordCount());
            for (uint32_t row = 0; row < dbc->getRecordCount(); ++row) {
                game::LocalAreaTriggerVolume volume;
                volume.id = dbc->getUInt32(row, 0); volume.mapId = dbc->getUInt32(row, 1);
                volume.x = dbc->getFloat(row, 2); volume.y = dbc->getFloat(row, 3); volume.z = dbc->getFloat(row, 4);
                volume.radius = dbc->getFloat(row, 5); volume.boxLength = dbc->getFloat(row, 6);
                volume.boxWidth = dbc->getFloat(row, 7); volume.boxHeight = dbc->getFloat(row, 8); volume.boxYaw = dbc->getFloat(row, 9);
                portalVolumes.push_back(volume);
            }
        }
        const auto sourceVolumeCount = portalVolumes.size();
        size_t rejectedVolumes = 0;
        portalVolumes = game::importClientAreaTriggers(portalVolumes,
            [&](const game::LocalAreaTriggerVolume& v, const std::string& reason) {
                ++rejectedVolumes;
                if (rejectedVolumes <= 32)
                    LOG_WARNING("[LOCAL_WORLD] AreaTrigger skipped id=", v.id, " map=", v.mapId,
                        " reason=", reason, " pos=", v.x, ",", v.y, ",", v.z,
                        " radius=", v.radius, " box=", v.boxLength, ",", v.boxWidth, ",", v.boxHeight);
            });
        LOG_INFO("[LOCAL_WORLD] AreaTrigger import rows=", sourceVolumeCount,
                 " accepted=", portalVolumes.size(), " skipped=", rejectedVolumes);
        if (!localRealm_->setAreaTriggers(portalVolumes)) {
            localRealmStatus(localRealm_->error(), true);
            return;
        }
        LOG_INFO("[LOCAL_WORLD] client portal volumes=", portalVolumes.size());

        // The travel network: flight routes and the transports that run them.
        //
        // Read from the player's own TaxiNodes/TaxiPath/TaxiPathNode DBCs -
        // the same three MovementHandler parses for online play - rather than
        // from a table shipped with this client. A route the client does not
        // describe therefore does not exist, which is what keeps a flight from
        // being sold over geometry nobody has.
        //
        // Failing to load them is not fatal. Without taxi data there are no
        // flight masters and no ships; everything else in the realm is
        // unaffected, so the world still opens.
        loadLocalTravelNetwork();

        const auto saveDir = localSaveDirectory();
        // Content, catalog, faction templates and the imported starter spells
        // are all installed by now, which is what a level-80 spellbook and a
        // level-80 health/mana pool are derived from. Nothing below this point
        // is needed to write a character.
        if (request.seedTestCharacters) { seedLocalTestCharacters(saveDir); return; }
        if (!localRealm_->setRealmName(request.realmName)) {
            localRealmStatus("Realm name must be 1-48 letters, numbers, spaces, apostrophes, - or _.", true);
            return;
        }
        // Bots are part of the world, so the choice made on the host screen is
        // applied before it starts rather than switched on inside it. A guest
        // never sets this: the host's bots reach it as ordinary players.
        localRealm_->setPlayerbots(localPlayerbotsEnabled_);

        bool started = request.mode == 1
            ? localRealm_->startSinglePlayer(saveDir, request.name)
            : request.mode == 2 ? localRealm_->startHost(saveDir, request.name, game::LocalRealm::DefaultPort, request.playerLimit)
            : localRealm_->browseHost(request.host, saveDir + "/client", request.realmPort, request.expectedRealmId);
        if (!started) {
            localRealmStatus(localRealm_->error(), true);
            return;
        }
        if (request.createOnly) {
            // The realm ran only to create and save the character. Back to
            // the list with the new hero selected; Enter World starts it again.
            localRealm_->stop();
            localRealm_.reset();
            refreshLocalCharacterList();
            if (uiManager) {
                uiManager->getCharacterScreen().reset();
                uiManager->getCharacterScreen().selectCharacterByName(request.name);
            }
            setState(AppState::CHARACTER_SELECTION);
            localRealmStatus("Character created: " + request.name, false);
            return;
        }
        localRealmStatus(localRealm_->status(), false);
    }
    if (!localRealm_) return;
    localRealm_->update(deltaTime);
    if (localRealm_->state() == game::LocalRealmState::Error) {
        const auto failure = localRealm_->error();
        disconnectNotice_ = failure;
        LOG_WARNING("[LAN_RECOVERY] connection error; scheduling full session unload entered=",localRealmEntered_);
        logoutToLoginPending_=true;
        return;
    }
    if (localRealm_->state() == game::LocalRealmState::Browsing) {
        if (localRosterRevision_ != localRealm_->characterListRevision()) {
            localRosterRevision_ = localRealm_->characterListRevision();
            refreshLocalCharacterList();
            if (localRealm_->characterListReady() && !localCreatedName_.empty()) {
                setState(AppState::CHARACTER_SELECTION);
                if (uiManager) { uiManager->getCharacterScreen().reset(); uiManager->getCharacterScreen().selectCharacterByName(localCreatedName_); }
                localCreatedName_.clear();
            }
            localRealmStatus(localRealm_->status(), !localRealm_->error().empty());
        }
        return;
    }
    const auto* player = localRealm_->localPlayer();
    if (!localRealm_->ready() || !player) return;
    if (gameHandler) gameHandler->setLocalWorldTimeHours(localRealm_->worldTimeHours());
    if (!localRealmEntered_) {
        const auto c = localCharacter(*player, &localRealm_->content());
        if (!localMapAssetsReady(*assetManager, *player, localRealmWmoOnly_)) {
            disconnectNotice_="World or character data is missing. Check the WotLK 3.3.5a MPQs and extraction log.";
            LOG_ERROR("[LAN_RECOVERY] joined character failed asset preflight; scheduling full unload");
            logoutToLoginPending_=true;
            return;
        }
        localRealm_->setWorldLoading(true);
        localRealmEntered_ = true;
        localRealmPositionRevision_ = player->positionRevision;
        localRealmInstanceId_ = player->instanceId;
        localRealmTarget_ = 0;
        gameHandler->beginLocalExploration(c, player->orientation);
        if(player->transportEntry) gameHandler->resumeLocalTransport(player->transportEntry,player->mapId,
            glm::vec3(player->transportOffsetX,player->transportOffsetY,player->transportOffsetZ));
        gameHandler->setLocalWorldTimeHours(localRealm_->worldTimeHours());
        return; // WorldLoader processes the queued entry between frames.
    }
    if (state != AppState::IN_GAME || !renderer || !gameHandler) return;
    // WorldLoader owns queued map entry between frames. Do not repeatedly
    // enqueue the same relocation or declare its not-yet-published scene bad.
    if (worldLoader_ && (worldLoader_->hasPendingEntry() || worldLoader_->isLoadingWorld())) {
        localRealm_->setWorldLoading(true);
        return;
    }
    // Authority relocations (respawn/recovery) must be applied before sending
    // another controller position, or a stale camera would overwrite them.
    if (player->positionRevision != localRealmPositionRevision_) {
        localWorldEntryGate_.beginRelocation();
        const auto canonical = coords::serverToCanonical(glm::vec3(player->x, player->y, player->z));
        auto position = coords::canonicalToRender(canonical);
        bool acherusArrival=false;
        if(player->classId==6 && player->portalCooldown>0 && renderer->getWMORenderer()) {
            for(const auto& pad:game::LocalAcherusPortals) if(pad.contains(player->mapId,player->x,player->y,player->z)) {
                if(const auto floor=renderer->getWMORenderer()->getFloorHeight(position.x,position.y,
                        position.z+3.f,nullptr,position.z,.5f);floor && std::abs(*floor-position.z)<7.f)
                    position.z=*floor+.05f;
                acherusArrival=true;
                break;
            }
        }
        if (player->mapId != gameHandler->getCurrentMapId()) {
            localRealm_->setWorldLoading(true);
            if (!localMapAssetsReady(*assetManager, *player, localRealmWmoOnly_)) {
                disconnectNotice_ = "Could not load the destination map. Check game data and the log.";
                logoutToLogin();
                return;
            }
            localRealmInstanceId_ = player->instanceId;
            const auto c = localCharacter(*player, &localRealm_->content());
            localRealmRemoteGuids_.clear();
            localRealmNpcGuids_.clear();
            localRealmPetGuids_.clear();
            localRealmTarget_ = 0;
            gameHandler->beginLocalExploration(c, player->orientation, player->transportEntry != 0);
            if(player->transportEntry) gameHandler->resumeLocalTransport(player->transportEntry,player->mapId,
                glm::vec3(player->transportOffsetX,player->transportOffsetY,player->transportOffsetZ));
        gameHandler->setLocalWorldTimeHours(localRealm_->worldTimeHours());
            return;
        }
        if (localRealmInstanceId_ != player->instanceId) {
            localRealmInstanceId_ = player->instanceId;
            localRealmTarget_ = 0;
            gameHandler->setTargetGuidRaw(0);
        }
        if (renderer->getCameraController()) renderer->getCameraController()->teleportTo(position);
        renderer->getCharacterPosition() = position;
        if(acherusArrival && renderer->getSpellVisualSystem())
            renderer->getSpellVisualSystem()->playAcherusTransition(position);
        renderer->setCharacterYaw(coords::canonicalToCharacterYawDeg(
            coords::serverToCanonicalYaw(player->orientation)));
        localRealmPositionRevision_ = player->positionRevision;
        LOG_INFO("[LOCAL_GAMEPLAY] authoritative relocation revision=", localRealmPositionRevision_,
                 " map=", player->mapId, " position=", player->x, ",", player->y, ",", player->z);
    }
    // Validate AFTER applying authority relocation: its camera determines
    // which terrain must stream next. Old city tiles can all retire before
    // the graveyard tile finishes uploading; tiles=0 alone is not a failure.
    auto* terrain = renderer->getTerrainManager();
    const auto entryPosition = renderer->getCharacterPosition();
    const bool geometryLoaded = localRealmWmoOnly_
        ? renderer->getWMORenderer() && renderer->getWMORenderer()->getInstanceCount() > 0
        : terrain && terrain->isTileLoadedAt(entryPosition.x, entryPosition.y);
    const bool streamPending = terrain && terrain->getPendingTileCount() > 0;
    const bool wasWaiting = localWorldEntryGate_.waiting();
    const auto entryGate = localWorldEntryGate_.update(playerCharacterSpawned,
        geometryLoaded || characterIntroOwnsView(), streamPending);
    if (entryGate != core::LocalWorldEntryGate::Result::Ready) {
        localRealm_->setWorldLoading(true);
        if (entryGate == core::LocalWorldEntryGate::Result::Waiting) {
            if (!wasWaiting)
                LOG_INFO("[LOCAL_WORLD_WAIT] destination scene pending map=", player->mapId,
                    " revision=", player->positionRevision, " avatar=", playerCharacterSpawned,
                    " pendingTiles=", terrain ? terrain->getPendingTileCount() : 0);
            if (auto* camera = renderer->getCameraController()) camera->suspendGravityFor(1.f);
            return; // Keep pumping WorldLoader/TerrainManager on subsequent frames.
        }
        disconnectNotice_ = "Could not load the destination world. Check game data and the log.";
        LOG_ERROR("[LOCAL_REALM] World entry failed after bounded streaming wait: avatar=",
            playerCharacterSpawned, " terrain tiles=", terrain ? terrain->getLoadedTileCount() : 0,
            " pendingTiles=", terrain ? terrain->getPendingTileCount() : 0);
        logoutToLogin();
        return;
    }
    if (wasWaiting) LOG_INFO("[LOCAL_WORLD_WAIT] destination ready map=", player->mapId,
                            " revision=", player->positionRevision);
    localRealm_->setWorldLoading(false);
    if (!player->dead || player->ghost) {
        const auto server = coords::canonicalToServer(coords::renderToCanonical(renderer->getCharacterPosition()));
        // Collision, liquid and interior-group state come from the local
        // geometry owner, alongside the existing position report. The realm
        // applies fall damage and outdoor-only form rules. This retains the
        // existing LAN movement trust model; it is not remote VMAP validation.
        const auto* fallCamera = renderer->getCameraController();
        const auto renderPosition=renderer->getCharacterPosition();
        const bool indoors=renderer->getWMORenderer() && renderer->getWMORenderer()->isInsideInteriorWMO(
            renderPosition.x,renderPosition.y,renderPosition.z+1.f);
        const uint8_t movement = static_cast<uint8_t>(
            (fallCamera && fallCamera->isFalling() ? game::kLocalMovementFalling : 0u) |
            (fallCamera && fallCamera->isSwimming() ? game::kLocalMovementInLiquid : 0u) |
            (indoors ? game::kLocalMovementIndoors : 0u));
        localRealm_->setLocalPosition(gameHandler->getCurrentMapId(), server.x, server.y, server.z,
            coords::canonicalToServerYaw(coords::characterYawDegToCanonical(renderer->getCharacterYaw())),
            movement);
        if(const auto* hull=gameHandler->getTransportManager()->getTransport(gameHandler->getPlayerTransportGuid());
           hull && hull->locallyScheduled && player->transportEntry==hull->entry) {
            const glm::vec3 modelOffset(hull->invTransform * glm::vec4(renderer->getCharacterPosition(),1));
            localRealm_->setLocalTransportOffset(hull->entry,-modelOffset.x,-modelOffset.y,modelOffset.z,
                hull->serverYaw-game::TransportManager::transportModelBowOffset(hull->displayId));
        }
    }
    // Physical deck tests own boarding; the authority retains that attachment at seams.
    if (!gameHandler->hasPendingPlayerTransportWorldTransfer()) {
        const uint64_t aboard = gameHandler->getPlayerTransportGuid();
        const auto* current = localRealm_->localPlayer();
        const auto* hull = gameHandler->getTransportManager()->getTransport(aboard);
        if (current && !current->transportEntry && hull && hull->locallyScheduled)
            localRealm_->boardTransport(hull->entry);
        else if (current && current->transportEntry && !aboard)
            localRealm_->leaveTransport();
    }
    std::shared_ptr<game::LocalRealmPlayer> snapshot;
    const auto presentationResult = updatePresentationWithRecovery(localPresentationOomFrames_, [&] {
    // Retain vector/string capacity between frames, while owning a snapshot
    // independent of commands that rebuild the realm's player list. The local
    // shared owner also survives a callback scheduling session destruction.
    if (!localRealmPresentationSnapshot_)
        localRealmPresentationSnapshot_ = std::make_shared<game::LocalRealmPlayer>();
    snapshot = localRealmPresentationSnapshot_;
    *snapshot = *localRealm_->localPlayer();
    const auto& self = *snapshot;
    if(audioCoordinator_) {
        bool zeppelin=false;
        for(const auto& route:localRealm_->travel().transportRoutes())if(route.entry==self.transportEntry)
            zeppelin=(route.displayId==3031 || route.displayId==7546);
        audioCoordinator_->setTransportMusic(self.transportEntry,zeppelin,assetManager.get());
    }
    const uint64_t previousAttackTarget = gameHandler->previousLocalAttackTarget(self.guid);
    const auto oldSelf = gameHandler->getEntityManager().getEntity(self.guid);
    const bool tookDamage = oldSelf && std::static_pointer_cast<game::Unit>(oldSelf)->getHealth() > self.health;
    const bool wasSelfGhost = gameHandler->isLocalGhostUnit(self.guid);
    if (gameHandler->syncLocalRealmPlayer(self, localRealm_->content())) {
        if (uiManager) uiManager->getGameScreen().refreshLocalEquipment(gameHandler->getInventory());
        if (appearanceComposer_) appearanceComposer_->loadEquippedWeapons();
        LOG_INFO("[LOCAL_GAMEPLAY] equipped visuals synchronized");
    }
    // Reapply while ghost so a delayed avatar/equipment spawn receives the
    // presentation even if the transition callback ran before its instance
    // existed (or another callback consumer replaced it). Restore on reclaim
    // only, leaving ordinary living stealth/fade opacity untouched.
    if (self.ghost || wasSelfGhost)
        if (auto* characters = renderer->getCharacterRenderer())
            characters->setInstanceOpacity(renderer->getCharacterInstanceId(), self.ghost ? .5f : 1.f);
    if (auto* animation = renderer->getAnimationController()) {
        animation->setDead(self.dead && !self.ghost);
        animation->setInCombat(self.attackTarget != 0 && !self.dead);
        animation->setLowHealth(self.health > 0 && self.health * 4 < self.maxHealth);
    }
    auto& present = localRealmPresentScratch_;
    present.clear();
    for (const auto& peer : localRealm_->players()) {
        if (peer.guid == self.guid || peer.mapId != gameHandler->getCurrentMapId() || peer.instanceId != self.instanceId) continue;
        present.insert(peer.guid);
        const bool wasPeerGhost = gameHandler->isLocalGhostUnit(peer.guid);
        gameHandler->syncLocalRealmPlayer(peer, localRealm_->content());
        if (peer.ghost || wasPeerGhost)
            if (auto* characters = renderer->getCharacterRenderer())
                characters->setInstanceOpacity(gameHandler->resolveUnitRenderInstance(peer.guid),
                                               peer.ghost ? .5f : 1.f);
    }
    for (auto guid : localRealmRemoteGuids_)
        if (!present.count(guid)) gameHandler->removeLocalExplorationPlayer(guid);
    localRealmRemoteGuids_.swap(present);
    present.clear();
    auto& presentedMelee = localRealmMeleeScratch_;
    presentedMelee.clear();
    const auto presentImpact = [&](uint64_t attacker, uint64_t victim) {
        if (presentedMelee.count(attacker)) return;
        presentedMelee.insert(attacker);
        gameHandler->presentLocalMeleeImpact(attacker, victim);
    };
    for (const auto& npc : localRealm_->npcs()) {
        if (npc.mapId != gameHandler->getCurrentMapId() || npc.instanceId != self.instanceId) continue;
        const float dx = npc.x - self.x, dy = npc.y - self.y;
        if (dx * dx + dy * dy > 350.0f * 350.0f) continue;
        present.insert(npc.guid);
        const auto old = gameHandler->getEntityManager().getEntity(npc.guid);
        const uint32_t oldHealth = old ? std::static_pointer_cast<game::Unit>(old)->getHealth() : npc.health;
        gameHandler->syncLocalRealmNpc(npc);
        // Local exploration bypasses the network-mode resync sweep. Restore
        // missing visuals after a transient parse/upload failure here too.
        if (npc.displayId && entitySpawner_ && entitySpawner_->canRetryCreature(npc.guid, npc.displayId)) {
            const auto p = coords::serverToCanonical(glm::vec3(npc.x, npc.y, npc.z));
            entitySpawner_->queueCreatureSpawn(npc.guid, npc.displayId, p.x, p.y, p.z,
                coords::serverToCanonicalYaw(npc.orientation), 1.0f);
        }
        if (game::localMeleeDamageObserved(oldHealth, npc, self.guid,
                                           self.attackTarget, previousAttackTarget)) {
            presentImpact(self.guid, npc.guid);
        }
        if (tookDamage && npc.targetGuid == self.guid && !npc.dead) {
            presentImpact(npc.guid, self.guid);
        }
        if (oldHealth > npc.health) {
            for (const auto& peer : localRealm_->players()) {
                if (peer.guid != self.guid && peer.attackTarget == npc.guid) {
                    presentImpact(peer.guid, npc.guid);
                }
            }
        }
    }
    for (auto guid : localRealmNpcGuids_) {
        if (present.count(guid)) continue;
        gameHandler->removeLocalRealmNpc(guid);
        if (localRealmTarget_ == guid) localRealmTarget_ = 0;
    }
    localRealmNpcGuids_.swap(present);
    present.clear();
    // Owned creatures walk the same gate and the same spawn retry as a spawn,
    // but their own retained set: the sweep above would evict every one of them.
    uint64_t controlledPet = 0;
    uint32_t petEntry = 0, petSpell = 0;
    uint8_t petLevel = 1;
    bool petAutocast = false;
    uint8_t petCommand = uint8_t(game::kLocalPetDefaultCommand);
    uint8_t petReact = uint8_t(game::kLocalPetDefaultReact);
    for (const auto& pet : localRealm_->pets()) {
        if (pet.mapId != gameHandler->getCurrentMapId() || pet.instanceId != self.instanceId) continue;
        const float dx = pet.x - self.x, dy = pet.y - self.y;
        if (dx * dx + dy * dy > 350.0f * 350.0f) continue;
        present.insert(pet.guid);
        if (pet.ownerGuid == self.guid && pet.kind == game::LocalPetKind::Controlled) {
            controlledPet = pet.guid;
            petEntry = pet.entry;
            petLevel = pet.level;
            petSpell = game::localPetFireboltSpell(pet.entry, pet.level);
            const auto* petSpellDefinition = localRealm_->content().spell(petSpell);
            if (!petSpellDefinition || !petSpellDefinition->npcOnly ||
                !petSpellDefinition->unsupportedReason.empty()) {
                petSpell = 0;
                petEntry = 0;
            }
            petAutocast = pet.fireboltAutocast;
            petCommand = uint8_t(pet.command);
            petReact = uint8_t(pet.react);
        }
        gameHandler->syncLocalRealmPet(pet);
        if (pet.displayId && entitySpawner_ && entitySpawner_->canRetryCreature(pet.guid, pet.displayId)) {
            const auto p = coords::serverToCanonical(glm::vec3(pet.x, pet.y, pet.z));
            entitySpawner_->queueCreatureSpawn(pet.guid, pet.displayId, p.x, p.y, p.z,
                coords::serverToCanonicalYaw(pet.orientation), 1.0f);
        }
    }
    for (auto guid : localRealmPetGuids_) {
        if (present.count(guid)) continue;
        gameHandler->removeLocalRealmPet(guid);
        if (localRealmTarget_ == guid) localRealmTarget_ = 0;
    }
    localRealmPetGuids_.swap(present);
    // What the Lua token "pet" resolves to. The network path announces the same
    // transition from SMSG_PET_SPELLS: UNIT_PET alone tells the frames the pet
    // changed but not that their interface should be redrawn.
    //
    // P07 : the pet bar and its stance ring are the same ones the
    // connected-server path draws out of SMSG_PET_SPELLS, and until now the
    // local path left every one of their inputs at zero. The ten slots are
    // CharmInfo::InitPetActionBar's own layout - three commands as
    // COMMAND_ATTACK - i, four empty spell slots, three reactions the same way
    // - and the command and react bytes are the two SMSG_PET_SPELLS sends
    // after the duration (Player::PetSpellInitialize, Player.cpp:9760-9765).
    // Reconcile rank and autocast even when the GUID stays the same: both a
    // level-up and a host-approved toggle can change the existing pet's bar.
    const bool changedPet = gameHandler->petGuidRef() != controlledPet;
    bool changedPetBar = changedPet;
    auto& slots = gameHandler->petActionSlotsRef();
    for (unsigned slot = 0; slot < game::pet::kActionBarSlots; ++slot) {
        const uint32_t desired = controlledPet
            ? game::localPetActionBarSlot(slot, petEntry, petLevel, petAutocast) : 0u;
        if (slots[slot] != desired) { slots[slot] = desired; changedPetBar = true; }
    }
    if (changedPetBar) {
        gameHandler->petGuidRef() = controlledPet;
        gameHandler->petSpellListRef().clear();
        gameHandler->petAutocastSpellsRef().clear();
        if (petSpell) {
            gameHandler->petSpellListRef().push_back(petSpell);
            if (petAutocast) gameHandler->petAutocastSpellsRef().insert(petSpell);
        }
        if (changedPet) gameHandler->fireAddonEvent("UNIT_PET", {"player"});
        gameHandler->fireAddonEvent("PET_UI_UPDATE", {});
        gameHandler->fireAddonEvent("PET_BAR_UPDATE", {});
        gameHandler->fireAddonEvent("PET_BAR_UPDATE_USABLE", {});
    }
    if (gameHandler->petCommandRef() != petCommand || gameHandler->petReactRef() != petReact) {
        gameHandler->petCommandRef() = petCommand;
        gameHandler->petReactRef() = petReact;
        // The same event pair SMSG_PET_MODE fires: the bar redraws the pressed
        // command and the chosen stance from these two bytes and nothing else.
        gameHandler->fireAddonEvent("PET_BAR_UPDATE", {});
        gameHandler->fireAddonEvent("UNIT_PET", {"player"});
    }
    gameHandler->setTargetGuidRaw(localRealmTarget_);

    syncLocalRealmTransports(self);
    if(auto* visuals=renderer->getSpellVisualSystem()) {
        std::vector<std::pair<uint64_t,glm::vec3>> pads;
        if(self.classId==6 && !self.instanceId) for(const auto& pad:game::LocalAcherusPortals) {
            const float dx=self.x-pad.x,dy=self.y-pad.y;
            if(pad.mapId!=self.mapId || dx*dx+dy*dy>80.f*80.f)continue;
            auto position=coords::canonicalToRender(coords::serverToCanonical(glm::vec3(pad.x,pad.y,pad.arrivalZ())));
            if(auto* wmo=renderer->getWMORenderer())
                if(const auto floor=wmo->getFloorHeight(position.x,position.y,position.z+3.f,nullptr,position.z,.5f);
                   floor && std::abs(*floor-position.z)<7.f) position.z=*floor+.03f;
            pads.emplace_back(pad.id,position);
        }
        visuals->setAcherusPortals(pads);
    }
    }, [&] {
        if (assetManager) assetManager->trimFileCache();
    });
    if (presentationResult != PresentationUpdateResult::Complete) {
        // This path allocates no diagnostic string at exhausted headroom.
        // Finish the ordinary frame, then perform the normal session teardown
        // if thirty consecutive presentations could not be rebuilt.
        std::fprintf(stderr, "[LOCAL_PRESENTATION_MEMORY] allocation failure frame=%u action=%s\n",
            localPresentationOomFrames_, presentationResult == PresentationUpdateResult::EndSession
                ? "session unload queued" : "retry presentation next frame");
        if (presentationResult == PresentationUpdateResult::EndSession)
            logoutToLoginPending_ = true;
        return;
    }
    const auto& self = *snapshot;
    // Gameplay owns automatic entrance activation, not a particular HUD. This
    // also runs with the original interface or with every panel hidden.
    if(!self.dead && !self.flight.active && !self.transportEntry && !characterIntroOwnsView()) {
        const auto portals=localRealm_->availablePortals();
        const uint32_t inside=portals.empty()?0:portals.front().id;
        if(inside!=localRealmPortalDwellId_) {localRealmPortalDwellId_=inside;localRealmPortalDwell_=0;}
        else localRealmPortalDwell_+=std::max(0.f,deltaTime);
        if(inside && self.portalCooldown<=0 && localRealmPortalDwell_>=0) {
            // A failed preflight is retried after one second, never every frame.
            if(!enterLocalRealmPortal(inside,false)) localRealmPortalDwell_=-1.f;
        }
    }
}

/// Put the local realm's zeppelins and ships on screen.
///
/// TransportManager already knows how to place, animate and render a moving
/// hull - it does exactly that in online play for every ship and zeppelin a
/// real server sends. It had simply never been fed by anything other than
/// SMSG_UPDATE_OBJECT, so the standalone realm's harbours stood empty.
///
/// The route arithmetic lives in LocalTravelNetwork, which derives it from the
/// player's own TaxiPath data. Here a hull is spawned once and then pushed
/// through updateServerTransport every frame - the same call the packet path
/// makes, so the animation, the deck collision and the passenger transform all
/// behave as they do online rather than through a second implementation that
/// would have to be kept in step with them.
/// Read the taxi network out of the player's client DBCs into the local realm.
///
/// The same three files MovementHandler parses for online play. Field indices
/// come from the active expansion's layout when there is one and fall back to
/// the WotLK positions otherwise, exactly as loadTaxiDbc does - a 1.12 client
/// lays TaxiNodes out differently, and reading it as WotLK would place every
/// flight master in the sea.
///
/// Not fatal on failure. Without taxi data there are simply no flights and no
/// ships; the rest of the realm is untouched.
void Application::loadLocalTravelNetwork() {
    if (!localRealm_ || !assetManager) return;

    const auto* layout = pipeline::getActiveDBCLayout();
    // The layout's answer when it has one, and the documented 3.3.5a column
    // otherwise - per field, not per table.
    //
    // This used to fall back only when the whole table was missing, and the
    // installed dbc_layouts.json carries a Map entry with exactly two fields in
    // it: ID and InternalName. So the table was found, every other name came
    // back as tryField's 0xFFFFFFFF sentinel, and importClientMaps refused the
    // whole file for being narrower than a column index of four billion. The
    // log said "client maps=0" and every dungeon and raid entrance in the game
    // quietly went back to depending on the world catalog alone.
    const auto field = [&](const char* table, const char* name, uint32_t fallback) {
        const auto* l = layout ? layout->getLayout(table) : nullptr;
        if (!l) return fallback;
        const uint32_t found = l->tryField(name);
        return found == 0xFFFFFFFFu ? fallback : found;
    };

    // Which maps the client itself calls a dungeon or a raid. The catalog only
    // knows the maps AzerothCore's instance_template dump happened to carry;
    // Map.dbc covers every map this client can load, which is what makes an
    // entrance usable on the client's own authority rather than by luck.
    if (const auto dbc = assetManager->loadDBC("Map.dbc"); dbc && dbc->isLoaded()) {
        auto maps = game::importClientMaps(dbc.get(),
            field("Map", "ID", game::kMapDbcFieldId),
            field("Map", "InstanceType", game::kMapDbcFieldInstanceType),
            field("Map", "MapName", game::kMapDbcFieldName),
            field("Map", "InternalName", game::kMapDbcFieldDirectory),
            field("Map", "ExpansionID", game::kMapDbcFieldExpansion),
            field("Map", "MaxPlayers", game::kMapDbcFieldMaxPlayers));
        const auto count = maps.size();
        if (count && !localRealm_->setClientMaps(std::move(maps)))
            LOG_WARNING("[LOCAL_WORLD] Map.dbc rejected: ", localRealm_->error());
        else LOG_INFO("[LOCAL_WORLD] client maps=", count);
    }
    // SkillLine.dbc names the professions a trainer can teach. Without it the
    // documented built-in set stands in, so this is not fatal either.
    std::vector<game::LocalSkillLine> skills;
    if (const auto dbc = assetManager->loadDBC("SkillLine.dbc"); dbc && dbc->isLoaded()) {
        const uint32_t idF = field("SkillLine", "ID", 0), catF = field("SkillLine", "Category", 1);
        const uint32_t nameF = field("SkillLine", "Name", 3);
        for (uint32_t row = 0; row < dbc->getRecordCount(); ++row) {
            game::LocalSkillLine line;
            line.id = dbc->getUInt32(row, idF);
            if (!line.id) continue;
            line.category = dbc->getUInt32(row, catF);
            if (dbc->getFieldCount() > nameF) line.name = dbc->getString(row, nameF);
            skills.push_back(std::move(line));
        }
    }
    if (!skills.empty() && !localRealm_->setSkillLines(skills))
        LOG_WARNING("[LOCAL_WORLD] SkillLine.dbc rejected: ", localRealm_->error());
    LOG_INFO("[LOCAL_WORLD] client skill lines=", localRealm_->skillLines().size());

    std::vector<game::LocalTaxiNode> nodes;
    if (const auto dbc = assetManager->loadDBC("TaxiNodes.dbc"); dbc && dbc->isLoaded()) {
        const uint32_t idF = field("TaxiNodes", "ID", 0);
        const uint32_t mapF = field("TaxiNodes", "MapID", 1);
        const uint32_t xF = field("TaxiNodes", "X", 2);
        const uint32_t yF = field("TaxiNodes", "Y", 3);
        const uint32_t zF = field("TaxiNodes", "Z", 4);
        const uint32_t nameF = field("TaxiNodes", "Name", 5);
        const uint32_t mountAF = field("TaxiNodes", "MountDisplayIdAlliance", 22);
        const uint32_t mountHF = field("TaxiNodes", "MountDisplayIdHorde", 23);
        const uint32_t fields = dbc->getFieldCount();
        nodes.reserve(dbc->getRecordCount());
        for (uint32_t row = 0; row < dbc->getRecordCount(); ++row) {
            game::LocalTaxiNode node;
            node.id = dbc->getUInt32(row, idF);
            if (!node.id) continue;
            node.mapId = dbc->getUInt32(row, mapF);
            node.x = dbc->getFloat(row, xF);
            node.y = dbc->getFloat(row, yF);
            node.z = dbc->getFloat(row, zF);
            node.name = dbc->getString(row, nameF);
            // A node with no mount for either faction is a boat, zeppelin or
            // tram stop rather than a flight point. Reading past the record
            // would answer with a name locale, so check the width first.
            if (fields > mountHF) {
                node.mountAlliance = dbc->getUInt32(row, mountAF);
                node.mountHorde = dbc->getUInt32(row, mountHF);
            }
            nodes.push_back(std::move(node));
        }
    }

    std::vector<game::LocalTaxiPath> paths;
    if (const auto dbc = assetManager->loadDBC("TaxiPath.dbc"); dbc && dbc->isLoaded()) {
        const uint32_t idF = field("TaxiPath", "ID", 0);
        const uint32_t fromF = field("TaxiPath", "FromNode", 1);
        const uint32_t toF = field("TaxiPath", "ToNode", 2);
        const uint32_t costF = field("TaxiPath", "Cost", 3);
        paths.reserve(dbc->getRecordCount());
        for (uint32_t row = 0; row < dbc->getRecordCount(); ++row) {
            game::LocalTaxiPath path;
            path.id = dbc->getUInt32(row, idF);
            if (!path.id) continue;
            path.fromNode = dbc->getUInt32(row, fromF);
            path.toNode = dbc->getUInt32(row, toF);
            path.cost = dbc->getUInt32(row, costF);
            paths.push_back(path);
        }
    }

    std::vector<game::LocalTaxiWaypoint> waypoints;
    if (const auto dbc = assetManager->loadDBC("TaxiPathNode.dbc"); dbc && dbc->isLoaded()) {
        const uint32_t pathF = field("TaxiPathNode", "PathID", 1);
        const uint32_t indexF = field("TaxiPathNode", "NodeIndex", 2);
        const uint32_t mapF = field("TaxiPathNode", "MapID", 3);
        const uint32_t xF = field("TaxiPathNode", "X", 4);
        const uint32_t yF = field("TaxiPathNode", "Y", 5);
        const uint32_t zF = field("TaxiPathNode", "Z", 6);
        waypoints.reserve(dbc->getRecordCount());
        for (uint32_t row = 0; row < dbc->getRecordCount(); ++row) {
            game::LocalTaxiWaypoint point;
            point.pathId = dbc->getUInt32(row, pathF);
            if (!point.pathId) continue;
            point.index = dbc->getUInt32(row, indexF);
            point.mapId = dbc->getUInt32(row, mapF);
            point.x = dbc->getFloat(row, xF);
            point.y = dbc->getFloat(row, yF);
            point.z = dbc->getFloat(row, zF);
            point.flags = dbc->getUInt32(row, field("TaxiPathNode", "Flags", 7));
            point.delaySeconds = dbc->getUInt32(row, field("TaxiPathNode", "Delay", 8));
            point.arrivalEvent = dbc->getUInt32(row, field("TaxiPathNode", "ArrivalEventID", 9));
            point.departureEvent = dbc->getUInt32(row, field("TaxiPathNode", "DepartureEventID", 10));
            waypoints.push_back(point);
        }
    }

    if (nodes.empty() || paths.empty()) {
        LOG_WARNING("[LOCAL_WORLD] No taxi data in the client archives; "
                    "flight masters and transports are unavailable "
                    "(nodes=", nodes.size(), " paths=", paths.size(), ")");
        return;
    }

    const size_t nodeCount = nodes.size(), pathCount = paths.size(), pointCount = waypoints.size();
    if (!localRealm_->setTravelNetwork(std::move(nodes), std::move(paths),
                                       std::move(waypoints))) {
        LOG_WARNING("[LOCAL_WORLD] Taxi data rejected: ", localRealm_->status());
        return;
    }
    LOG_INFO("[LOCAL_WORLD] taxi network nodes=", nodeCount, " paths=", pathCount,
             " waypoints=", pointCount, " transports=",
             localRealm_->travel().transportRoutes().size());
    // What the player's own client says the transport routes are, beside what
    // the built-in table offered. The two disagreeing is the difference
    // between a zeppelin that docks at its tower and one that does not.
    localRealm_->travel().logClientTransportPaths();
}

namespace {
/// The hull model a transport entry uses, from the route the local realm
/// accepted for it.
///
/// The display id is what resolves to a WMO or an M2 in the user's own MPQs,
/// and it is the same number transport_path_repository already recognises as a
/// ship or a zeppelin - so a locally spawned hull reaches the renderer exactly
/// as a server-spawned one does. Zero when no accepted route claims the entry,
/// which is how a transport the client's data cannot support quietly does not
/// appear rather than spawning as an untextured box.
uint32_t transportDisplayForEntry(const game::LocalTravelNetwork& travel, uint32_t entry) {
    for (const auto& route : travel.transportRoutes()) {
        if (route.entry == entry) return route.displayId;
    }
    return 0;
}
} // namespace

void Application::syncLocalRealmTransports(const game::LocalRealmPlayer& self) {
    if (!localRealm_ || !gameHandler || !entitySpawner_) return;
    auto* transportManager = gameHandler->getTransportManager();
    if (!transportManager) return;

    auto& present = localRealmPresentScratch_;
    present.clear();
    if(!self.instanceId && assetManager){
        const uint32_t mailboxDisplay = entitySpawner_->localMailboxDisplayId();
        if(mailboxDisplay)for(const auto& m:game::localMailboxSites(localRealm_->content(),self)){
            if(m.mapId!=self.mapId || std::hypot(m.x-self.x,m.y-self.y)>120.f)continue;
            present.insert(m.guid);
            if(!entitySpawner_->isGameObjectSpawned(m.guid)){
                const auto pos=coords::serverToCanonical(glm::vec3(m.x,m.y,m.z));
                entitySpawner_->queueGameObjectSpawn(m.guid,142075,mailboxDisplay,pos.x,pos.y,pos.z,coords::serverToCanonicalYaw(m.orientation),1.f);
                // queueGameObjectSpawn coalesces pending requests; report each site once.
                if(!localRealmTransportGuids_.count(m.guid))
                    LOG_INFO("[LOCAL_MAILBOX] queued guid=",m.guid," map=",m.mapId," position=",m.x,",",m.y,",",m.z," display=",mailboxDisplay);
            }
        }
    }
    // A transport belongs to the open world, so a player inside an instance
    // sees none - and every hull is retired below rather than left floating in
    // a dungeon that has no harbour.
    if (self.instanceId == 0) {
        for (const auto& hull : localRealm_->transports()) {
            if (hull.mapId != gameHandler->getCurrentMapId()) continue;
            if (self.transportEntry!=hull.entry && std::hypot(hull.x-self.x,hull.y-self.y)>1800.f) continue;

            const uint32_t displayId =
                transportDisplayForEntry(localRealm_->travel(), hull.entry);
            if (!displayId) continue;

            // MO_TRANSPORT's high guid, as the original client uses it, with
            // the gameobject entry below. Stable across frames and restarts,
            // so a hull keeps its identity when a player walks away and back.
            const uint64_t guid = 0xf1c0000000000000ULL | uint64_t(hull.entry);
            present.insert(guid);

            const auto canonical =
                coords::serverToCanonical(glm::vec3(hull.x, hull.y, hull.z));
            // The hull's facing, derived exactly as TransportAnimator derives
            // it for a server-driven transport on a TaxiPathNode route.
            //
            // This used to be serverToCanonicalYaw(hull.orientation), which is
            // the conversion for a *character's* facing and is wrong for a
            // transport by a constant 270 degrees: the animator's route yaw is
            // atan2 of the canonical tangent plus the hull's bow offset, and
            // every transport model in the data is authored bow-at--X so that
            // offset is PI. serverToCanonicalYaw instead subtracts PI/2. The
            // two differ by 3*PI/2, which is precisely a zeppelin flying
            // sideways and backwards along its route.
            //
            // updateServerTransport treats this client as the server here, and
            // the animator's own comment records that server yaw wins while the
            // server is also driving position - so the corrected route yaw has
            // to be computed here rather than left for the animator to fix.
            //
            // serverToCanonical swaps X and Y, so atan2(canonicalTangent.x,
            // canonicalTangent.y) is atan2(serverDy, serverDx) - which is what
            // LocalTravelNetwork::samplePath already returns as orientation.
            const float yaw = coords::normalizeAngleRad(
                hull.orientation + game::TransportManager::transportModelBowOffset(displayId));

            if (!entitySpawner_->isGameObjectSpawned(guid)) {
                // The render instance first: registration needs to know
                // whether the model resolved to a WMO or an M2, and only the
                // spawn can answer that.
                entitySpawner_->queueGameObjectSpawn(guid, hull.entry, displayId,
                                                     canonical.x, canonical.y,
                                                     canonical.z, yaw, 1.0f);
            }
            if (!transportManager->getTransport(guid) &&
                !entitySpawner_->hasTransportRegistrationPending(guid)) {
                entitySpawner_->queueTransportRegistration(guid, hull.entry, displayId,
                                                           canonical.x, canonical.y,
                                                           canonical.z, yaw);
            }

            // Where the hull is now. Before registration completes this is
            // held as a pending move and replayed - which is what the network
            // path does with a position that arrives ahead of its spawn.
            if (transportManager->getTransport(guid)) {
                transportManager->updateLocalTransportPose(
                    guid, glm::vec3(canonical.x, canonical.y, canonical.z), yaw, hull.docked);
                glm::vec3 restored;
                if(gameHandler->completePlayerTransportWorldTransfer(guid, restored) && renderer) {
                    const auto renderPosition=coords::canonicalToRender(restored);
                    renderer->getCharacterPosition()=renderPosition;
                    if(auto* camera=renderer->getCameraController()) {
                        camera->teleportTo(renderPosition);camera->suspendGravityFor(2.f);
                    }
                }
            } else {
                entitySpawner_->setTransportPendingMove(guid, canonical.x, canonical.y,
                                                        canonical.z, yaw);
            }
        }
    }

    for (auto guid : localRealmTransportGuids_) {
        if (present.count(guid)) continue;
        // Leaving the map, or entering an instance, retires the hull. Its
        // route is untouched: the schedule is a function of world time, so the
        // same transport reappears where it should when the player returns.
        entitySpawner_->despawnGameObject(guid);
    }
    localRealmTransportGuids_.swap(present);
    gameHandler->syncLocalTransportPassengers();
}

} // namespace wowee::core
