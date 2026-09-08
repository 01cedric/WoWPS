#include "ui/character_screen.hpp"
#include "game/equipment_hash.hpp"
#include "ui/ui_colors.hpp"
#include "rendering/character_preview.hpp"
#include "rendering/renderer.hpp"
#include "pipeline/asset_manager.hpp"
#include "core/application.hpp"
#include "core/config_paths.hpp"
#include "core/logger.hpp"
#include "addons/addon_manager.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#ifdef WOWEE_PS4
#include "platform/ps4/input_ps4.hpp"
#include <orbis/Pad.h>
#endif

namespace wowee { namespace ui {

namespace {

// Written in units and drawn at a scale taken from the window, the same way
// the login card is. See paper_ui.hpp.
constexpr float kSheetWidth  = 1020.0f;
constexpr float kSheetHeight = 640.0f;
constexpr float kPad         = 34.0f;
constexpr float kTitleSize   = 40.0f;
constexpr float kNameSize    = 26.0f;
constexpr float kLabelSize   = 14.0f;
constexpr float kBodySize    = 15.5f;
constexpr float kSmallSize   = 13.0f;
constexpr float kRowHeight   = 36.0f;
constexpr float kButtonH     = 44.0f;

/// Enter is claimed for the rest of the press when it takes a character into
/// the world, so the game screen does not read the same keystroke as "open
/// chat" the moment it appears. Any distinct id will do; this one is not a
/// window's, because this screen no longer has one.
constexpr ImGuiID kCharListOwner = 0xC4A21151u;

ImVec4 classColor(uint8_t classId) { return ui::getClassColor(classId); }

} // namespace

CharacterScreen::CharacterScreen() = default;
CharacterScreen::~CharacterScreen() = default;

void CharacterScreen::setAssetManager(pipeline::AssetManager* am) {
    if (assetManager_ == am) return;
    assetManager_ = am;
    // This setter can run during a screen transition inside render(). Retire
    // the old target only in prepareScene, before recording the next frame.
    previewAssetReset_ = true;
    previewInitAttempted_ = false;
    previewGuid_ = 0;
    previewEmptyScene_ = false;
}

void CharacterScreen::synchronizeSelection(const std::vector<game::Character>& characters) {
    if (characters.empty()) {
        selectedCharacterIndex = -1;
        selectedCharacterGuid = 0;
        deleteConfirmStage = 0;
        return;
    }
    const auto findGuid = [&](uint64_t guid) {
        for (size_t i = 0; i < characters.size(); ++i)
            if (characters[i].guid == guid) return static_cast<int>(i);
        return -1;
    };
    if (!newlyCreatedCharacterName.empty()) {
        for (size_t i = 0; i < characters.size(); ++i) {
            if (characters[i].name == newlyCreatedCharacterName) {
                selectedCharacterGuid = characters[i].guid;
                newlyCreatedCharacterName.clear();
                saveLastCharacter(selectedCharacterGuid);
                break;
            }
        }
    }
    if (!restoredLastCharacter && selectedCharacterGuid == 0)
        selectedCharacterGuid = loadLastCharacter();
    selectedCharacterIndex = findGuid(selectedCharacterGuid);
    if (selectedCharacterIndex < 0) {
        // A deletion or refreshed list may remove the selected GUID. Never
        // retain a valid-looking old index that now describes another hero.
        selectedCharacterIndex = 0;
        selectedCharacterGuid = characters.front().guid;
        deleteConfirmStage = 0;
    }
    restoredLastCharacter = true;
}

void CharacterScreen::releaseSceneResources() {
    const bool hadPreview = static_cast<bool>(preview_);
    // CharacterPreview unregisters itself and retires submitted GPU work.
    preview_.reset();
    previewInitialized_ = false;
    previewInitAttempted_ = false;
    previewAssetReset_ = false;
    previewEmptyScene_ = false;
    previewGuid_ = 0;
    previewAppearanceBytes_ = 0;
    previewFacialFeatures_ = 0;
    previewUseFemaleModel_ = false;
    previewEquipHash_ = 0;
    previewError_.clear();
    if (hadPreview) LOG_INFO("Frontend scene released: character selection (selection retained)");
}

void CharacterScreen::prepareScene(game::GameHandler& gameHandler, float deltaTime) {
    if (!assetManager_) assetManager_ = core::Application::getInstance().getAssetManager();
    auto* renderer = services_.renderer ? services_.renderer
                                       : core::Application::getInstance().getRenderer();
    if (!assetManager_ || !assetManager_->isInitialized() || !renderer) return;
    glue_.ensureLoaded(assetManager_, renderer->getVkContext());
    if (previewAssetReset_) {
        preview_.reset();
        previewInitialized_ = false;
        previewAssetReset_ = false;
        previewGuid_ = 0;
    }
    const auto& characters = gameHandler.getCharacters();
    synchronizeSelection(characters);
    if (!previewInitialized_ && !previewInitAttempted_) {
        previewInitAttempted_ = true;
        preview_ = std::make_unique<rendering::CharacterPreview>();
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const float factor = std::min(1.0f, std::min(1280.0f / std::max(display.x, 1.0f),
                                                   720.0f / std::max(display.y, 1.0f)));
        previewInitialized_ = preview_->initialize(assetManager_,
            std::max(1, static_cast<int>(display.x * factor)),
            std::max(1, static_cast<int>(display.y * factor)));
        if (!previewInitialized_) {
            previewError_ = "Character scene could not be initialized.";
            LOG_ERROR("CharacterScreen: preview initialization failed");
            preview_.reset();
            return;
        }
        preview_->setSceneCameraMode(true);
        renderer->registerPreview(preview_.get());
    }
    if (!preview_) return;
    if (characters.empty()) {
        if (!previewEmptyScene_) {
            const bool ready = preview_->loadGlueScene(
                "Interface\\Glues\\Models\\UI_Human\\UI_Human.m2");
            previewError_ = ready ? "" : "Character scene is missing from the client data.";
            previewEmptyScene_ = true;
            previewGuid_ = 0;
        }
    } else {
        const auto& character = characters[static_cast<size_t>(selectedCharacterIndex)];
        const uint64_t equipHash = game::hashEquipmentAppearance(character.equipment);
        if (previewEmptyScene_ || previewGuid_ != character.guid ||
            previewAppearanceBytes_ != character.appearanceBytes ||
            previewFacialFeatures_ != character.facialFeatures ||
            previewUseFemaleModel_ != character.useFemaleModel || previewEquipHash_ != equipHash) {
            const bool ready = preview_->loadCharacter(character.race, character.gender,
                static_cast<uint8_t>(character.appearanceBytes),
                static_cast<uint8_t>(character.appearanceBytes >> 8),
                static_cast<uint8_t>(character.appearanceBytes >> 16),
                static_cast<uint8_t>(character.appearanceBytes >> 24),
                character.facialFeatures, character.useFemaleModel);
            if (ready) preview_->applyEquipment(character.equipment);
            previewError_ = ready ? "" : "Character model is missing from the client data.";
            previewGuid_ = character.guid;
            previewAppearanceBytes_ = character.appearanceBytes;
            previewFacialFeatures_ = character.facialFeatures;
            previewUseFemaleModel_ = character.useFemaleModel;
            previewEquipHash_ = equipHash;
            previewEmptyScene_ = false;
            LOG_INFO("CharacterScreen: selected scene race=", static_cast<unsigned>(character.race),
                     " class=", static_cast<unsigned>(character.characterClass),
                     " modelReady=", ready, " equipment=", character.equipment.size());
        }
    }
    preview_->update(std::clamp(deltaTime, 0.0f, 0.1f));
    preview_->render();
    preview_->requestComposite();
    if (!preview_->getLastError().empty()) previewError_ = preview_->getLastError();
}

void CharacterScreen::render(game::GameHandler& gameHandler) {
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const float scale = std::max(0.25f, std::min(screen.x / 1024.0f, screen.y / 768.0f));
    ui_.setWotlkSkin(&glue_);
    ui_.begin(ImGui::GetIO().DeltaTime, scale);
    ui_.setLayer(PaperLayer::Page);
    const PaperTheme& theme = ui_.theme();
    const auto px = [this](float units) { return ui_.px(units); };
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(ImVec2(0, 0), screen, IM_COL32(5, 7, 12, 255));
    if (preview_ && preview_->getTextureId())
        dl->AddImage(reinterpret_cast<ImTextureID>(preview_->getTextureId()), ImVec2(0, 0), screen);
    glue_.drawLogo(dl, ImVec2(px(14), px(12)), ImVec2(px(290), px(150)));

    const auto& characters = gameHandler.getCharacters();
    synchronizeSelection(characters);
    if (characters.empty()) {
        renderNotice(gameHandler, screen.x, screen.y);
        ui_.end();
        return;
    }
    const bool modalUp = deleteConfirmStage != 0 || showAddonsWindow_;
    if (modalUp) ui_.pushInert();
    const float right = screen.x - px(22);
    const float left = right - px(264);
    const float top = px(52);
    const float bottom = screen.y - px(112);
    ui_.sheet(ImVec2(left, top), ImVec2(right, bottom), false);
    ui_.claimMouse(ImVec2(left, top), ImVec2(right, bottom));
    ui_.textCentered((left + right) * 0.5f, top + px(13),
                     localRealmMode_ ? "Local Realm" : "Character Selection",
                     px(21), theme.ink, true);

    bool goBack = false, create = false, refresh = false;
    uint64_t enterGuid = 0;
    std::string enterName;
    const float listTop = top + px(78);
    const float listBottom = bottom - px(93);
    const float rowHeight = std::min(px(52), (listBottom - listTop) / 10.0f);
    if (ui_.button("realm.change", ImVec2(left + px(36), top + px(41)),
                   ImVec2(right - px(36), top + px(69)), "Change Realm")) goBack = true;
    const auto picked = ui_.list("characters", ImVec2(left + px(11), listTop),
        ImVec2(right - px(11), listBottom), static_cast<int>(characters.size()), rowHeight,
        selectedCharacterIndex, [&](int index, ImVec2 a, ImVec2 b, bool selected, bool) {
            const auto& character = characters[static_cast<size_t>(index)];
            const ImU32 nameColor = selected ? theme.ink : IM_COL32(238, 224, 174, 255);
            ui_.text(ImVec2(a.x + px(10), a.y + px(3)), character.name.c_str(), px(17), nameColor);
            char description[96];
            std::snprintf(description, sizeof(description), "Level %u %s %s", character.level,
                          game::getRaceName(character.race), game::getClassName(character.characterClass));
            ui_.text(ImVec2(a.x + px(10), a.y + px(21)), description, px(12),
                     ImGui::ColorConvertFloat4ToU32(classColor(static_cast<uint8_t>(character.characterClass))));
            std::string zone = gameHandler.getWhoAreaName(character.zoneId);
            if (!zone.empty() && b.y - a.y >= px(46))
                ui_.text(ImVec2(a.x + px(10), a.y + px(35)), zone.c_str(), px(11), theme.inkSoft);
        });
    if (picked.clicked >= 0 && picked.clicked < static_cast<int>(characters.size())) {
        selectedCharacterIndex = picked.clicked;
        selectedCharacterGuid = characters[static_cast<size_t>(picked.clicked)].guid;
        saveLastCharacter(selectedCharacterGuid);
    }
    if (picked.activated >= 0 && picked.activated < static_cast<int>(characters.size())) {
        enterGuid = characters[static_cast<size_t>(picked.activated)].guid;
        enterName = characters[static_cast<size_t>(picked.activated)].name;
    }
    if (!modalUp) {
        int direction = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) direction = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) direction = 1;
#ifdef WOWEE_PS4
        const auto& pad = platform::ps4::padState();
        if (pad.pressed & ORBIS_PAD_BUTTON_UP) direction = -1;
        if (pad.pressed & ORBIS_PAD_BUTTON_DOWN) direction = 1;
        // Keep Cross as pointer activation; it must not simultaneously enter
        // the selected hero when the pointer is over Delete or Create.
        if (pad.pressed & ORBIS_PAD_BUTTON_CIRCLE) goBack = true;
#endif
        if (direction != 0) {
            selectedCharacterIndex = std::clamp(selectedCharacterIndex + direction, 0,
                                                static_cast<int>(characters.size()) - 1);
            selectedCharacterGuid = characters[static_cast<size_t>(selectedCharacterIndex)].guid;
            saveLastCharacter(selectedCharacterGuid);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
            ImGui::SetKeyOwner(ImGuiKey_Enter, kCharListOwner, ImGuiInputFlags_LockUntilRelease);
            ImGui::SetKeyOwner(ImGuiKey_KeypadEnter, kCharListOwner, ImGuiInputFlags_LockUntilRelease);
            enterGuid = selectedCharacterGuid;
            enterName = characters[static_cast<size_t>(selectedCharacterIndex)].name;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) goBack = true;
    }
    const float controlLeft = left + px(17), controlRight = right - px(17);
    const float mid = (controlLeft + controlRight) * 0.5f;
    create = ui_.button("create", ImVec2(controlLeft, bottom - px(80)),
                        ImVec2(controlRight, bottom - px(48)), "Create New Character",
                        PaperUI::ButtonKind::Primary, characters.size() < 10);
    if (ui_.button("delete", ImVec2(controlLeft, bottom - px(40)),
                   ImVec2(mid - px(4), bottom - px(12)), "Delete Character")) {
        deleteConfirmStage = 1;
        ui_.swallowPress();
    }
    if (ui_.button("addons", ImVec2(mid + px(4), bottom - px(40)),
                   ImVec2(controlRight, bottom - px(12)), "AddOns")) {
        showAddonsWindow_ = true;
        ui_.swallowPress();
    }
    const bool disconnected = !localRealmMode_ &&
        (gameHandler.getState() == game::WorldState::DISCONNECTED ||
         gameHandler.getState() == game::WorldState::FAILED);
    const float enterX = (screen.x - px(248)) * 0.5f;
    if (ui_.button("enter", ImVec2(enterX, screen.y - px(72)),
                   ImVec2(enterX + px(248), screen.y - px(28)), "Enter World",
                   PaperUI::ButtonKind::Primary, !disconnected && !characterSelected)) {
        enterGuid = selectedCharacterGuid;
        enterName = characters[static_cast<size_t>(selectedCharacterIndex)].name;
    }
    if (ui_.button("back", ImVec2(screen.x - px(158), screen.y - px(62)),
                   ImVec2(screen.x - px(22), screen.y - px(30)), "Back")) goBack = true;
    if (!localRealmMode_ || core::Application::getInstance().lanCharacterFlowActive())
        refresh = ui_.button("refresh", ImVec2(px(22), screen.y - px(62)),
                             ImVec2(px(156), screen.y - px(30)), "Refresh");
    if (preview_ && !modalUp) {
        const ImVec2 modelMin(px(40), px(158)), modelMax(left - px(12), screen.y - px(125));
        if (ui_.hover(modelMin, modelMax) && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            preview_->rotate(ImGui::GetIO().MouseDelta.x * 0.25f);
        if (ui_.button("rotate.left", ImVec2(enterX + px(50), screen.y - px(112)),
                       ImVec2(enterX + px(92), screen.y - px(82)), "<")) preview_->rotate(-20.0f);
        if (ui_.button("rotate.right", ImVec2(enterX + px(156), screen.y - px(112)),
                       ImVec2(enterX + px(198), screen.y - px(82)), ">")) preview_->rotate(20.0f);
    }
    const std::string& notice = statusMessage.empty() ? previewError_ : statusMessage;
    if (!notice.empty())
        ui_.textCentered(screen.x * 0.5f, screen.y - px(142), notice.c_str(), px(14),
                         statusIsError || statusMessage.empty() ? theme.crayonRed : theme.ink);
#ifdef WOWEE_PS4
    ui_.text(ImVec2(px(22), screen.y - px(20)), "D-pad: select   Right stick: cursor   Cross: choose   Circle: back",
             px(11), theme.inkSoft);
#endif
    if (modalUp) ui_.popInert();
    if (deleteConfirmStage != 0)
        renderDeleteConfirm(characters[static_cast<size_t>(selectedCharacterIndex)], screen.x, screen.y);
    if (showAddonsWindow_) renderAddonsSheet(screen.x, screen.y);
    ui_.end();

    // Screen callbacks may replace the character vector, start map loading or
    // change the active screen. Nothing below keeps references into that list.
    if (deferredDeleteGuid_ != 0) {
        const uint64_t guid = std::exchange(deferredDeleteGuid_, 0);
        if (onDeleteCharacter) onDeleteCharacter(guid);
        return;
    }
    if (goBack) { if (onBack) onBack(); return; }
    if (create) { if (onCreateCharacter) onCreateCharacter(); return; }
    if (refresh && core::Application::getInstance().lanCharacterFlowActive())
        core::Application::getInstance().refreshLanCharacters();
    else if (refresh && (gameHandler.getState() == game::WorldState::READY ||
                    gameHandler.getState() == game::WorldState::CHAR_LIST_RECEIVED)) {
        gameHandler.requestCharacterList();
        setStatus("Retrieving character list...");
    }
    if (enterGuid != 0 && !disconnected && !characterSelected && !modalUp) {
        characterSelected = true;
        saveLastCharacter(enterGuid);
        setStatus("Entering world with " + enterName + "...");
        if (!localRealmMode_) gameHandler.selectCharacter(enterGuid);
        if (onCharacterSelected) onCharacterSelected(enterGuid);
    }
}

void CharacterScreen::renderNotice(game::GameHandler& gameHandler, float screenW, float screenH) {
    const auto px = [this](float v) { return ui_.px(v); };
    const auto& theme = ui_.theme();
    const auto state = gameHandler.getState();
    const bool loading = !localRealmMode_ &&
        (state == game::WorldState::READY || state == game::WorldState::CHAR_LIST_REQUESTED);
    const bool disconnected = !localRealmMode_ &&
        (state == game::WorldState::DISCONNECTED || state == game::WorldState::FAILED);
    const float x = screenW - px(286), right = screenW - px(22);
    ui_.sheet(ImVec2(x, px(52)), ImVec2(right, screenH - px(112)), false);
    ui_.textCentered((x + right) * 0.5f, px(65), localRealmMode_ ? "Local Realm" : "Character Selection",
                     px(21), theme.ink, true);
    const char* text = loading ? "Retrieving character list..." : disconnected ? "Disconnected from realm."
        : "You have no characters on this realm. Create a new character to begin your adventure.";
    ui_.wrapped(ImVec2(x + px(20), px(142)), right - x - px(40), text, px(16), theme.inkSoft);
    if (!previewError_.empty())
        ui_.wrapped(ImVec2(x + px(20), px(256)), right - x - px(40), previewError_.c_str(), px(13), theme.crayonRed);
    const bool create = ui_.button("notice.create", ImVec2(x + px(18), screenH - px(180)),
        ImVec2(right - px(18), screenH - px(144)), "Create New Character",
        PaperUI::ButtonKind::Primary, !loading && !disconnected);
    bool back = ui_.button("notice.back", ImVec2(right - px(134), screenH - px(62)),
        ImVec2(right, screenH - px(30)), "Back");
    back = back || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
#ifdef WOWEE_PS4
    back = back || ((platform::ps4::padState().pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0);
#endif
    if (back) { if (onBack) onBack(); return; }
    if (create) { if (onCreateCharacter) onCreateCharacter(); return; }
    if (!localRealmMode_ && state == game::WorldState::READY) gameHandler.requestCharacterList();
}

void CharacterScreen::renderDeleteConfirm(const game::Character& character, float screenW,
                                          float screenH) {
    const PaperTheme& theme = ui_.theme();
    const auto px = [this](float units) { return ui_.px(units); };

    ui_.setLayer(PaperLayer::Overlay);
    ui_.scrim(0.5f);

    const bool final = (deleteConfirmStage == 2);
    const float w = std::min(px(480), screenW - px(40));
    const float h = px(final ? 270.0f : 250.0f);
    const ImVec2 a((screenW - w) * 0.5f, (screenH - h) * 0.5f);
    const ImVec2 b(a.x + w, a.y + h);
    ui_.sheet(a, b, /*taped=*/false);
    ui_.claimMouse(a, b);

    Column col{a.x + px(28), b.x - px(28), a.y + px(28)};

    char line[192];
    if (final) {
        ui_.text(col.at(), "This cannot be undone", px(26), theme.crayonRed,
                 /*titleFace=*/true);
        col.gap(px(30));
        std::snprintf(line, sizeof(line),
                      "%s will be gone for good. Are you certain?", character.name.c_str());
    } else {
        ui_.text(col.at(), "Delete Character", px(26), theme.ink, /*titleFace=*/true);
        col.gap(px(30));
        std::snprintf(line, sizeof(line), "%s, level %d %s %s.", character.name.c_str(),
                      character.level, game::getRaceName(character.race),
                      game::getClassName(character.characterClass));
    }
    col.gap(ui_.wrapped(col.at(), col.width(), line, px(kBodySize), theme.inkSoft) + px(10));

    const float y = b.y - px(28) - px(kButtonH);
    const float bh = px(kButtonH);
    if (ui_.button("del.cancel", ImVec2(col.x0, y), ImVec2(col.x0 + px(110), y + bh),
                   "Cancel")) {
        deleteConfirmStage = 0;
    }
    const float confirmW = px(final ? 200.0f : 170.0f);
    if (ui_.button("del.confirm", ImVec2(col.x1 - confirmW, y), ImVec2(col.x1, y + bh),
                   final ? "Delete for good" : "Yes, delete",
                   PaperUI::ButtonKind::Primary)) {
        if (final) {
            deferredDeleteGuid_ = character.guid;
            deleteConfirmStage = 0;
            selectedCharacterIndex = -1;
            selectedCharacterGuid = 0;
        } else {
            deleteConfirmStage = 2;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) deleteConfirmStage = 0;

    ui_.setLayer(PaperLayer::Page);
}

void CharacterScreen::setStatus(const std::string& message, bool isError) {
    statusMessage = message;
    statusIsError = isError;
}

void CharacterScreen::renderAddonsSheet(float screenW, float screenH) {
    const PaperTheme& theme = ui_.theme();
    const auto px = [this](float units) { return ui_.px(units); };

    ui_.setLayer(PaperLayer::Overlay);
    ui_.scrim(0.45f);

    const float w = std::min(px(560), screenW - px(40));
    const float h = std::min(px(520), screenH - px(40));
    const ImVec2 a((screenW - w) * 0.5f, (screenH - h) * 0.5f);
    const ImVec2 b(a.x + w, a.y + h);
    ui_.sheet(a, b, /*taped=*/false);
    ui_.claimMouse(a, b);

    Column col{a.x + px(28), b.x - px(28), a.y + px(28)};
    const float smallSize = px(kSmallSize);

    ui_.text(col.at(), "AddOns", px(30), theme.ink, /*titleFace=*/true);
    {
        const float r = smallSize * 0.95f;
        if (ui_.glyphButton("addons.close", ImVec2(col.x1 - r, col.y + r), r,
                            PaperUI::Glyph::Cross)) {
            showAddonsWindow_ = false;
        }
    }
    col.gap(px(34));

    auto* am = services_.addonManager;
    if (!am) {
        ui_.text(col.at(), "The addon system is not running.", px(kBodySize), theme.pencil);
        ui_.setLayer(PaperLayer::Page);
        return;
    }

    const auto& addons = am->getAddons();
    ui_.text(col.at(), "Enabled addons load when you enter the world, or on /reload.",
             smallSize, theme.pencil);
    col.gap(ui_.lineHeight(smallSize) + px(8));
    ui_.rule(ImVec2(col.x0, col.y), ImVec2(col.x1, col.y), paperFade(theme.ink, 0.45f),
             px(1.2f), 0x7712u);
    col.gap(px(8));

    const float listBottom = b.y - px(28);
    if (addons.empty()) {
        col.gap(ui_.wrapped(col.at(), col.width(),
                            "None installed. Put addon folders under interface/AddOns/ in your "
                            "data path, then restart the client.",
                            px(kBodySize), theme.pencil));
        ui_.setLayer(PaperLayer::Page);
        return;
    }

    const float rowH = px(38);
    const PaperUI::ListResult picked = ui_.list(
        "addons", ImVec2(col.x0, col.y), ImVec2(col.x1, listBottom),
        static_cast<int>(addons.size()), rowH, -1,
        [&](int index, ImVec2 rowA, ImVec2 rowB, bool, bool) {
            const auto& addon = addons[static_cast<size_t>(index)];
            const bool enabled = am->isAddonEnabled(addon.addonName);
            const float boxSize = px(17);
            const float cy = (rowA.y + rowB.y) * 0.5f;

            // The checkbox is drawn here rather than left to the row's click,
            // so an addon can be turned on without being pointed at exactly.
            bool value = enabled;
            if (ui_.checkbox(addon.addonName.c_str(),
                             ImVec2(rowA.x + px(8), cy - boxSize * 0.5f), boxSize, nullptr,
                             &value)) {
                am->setAddonEnabled(addon.addonName, value);
            }

            const float textX = rowA.x + px(8) + boxSize + px(12);
            const std::string title = addon.getTitle();
            ui_.text(ImVec2(textX, rowA.y + px(4)), title.c_str(), px(kBodySize),
                     enabled ? theme.ink : theme.pencil);

            std::string sub;
            if (auto it = addon.directives.find("Version");
                it != addon.directives.end() && !it->second.empty()) {
                sub = "v" + it->second;
            }
            if (auto it = addon.directives.find("Author");
                it != addon.directives.end() && !it->second.empty()) {
                if (!sub.empty()) sub += "  ";
                sub += "by " + it->second;
            }
            if (!sub.empty()) {
                ui_.text(ImVec2(textX, rowA.y + px(4) + px(kBodySize) * 1.15f), sub.c_str(),
                         smallSize, theme.pencil);
            }
        });
    (void)picked;

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) showAddonsWindow_ = false;
    ui_.setLayer(PaperLayer::Page);
}

void CharacterScreen::selectCharacterByName(const std::string& name) {
    newlyCreatedCharacterName = name;
    restoredLastCharacter = false;  // Allow re-selection in render()
    selectedCharacterIndex = -1;
}

ImVec4 CharacterScreen::getFactionColor(game::Race race) const {
    // Alliance races: blue
    if (race == game::Race::HUMAN ||
        race == game::Race::DWARF ||
        race == game::Race::NIGHT_ELF ||
        race == game::Race::GNOME ||
        race == game::Race::DRAENEI) {
        return ImVec4(0.3f, 0.5f, 1.0f, 1.0f);
    }

    // Horde races: red
    if (race == game::Race::ORC ||
        race == game::Race::UNDEAD ||
        race == game::Race::TAUREN ||
        race == game::Race::TROLL ||
        race == game::Race::BLOOD_ELF) {
        return ui::colors::kRed;
    }

    return ui::colors::kWhite;
}

std::string CharacterScreen::getConfigDir() {
    return core::getConfigRoot();
}

void CharacterScreen::saveLastCharacter(uint64_t guid) {
    std::string dir = getConfigDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        LOG_WARNING("CharacterScreen: cannot save last selection: ", ec.message());
        return;
    }
    std::ofstream f(dir + "/last_character.cfg");
    if (f) f << guid;
}

uint64_t CharacterScreen::loadLastCharacter() {
    std::string path = getConfigDir() + "/last_character.cfg";
    std::ifstream f(path);
    uint64_t guid = 0;
    if (f) f >> guid;
    return guid;
}

}} // namespace wowee::ui
