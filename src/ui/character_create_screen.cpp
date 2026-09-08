#include "ui/character_create_screen.hpp"
#include "core/logger.hpp"
#include "ui/ui_colors.hpp"
#include "rendering/character_preview.hpp"
#include "rendering/renderer.hpp"
#include "core/application.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/dbc_layout.hpp"
#include "pipeline/character_start_outfit.hpp"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <random>
#ifdef WOWEE_PS4
#include "platform/ps4/input_ps4.hpp"
#include <orbis/Pad.h>
#endif

namespace wowee {
namespace ui {

// Full WotLK race/class lists (used as defaults when no expansion constraints set)
static constexpr game::Race kAllRaces[] = {
    // Alliance
    game::Race::HUMAN, game::Race::DWARF, game::Race::NIGHT_ELF,
    game::Race::GNOME, game::Race::DRAENEI,
    // Horde
    game::Race::ORC, game::Race::UNDEAD, game::Race::TAUREN,
    game::Race::TROLL, game::Race::BLOOD_ELF,
};
static constexpr int kAllRaceCount = 10;
static constexpr int kAllianceCount = 5;

static constexpr game::Class kAllClasses[] = {
    game::Class::WARRIOR, game::Class::PALADIN, game::Class::HUNTER,
    game::Class::ROGUE, game::Class::PRIEST, game::Class::DEATH_KNIGHT,
    game::Class::SHAMAN, game::Class::MAGE, game::Class::WARLOCK,
    game::Class::DRUID,
};

namespace {

uint8_t selectedAppearanceId(const std::vector<uint8_t>& ids, int index) {
    if (!ids.empty() && index >= 0 && index < static_cast<int>(ids.size())) {
        return ids[static_cast<size_t>(index)];
    }
    return static_cast<uint8_t>(std::max(index, 0));
}

void sortUnique(std::vector<uint8_t>& ids) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
}

} // namespace


CharacterCreateScreen::CharacterCreateScreen() {
    reset();
}

CharacterCreateScreen::~CharacterCreateScreen() {
#ifdef WOWEE_PS4
    if (nameKeyboard_.open) platform::ps4::setApplicationKeyboardOpen(false);
#endif
}

void CharacterCreateScreen::setExpansionConstraints(
        const std::vector<uint32_t>& races, const std::vector<uint32_t>& classes) {
    // Build filtered race list: alliance first, then horde
    availableRaces_.clear();
    expansionClasses_.clear();

    if (!races.empty()) {
        // Alliance races in display order
        for (auto r : std::initializer_list<game::Race>{
                game::Race::HUMAN, game::Race::DWARF, game::Race::NIGHT_ELF,
                game::Race::GNOME, game::Race::DRAENEI}) {
            if (std::find(races.begin(), races.end(), static_cast<uint32_t>(r)) != races.end()) {
                availableRaces_.push_back(r);
            }
        }
        allianceRaceCount_ = static_cast<int>(availableRaces_.size());

        // Horde races in display order
        for (auto r : std::initializer_list<game::Race>{
                game::Race::ORC, game::Race::UNDEAD, game::Race::TAUREN,
                game::Race::TROLL, game::Race::BLOOD_ELF}) {
            if (std::find(races.begin(), races.end(), static_cast<uint32_t>(r)) != races.end()) {
                availableRaces_.push_back(r);
            }
        }
    }

    if (!classes.empty()) {
        for (auto cls : kAllClasses) {
            if (std::find(classes.begin(), classes.end(), static_cast<uint32_t>(cls)) != classes.end()) {
                expansionClasses_.push_back(cls);
            }
        }
    }

    // If no constraints provided, fall back to WotLK defaults
    if (availableRaces_.empty()) {
        availableRaces_.assign(kAllRaces, kAllRaces + kAllRaceCount);
        allianceRaceCount_ = kAllianceCount;
    }

    raceIndex = 0;
    classIndex = 0;
    updateAvailableClasses();
}

void CharacterCreateScreen::reset() {
    name_.clear();
    nameKeyboard_.open = false;
    nameWasFocused_ = false;
#ifdef WOWEE_PS4
    platform::ps4::setApplicationKeyboardOpen(false);
#endif
    raceIndex = 0;
    classIndex = 0;
    genderIndex = 0;
    bodyTypeIndex = 0;
    skin = 0;
    face = 0;
    hairStyle = 0;
    hairColor = 0;
    facialHair = 0;
    // The same limits game::getMax* answers, asked rather than repeated: this
    // is the fallback before the DBC scan has run, and the two have to agree
    // or the sliders offer a face the scan will not produce.
    const game::Race race = availableRaces_.empty()
        ? game::Race::HUMAN : availableRaces_[0];
    const game::Gender gender = game::Gender::MALE;
    maxSkin = game::getMaxSkin(race, gender);
    maxFace = game::getMaxFace(race, gender);
    maxHairStyle = game::getMaxHairStyle(race, gender);
    maxHairColor = game::getMaxHairColor(race, gender);
    maxFacialHair = game::getMaxFacialFeature(race, gender);
    statusMessage.clear();
    statusIsError = false;
    createTimer_ = -1.0f;
    pendingCreate_ = false;
    appearanceValid_ = false;
    previewError_.clear();
    previewInitAttempted_ = false;
    prevClassIndex_ = -1;
    skinIds_.clear();
    faceIds_.clear();
    hairStyleIds_.clear();
    hairColorIds_.clear();
    facialHairIds_.clear();

    // Populate default races if not yet set by setExpansionConstraints
    if (availableRaces_.empty()) {
        availableRaces_.assign(kAllRaces, kAllRaces + kAllRaceCount);
        allianceRaceCount_ = kAllianceCount;
    }

    updateAvailableClasses();

    // Reset preview tracking to force model reload on next render
    prevRaceIndex_ = -1;
    prevGenderIndex_ = -1;
    prevBodyTypeIndex_ = -1;
    prevSkin_ = -1;
    prevFace_ = -1;
    prevHairStyle_ = -1;
    prevHairColor_ = -1;
    prevFacialHair_ = -1;
    prevRangeRace_ = -1;
    prevRangeGender_ = -1;
    prevRangeBodyType_ = -1;
    prevRangeSkin_ = -1;
    prevRangeHairStyle_ = -1;
}

void CharacterCreateScreen::initializePreview(pipeline::AssetManager* am) {
    // State transitions can happen while a frame is being recorded. Binding
    // the source here is safe; allocation and uploads belong to prepareScene.
    if (assetManager_ != am) {
        assetManager_ = am;
        previewAssetReset_ = true;
        previewInitAttempted_ = false;
    }
    prevRaceIndex_ = -1;
}

void CharacterCreateScreen::releaseSceneResources() {
    const bool hadPreview = static_cast<bool>(preview_);
    preview_.reset();
    previewInitialized_ = false;
    previewInitAttempted_ = false;
    previewAssetReset_ = false;
    previewError_.clear();
    appearanceValid_ = false;
    prevClassIndex_ = -1;
    prevRaceIndex_ = -1;
    prevGenderIndex_ = -1;
    prevBodyTypeIndex_ = -1;
    prevSkin_ = -1;
    prevFace_ = -1;
    prevHairStyle_ = -1;
    prevHairColor_ = -1;
    prevFacialHair_ = -1;
    prevRangeRace_ = -1;
    prevRangeGender_ = -1;
    prevRangeBodyType_ = -1;
    prevRangeSkin_ = -1;
    prevRangeHairStyle_ = -1;
    if (hadPreview) LOG_INFO("Frontend scene released: character creation (form retained)");
}

void CharacterCreateScreen::prepareScene(game::GameHandler&, float deltaTime) {
    if (!assetManager_) assetManager_ = core::Application::getInstance().getAssetManager();
    auto* renderer = core::Application::getInstance().getRenderer();
    if (!assetManager_ || !assetManager_->isInitialized() || !renderer) return;
    glue_.ensureLoaded(assetManager_, renderer->getVkContext());
    if (previewAssetReset_) {
        preview_.reset();
        previewInitialized_ = false;
        previewAssetReset_ = false;
    }
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
            LOG_ERROR("CharacterCreateScreen: preview initialization failed");
            preview_.reset();
        } else {
            preview_->setSceneCameraMode(true);
            renderer->registerPreview(preview_.get());
        }
    }
    updateAppearanceRanges();
    updatePreviewIfNeeded();
    if (preview_) {
        preview_->update(std::clamp(deltaTime, 0.0f, 0.1f));
        preview_->render();
        preview_->requestComposite();
        if (!preview_->getLastError().empty()) previewError_ = preview_->getLastError();
    }
    if (pendingCreate_) {
        pendingCreate_ = false;
        submitCharacter();
    }
}

void CharacterCreateScreen::update(float deltaTime) {
    // Preview animation runs in prepareScene, immediately before compositing.
    if (createTimer_ >= 0.0f) {
        createTimer_ += deltaTime;
        if (createTimer_ > 10.0f) {
            createTimer_ = -1.0f;
            setStatus("Server did not respond. Try again.", true);
        }
    }
}

void CharacterCreateScreen::setStatus(const std::string& msg, bool isError) {
    statusMessage = msg;
    statusIsError = isError;
    if (isError || msg.empty()) {
        createTimer_ = -1.0f;  // Stop waiting on error/clear
    }
}

void CharacterCreateScreen::updateAvailableClasses() {
    availableClasses.clear();
    if (availableRaces_.empty() || raceIndex < 0 || raceIndex >= static_cast<int>(availableRaces_.size())) return;
    game::Race race = availableRaces_[raceIndex];
    for (auto cls : kAllClasses) {
        if (!game::isValidRaceClassCombo(race, cls)) continue;
        // If expansion constraints set, only allow listed classes
        if (!expansionClasses_.empty()) {
            if (std::find(expansionClasses_.begin(), expansionClasses_.end(), cls) == expansionClasses_.end())
                continue;
        }
        availableClasses.push_back(cls);
    }
    // Clamp class index
    if (classIndex >= static_cast<int>(availableClasses.size())) {
        classIndex = 0;
    }
}

void CharacterCreateScreen::updatePreviewIfNeeded() {
    if (!preview_ || availableRaces_.empty() || raceIndex < 0 ||
        raceIndex >= static_cast<int>(availableRaces_.size())) return;

    bool changed = (raceIndex != prevRaceIndex_ ||
                    genderIndex != prevGenderIndex_ ||
                    bodyTypeIndex != prevBodyTypeIndex_ ||
                    skin != prevSkin_ ||
                    face != prevFace_ ||
                    hairStyle != prevHairStyle_ ||
                    hairColor != prevHairColor_ ||
                    facialHair != prevFacialHair_);

    if (changed) {
        bool useFemaleModel = (genderIndex == 2 && bodyTypeIndex == 1);  // Nonbinary + Feminine
        const bool loaded = preview_->loadCharacter(
            availableRaces_[raceIndex],
            static_cast<game::Gender>(genderIndex),
            selectedAppearanceId(skinIds_, skin),
            selectedAppearanceId(faceIds_, face),
            selectedAppearanceId(hairStyleIds_, hairStyle),
            selectedAppearanceId(hairColorIds_, hairColor),
            selectedAppearanceId(facialHairIds_, facialHair),
            useFemaleModel);
        previewError_ = loaded ? "" : "Character model is missing from the client data.";
        if (loaded) applyStartingOutfit();
        prevClassIndex_ = classIndex;

        prevRaceIndex_ = raceIndex;
        prevGenderIndex_ = genderIndex;
        prevBodyTypeIndex_ = bodyTypeIndex;
        prevSkin_ = skin;
        prevFace_ = face;
        prevHairStyle_ = hairStyle;
        prevHairColor_ = hairColor;
        prevFacialHair_ = facialHair;
    } else if (prevClassIndex_ != classIndex) {
        applyStartingOutfit();
        prevClassIndex_ = classIndex;
    }
}

void CharacterCreateScreen::updateAppearanceRanges() {
    if (raceIndex == prevRangeRace_ &&
        genderIndex == prevRangeGender_ &&
        bodyTypeIndex == prevRangeBodyType_ &&
        skin == prevRangeSkin_ &&
        hairStyle == prevRangeHairStyle_) {
        return;
    }

    // The fallback ranges, before the DBC scan narrows them to what this race
    // and sex actually have art for.
    const game::Race race = raceIndex < static_cast<int>(availableRaces_.size())
        ? availableRaces_[raceIndex] : game::Race::HUMAN;
    const game::Gender gender =
        genderIndex == 0 ? game::Gender::MALE : game::Gender::FEMALE;
    maxSkin = game::getMaxSkin(race, gender);
    maxFace = game::getMaxFace(race, gender);
    maxHairStyle = game::getMaxHairStyle(race, gender);
    maxHairColor = game::getMaxHairColor(race, gender);
    maxFacialHair = game::getMaxFacialFeature(race, gender);
    skinIds_.clear();
    faceIds_.clear();
    hairStyleIds_.clear();
    hairColorIds_.clear();
    facialHairIds_.clear();

    appearanceValid_ = false;
    maxSkin = maxFace = maxHairStyle = maxHairColor = maxFacialHair = 0;
    if (!assetManager_ || availableRaces_.empty() || raceIndex < 0 ||
        raceIndex >= static_cast<int>(availableRaces_.size())) return;
    auto dbc = assetManager_->loadDBC("CharSections.dbc");
    if (!dbc) return;

    uint32_t targetRaceId = static_cast<uint32_t>(availableRaces_[raceIndex]);
    const bool useFemaleModel = genderIndex == 1 || (genderIndex == 2 && bodyTypeIndex == 1);
    uint32_t targetSexId = useFemaleModel ? 1u : 0u;

    const auto* csL = pipeline::getActiveDBCLayout() ? pipeline::getActiveDBCLayout()->getLayout("CharSections") : nullptr;
    auto csF = pipeline::detectCharSectionsFields(dbc.get(), csL);
    for (uint32_t r = 0; r < dbc->getRecordCount(); r++) {
        uint32_t raceId = dbc->getUInt32(r, csF.raceId);
        uint32_t sexId = dbc->getUInt32(r, csF.sexId);
        if (raceId != targetRaceId || sexId != targetSexId) continue;

        uint32_t baseSection = dbc->getUInt32(r, csF.baseSection);
        uint32_t variationIndex = dbc->getUInt32(r, csF.variationIndex);
        uint32_t colorIndex = dbc->getUInt32(r, csF.colorIndex);

        if (baseSection == 0 && variationIndex == 0 && colorIndex <= 255) {
            skinIds_.push_back(static_cast<uint8_t>(colorIndex));
        } else if (baseSection == 3 && variationIndex <= 255) {
            hairStyleIds_.push_back(static_cast<uint8_t>(variationIndex));
        }
    }

    sortUnique(skinIds_);
    sortUnique(hairStyleIds_);
    if (skinIds_.empty() || hairStyleIds_.empty()) return;
    maxSkin = static_cast<int>(skinIds_.size()) - 1;
    maxHairStyle = static_cast<int>(hairStyleIds_.size()) - 1;
    skin = std::clamp(skin, 0, maxSkin);
    hairStyle = std::clamp(hairStyle, 0, maxHairStyle);

    const uint8_t skinId = selectedAppearanceId(skinIds_, skin);
    const uint8_t hairStyleId = selectedAppearanceId(hairStyleIds_, hairStyle);

    for (uint32_t r = 0; r < dbc->getRecordCount(); r++) {
        uint32_t raceId = dbc->getUInt32(r, csF.raceId);
        uint32_t sexId = dbc->getUInt32(r, csF.sexId);
        if (raceId != targetRaceId || sexId != targetSexId) continue;

        uint32_t baseSection = dbc->getUInt32(r, csF.baseSection);
        uint32_t variationIndex = dbc->getUInt32(r, csF.variationIndex);
        uint32_t colorIndex = dbc->getUInt32(r, csF.colorIndex);

        if (baseSection == 1 && colorIndex == skinId && variationIndex <= 255) {
            faceIds_.push_back(static_cast<uint8_t>(variationIndex));
        } else if (baseSection == 3 && variationIndex == hairStyleId) {
            if (colorIndex <= 255) {
                hairColorIds_.push_back(static_cast<uint8_t>(colorIndex));
            }
        }
    }

    sortUnique(faceIds_);
    sortUnique(hairColorIds_);
    if (faceIds_.empty() || hairColorIds_.empty()) return;
    maxFace = static_cast<int>(faceIds_.size()) - 1;
    maxHairColor = static_cast<int>(hairColorIds_.size()) - 1;
    face = std::clamp(face, 0, maxFace);
    hairColor = std::clamp(hairColor, 0, maxHairColor);

    auto facialDbc = assetManager_->loadDBC("CharacterFacialHairStyles.dbc");
    const auto* fhL = pipeline::getActiveDBCLayout() ? pipeline::getActiveDBCLayout()->getLayout("CharacterFacialHairStyles") : nullptr;
    if (facialDbc) {
        for (uint32_t r = 0; r < facialDbc->getRecordCount(); r++) {
            uint32_t raceId = facialDbc->getUInt32(r, fhL ? (*fhL)["RaceID"] : 0);
            uint32_t sexId = facialDbc->getUInt32(r, fhL ? (*fhL)["SexID"] : 1);
            if (raceId != targetRaceId || sexId != targetSexId) continue;
            uint32_t variation = facialDbc->getUInt32(r, fhL ? (*fhL)["Variation"] : 2);
            if (variation <= 255) {
                facialHairIds_.push_back(static_cast<uint8_t>(variation));
            }
        }
    }
    sortUnique(facialHairIds_);
    // No facial-hair row means the default unmodified geosets, particularly
    // for female models. Zero is the client default, not an invented range.
    if (facialHairIds_.empty()) facialHairIds_.push_back(0);
    maxFacialHair = static_cast<int>(facialHairIds_.size()) - 1;
    facialHair = std::clamp(facialHair, 0, maxFacialHair);

    appearanceValid_ = true;
    prevRangeRace_ = raceIndex;
    prevRangeGender_ = genderIndex;
    prevRangeBodyType_ = bodyTypeIndex;
    prevRangeSkin_ = skin;
    prevRangeHairStyle_ = hairStyle;
}

namespace {

// Texture coordinates from the client GlueXML CharacterCreate race/class
// atlases. Races occupy 64px cells in a 512x256 image; classes a 256px atlas.
std::pair<ImVec2, ImVec2> raceIcon(game::Race race, bool female) {
    int column = 0, row = 0;
    switch (race) {
        case game::Race::HUMAN: column = 0; row = 0; break;
        case game::Race::DWARF: column = 1; row = 0; break;
        case game::Race::GNOME: column = 2; row = 0; break;
        case game::Race::NIGHT_ELF: column = 3; row = 0; break;
        case game::Race::DRAENEI: column = 4; row = 0; break;
        case game::Race::TAUREN: column = 0; row = 1; break;
        case game::Race::UNDEAD: column = 1; row = 1; break;
        case game::Race::TROLL: column = 2; row = 1; break;
        case game::Race::ORC: column = 3; row = 1; break;
        case game::Race::BLOOD_ELF: column = 4; row = 1; break;
        default: break;
    }
    if (female) row += 2;
    return {{column * .125f, row * .25f}, {(column + 1) * .125f, (row + 1) * .25f}};
}

std::pair<ImVec2, ImVec2> classIcon(game::Class cls) {
    int column = 0, row = 0;
    switch (cls) {
        case game::Class::WARRIOR: column = 0; row = 0; break;
        case game::Class::MAGE: column = 1; row = 0; break;
        case game::Class::ROGUE: column = 2; row = 0; break;
        case game::Class::DRUID: column = 3; row = 0; break;
        case game::Class::HUNTER: column = 0; row = 1; break;
        case game::Class::SHAMAN: column = 1; row = 1; break;
        case game::Class::PRIEST: column = 2; row = 1; break;
        case game::Class::WARLOCK: column = 3; row = 1; break;
        case game::Class::PALADIN: column = 0; row = 2; break;
        case game::Class::DEATH_KNIGHT: column = 1; row = 2; break;
        default: break;
    }
    constexpr float boundaries[] = {0.0f, .25f, .49609375f, .7421875f, .98828125f};
    return {{boundaries[column], row * .25f}, {boundaries[column + 1], (row + 1) * .25f}};
}

} // namespace

void CharacterCreateScreen::applyStartingOutfit() {
    if (!preview_ || !assetManager_ || availableClasses.empty() || availableRaces_.empty()) return;
    // Outfit presentation comes from the client DBC. It does not grant items
    // to a local realm or replace the external server's starting equipment.
    std::vector<game::EquipmentItem> outfit;
    const auto dbc = assetManager_->loadDBC("CharStartOutfit.dbc");
    const auto resolved = pipeline::resolveCharacterStartingOutfit(dbc.get(),
        static_cast<uint8_t>(availableRaces_[raceIndex]),
        static_cast<uint8_t>(availableClasses[classIndex]),
        static_cast<uint8_t>(genderIndex == 1 || (genderIndex == 2 && bodyTypeIndex == 1)));
    for (const auto& item : resolved.items)
        outfit.push_back({item.displayId, item.inventoryType, 0});
    if (!resolved.supportedLayout) {
        LOG_WARNING("CharacterCreateScreen: starting outfit DBC absent or incompatible; no preview outfit");
    }
    LOG_INFO("CharacterCreateScreen: client starting outfit race=", static_cast<int>(availableRaces_[raceIndex]),
             " class=", static_cast<int>(availableClasses[classIndex]),
             " fields=", dbc ? dbc->getFieldCount() : 0,
             " recordBytes=", dbc ? dbc->getRecordSize() : 0,
             " matched=", resolved.matched, " visibleItems=", outfit.size());
    preview_->applyEquipment(outfit);
}

void CharacterCreateScreen::submitCharacter() {
    std::string name = name_.text();
    const size_t start = name.find_first_not_of(" \t\r\n");
    const size_t end = name.find_last_not_of(" \t\r\n");
    name = start == std::string::npos ? "" : name.substr(start, end - start + 1);
    if (name.size() < 2 || name.size() > 12) {
        setStatus("Names must contain 2 to 12 letters.", true);
        ui_.focus("name");
        return;
    }
    if (availableRaces_.empty() || availableClasses.empty() || raceIndex < 0 || classIndex < 0 ||
        raceIndex >= static_cast<int>(availableRaces_.size()) ||
        classIndex >= static_cast<int>(availableClasses.size())) {
        setStatus("Choose a valid race and class.", true);
        return;
    }
    if (!appearanceValid_) {
        setStatus("Character appearance data is unavailable. Check the client MPQs.", true);
        return;
    }
    game::CharCreateData data;
    data.name = name;
    data.race = availableRaces_[static_cast<size_t>(raceIndex)];
    data.characterClass = availableClasses[static_cast<size_t>(classIndex)];
    data.gender = static_cast<game::Gender>(genderIndex);
    data.useFemaleModel = genderIndex == 2 && bodyTypeIndex == 1;
    data.skin = selectedAppearanceId(skinIds_, skin);
    data.face = selectedAppearanceId(faceIds_, face);
    data.hairStyle = selectedAppearanceId(hairStyleIds_, hairStyle);
    data.hairColor = selectedAppearanceId(hairColorIds_, hairColor);
    data.facialHair = selectedAppearanceId(facialHairIds_, facialHair);
    if (!onCreate) {
        setStatus("Character creation is unavailable.", true);
        return;
    }
    setStatus("Creating character...");
    createTimer_ = 0.0f;
    onCreate(data);
}

void CharacterCreateScreen::render(game::GameHandler&) {
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const float scale = std::max(.25f, std::min(screen.x / 1024.0f, screen.y / 768.0f));
    ui_.setWotlkSkin(&glue_);
    ui_.begin(ImGui::GetIO().DeltaTime, scale);
    ui_.setLayer(PaperLayer::Page);
    const auto px = [this](float units) { return ui_.px(units); };
    const auto& theme = ui_.theme();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(ImVec2(0, 0), screen, IM_COL32(5, 7, 12, 255));
    if (preview_ && preview_->getTextureId())
        dl->AddImage(reinterpret_cast<ImTextureID>(preview_->getTextureId()), ImVec2(0, 0), screen);
    glue_.drawLogo(dl, ImVec2(px(14), px(12)), ImVec2(px(290), px(150)));

    const float x0 = px(24), x1 = px(270);
    const float rx0 = screen.x - px(270), rx1 = screen.x - px(24);
    const float panelTop = px(164), panelBottom = screen.y - px(118);
    ui_.sheet(ImVec2(x0, panelTop), ImVec2(x1, panelBottom), false);
    ui_.sheet(ImVec2(rx0, panelTop), ImVec2(rx1, panelBottom), false);
    ui_.claimMouse(ImVec2(x0, panelTop), ImVec2(x1, panelBottom));
    ui_.claimMouse(ImVec2(rx0, panelTop), ImVec2(rx1, panelBottom));
    ui_.textCentered((x0 + x1) * .5f, panelTop + px(13), "Create Character", px(22), theme.ink, true);
    ui_.textCentered((rx0 + rx1) * .5f, panelTop + px(13), "Customize", px(22), theme.ink, true);
#ifdef WOWEE_PS4
    const bool keyboardOwnsInput = platform::ps4::keyboardCapturesInput();
#else
    constexpr bool keyboardOwnsInput = false;
#endif
    const bool waiting = createTimer_ >= 0.0f || pendingCreate_ || keyboardOwnsInput;
    if (waiting) ui_.pushInert();

    float leftY = panelTop + px(53);
    if (ui_.chip("gender.male", ImVec2(x0 + px(20), leftY), ImVec2(x0 + px(116), leftY + px(30)),
                 "Male", genderIndex == 0, theme.ink)) genderIndex = 0;
    if (ui_.chip("gender.female", ImVec2(x0 + px(130), leftY), ImVec2(x1 - px(20), leftY + px(30)),
                 "Female", genderIndex == 1, theme.ink)) genderIndex = 1;
    leftY += px(43);
    std::string hoveredName;
    const auto pickRace = [&](int index) {
        if (raceIndex == index) return;
        raceIndex = index;
        classIndex = 0;
        skin = face = hairStyle = hairColor = facialHair = 0;
        updateAvailableClasses();
    };
    const auto raceRow = [&](const char* label, int begin, int end, ImU32 color) {
        if (begin >= end) return;
        ui_.textCentered((x0 + x1) * .5f, leftY, label, px(15), color);
        leftY += px(20);
        const float icon = px(36), gap = px(6);
        const float start = (x0 + x1 - (end - begin) * (icon + gap) + gap) * .5f;
        for (int i = begin; i < end; ++i) {
            const auto race = availableRaces_[static_cast<size_t>(i)];
            const auto uv = raceIcon(race, genderIndex == 1);
            const ImVec2 a(start + (i - begin) * (icon + gap), leftY);
            const ImVec2 b(a.x + icon, a.y + icon);
            const std::string id = "race." + std::to_string(static_cast<unsigned>(race));
            if (ui_.chip(id.c_str(), a, b, "", raceIndex == i, color)) pickRace(i);
            const bool iconReady = glue_.drawAsset(dl, "Interface/Glues/CharacterCreate/UI-CharacterCreate-Races.blp",
                ImVec2(a.x + px(3), a.y + px(3)), ImVec2(b.x - px(3), b.y - px(3)), uv.first, uv.second);
            if (!iconReady) {
                const char initial[] = {game::getRaceName(race)[0], '\0'};
                ui_.textCentered((a.x + b.x) * .5f, a.y + px(9), initial, px(18), theme.ink);
            }
            if (ui_.hover(a, b)) hoveredName = game::getRaceName(race);
        }
        leftY += icon + px(13);
    };
    raceRow("Alliance", 0, std::min(allianceRaceCount_, static_cast<int>(availableRaces_.size())),
            IM_COL32(130, 177, 255, 255));
    raceRow("Horde", allianceRaceCount_, static_cast<int>(availableRaces_.size()),
            IM_COL32(241, 116, 91, 255));
    ui_.textCentered((x0 + x1) * .5f, leftY, "Class", px(17), theme.ink);
    leftY += px(24);
    for (size_t i = 0; i < availableClasses.size(); ++i) {
        const auto cls = availableClasses[i];
        const auto uv = classIcon(cls);
        const float icon = px(36), gap = px(6);
        const ImVec2 a(x0 + px(20) + (i % 5) * (icon + gap), leftY + (i / 5) * (icon + px(7)));
        const ImVec2 b(a.x + icon, a.y + icon);
        const auto color = ImGui::ColorConvertFloat4ToU32(getClassColor(static_cast<uint8_t>(cls)));
        const std::string id = "class." + std::to_string(static_cast<unsigned>(cls));
        if (ui_.chip(id.c_str(), a, b, "", classIndex == static_cast<int>(i), color)) classIndex = static_cast<int>(i);
        const bool iconReady = glue_.drawAsset(dl, "Interface/Glues/CharacterCreate/UI-CharacterCreate-Classes.blp",
            ImVec2(a.x + px(3), a.y + px(3)), ImVec2(b.x - px(3), b.y - px(3)), uv.first, uv.second);
        if (!iconReady) {
            const char initial[] = {game::getClassName(cls)[0], '\0'};
            ui_.textCentered((a.x + b.x) * .5f, a.y + px(9), initial, px(18), color);
        }
        if (ui_.hover(a, b)) hoveredName = game::getClassName(cls);
    }
    if (!availableRaces_.empty() && !availableClasses.empty()) {
        const std::string identity = std::string(game::getRaceName(availableRaces_[raceIndex])) + " " +
                                     game::getClassName(availableClasses[classIndex]);
        ui_.textCentered((x0 + x1) * .5f, panelBottom - px(52), identity.c_str(), px(17), theme.ink);
    }
    if (!hoveredName.empty())
        ui_.textCentered((x0 + x1) * .5f, panelBottom - px(28), hoveredName.c_str(), px(14), theme.inkSoft);

    const bool rangesCurrent = appearanceValid_ && raceIndex == prevRangeRace_ &&
        genderIndex == prevRangeGender_ && skin == prevRangeSkin_ && hairStyle == prevRangeHairStyle_;
    float y = panelTop + px(56);
    const auto appearanceRow = [&](const char* id, const char* label, int& value, int maxValue) {
        ui_.textCentered((rx0 + rx1) * .5f, y, label, px(15), theme.ink);
        y += px(23);
        const std::string previous = std::string(id) + ".previous", next = std::string(id) + ".next";
        if (ui_.button(previous.c_str(), ImVec2(rx0 + px(23), y), ImVec2(rx0 + px(60), y + px(26)),
                       "<", PaperUI::ButtonKind::Quiet, rangesCurrent && maxValue > 0))
            value = value > 0 ? value - 1 : maxValue;
        char choice[32];
        std::snprintf(choice, sizeof(choice), "%d / %d", value + 1, maxValue + 1);
        ui_.textCentered((rx0 + rx1) * .5f, y + px(5), rangesCurrent ? choice : "...", px(14), theme.inkSoft);
        if (ui_.button(next.c_str(), ImVec2(rx1 - px(60), y), ImVec2(rx1 - px(23), y + px(26)),
                       ">", PaperUI::ButtonKind::Quiet, rangesCurrent && maxValue > 0))
            value = value < maxValue ? value + 1 : 0;
        y += px(38);
    };
    appearanceRow("look.skin", "Skin Color", skin, maxSkin);
    appearanceRow("look.face", "Face", face, maxFace);
    appearanceRow("look.hair", "Hair Style", hairStyle, maxHairStyle);
    appearanceRow("look.color", "Hair Color", hairColor, maxHairColor);
    appearanceRow("look.feature", "Facial Feature", facialHair, maxFacialHair);
    if (ui_.button("look.random", ImVec2(rx0 + px(25), panelBottom - px(52)),
                   ImVec2(rx1 - px(25), panelBottom - px(20)), "Randomize Appearance",
                   PaperUI::ButtonKind::Quiet, rangesCurrent)) {
        // A private PRNG avoids device entropy/syscalls on the console. The
        // selected indices are mapped back to actual DBC IDs in prepareScene.
        static std::minstd_rand random{0x574f5750u};
        const auto pick = [&](int maximum) { return static_cast<int>(random() % static_cast<unsigned>(maximum + 1)); };
        skin = pick(maxSkin); face = pick(maxFace); hairStyle = pick(maxHairStyle);
        hairColor = pick(maxHairColor); facialHair = pick(maxFacialHair);
    }
    if (waiting) ui_.popInert();

    const float center = screen.x * .5f;
    ui_.textCentered(center, screen.y - px(166), "Name", px(18), theme.ink);
    PaperUI::FieldOpts fieldOptions;
    fieldOptions.placeholder = "Character name";
    if (waiting) ui_.pushInert();
    const auto field = ui_.field("name", ImVec2(center - px(116), screen.y - px(139)),
                                 ImVec2(center + px(116), screen.y - px(105)), name_, fieldOptions);
    if (waiting) ui_.popInert();
    if (field.changed && statusIsError) setStatus("");
#ifdef WOWEE_PS4
    const bool askNameKeyboard = (platform::ps4::padState().pressed & ORBIS_PAD_BUTTON_TRIANGLE) != 0;
    if (!nameKeyboard_.open && !keyboardOwnsInput && field.focused && (!nameWasFocused_ || askNameKeyboard)) {
        nameKeyboard_.begin(name_.text());
        platform::ps4::setApplicationKeyboardOpen(true);
        platform::ps4::takeKeyboardRequest();
        LOG_INFO("Character name keyboard opened (in-game, explicit Done/Cancel)");
    }
    if (!nameKeyboard_.open) nameWasFocused_ = field.focused;
#endif

    if (preview_) {
        const ImVec2 modelA(x1 + px(14), px(170)), modelB(rx0 - px(14), screen.y - px(210));
        if (!waiting && ui_.hover(modelA, modelB) && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            preview_->rotate(ImGui::GetIO().MouseDelta.x * .25f);
        if (ui_.button("rotate.left", ImVec2(center - px(90), screen.y - px(205)),
                       ImVec2(center - px(48), screen.y - px(175)), "<")) preview_->rotate(-20.0f);
        if (ui_.button("rotate.right", ImVec2(center + px(48), screen.y - px(205)),
                       ImVec2(center + px(90), screen.y - px(175)), ">")) preview_->rotate(20.0f);
    }
    bool cancel = ui_.button("create.cancel", ImVec2(screen.x - px(165), screen.y - px(66)),
        ImVec2(screen.x - px(24), screen.y - px(31)), "Back");
    const bool canCreate = !waiting && !nameKeyboard_.open && appearanceValid_ && !availableClasses.empty();
    const bool submit = ui_.button("create.go", ImVec2(center - px(124), screen.y - px(74)),
        ImVec2(center + px(124), screen.y - px(29)), (createTimer_ >= 0.0f || pendingCreate_) ? "Creating..." : "Create Character",
        PaperUI::ButtonKind::Primary, canCreate);
#ifdef WOWEE_PS4
    // Enter also comes from controller confirmation and system IME. Creating
    // a character is an explicit button action, never a letter/Done action.
    const bool enter = false;
    const bool fieldSubmit = false;
#else
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    const bool fieldSubmit = field.submitted;
#endif
    if (canCreate && (submit || enter || fieldSubmit)) pendingCreate_ = true;
    bool backKey = !keyboardOwnsInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
#ifdef WOWEE_PS4
    if (!keyboardOwnsInput)
        backKey = backKey || ((platform::ps4::padState().pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0);
#endif
    if (backKey && !ui_.popupOpen()) {
        if (ui_.wantsTextInput()) ui_.clearFocus();
        else cancel = true;
    }
    const std::string& status = statusMessage.empty() ? previewError_ : statusMessage;
    if (!status.empty())
        ui_.textCentered(center, px(32), status.c_str(), px(15), statusIsError ? theme.crayonRed : theme.ink);
#ifdef WOWEE_PS4
    ui_.text(ImVec2(px(24), screen.y - px(20)), "Right stick: cursor   Cross: choose   Triangle: keyboard   Circle: back",
             px(11), theme.inkSoft);
#endif
    ui_.end();
    renderNameKeyboard();
    if (cancel && !keyboardOwnsInput && !nameKeyboard_.open) {
        pendingCreate_ = false;
        if (onCancel) onCancel();
    }
}

void CharacterCreateScreen::renderNameKeyboard() {
#ifdef WOWEE_PS4
    if (!nameKeyboard_.open) return;
    ImGui::OpenPopup("Character name##keyboard");
    const auto display = ImGui::GetIO().DisplaySize;
    const float scale = std::clamp(display.y / 768.0f, 0.7f, 1.6f);
    ImGui::SetNextWindowPos(ImVec2(display.x*.5f, display.y*.5f), ImGuiCond_Always, ImVec2(.5f,.5f));
    ImGui::SetNextWindowSize(ImVec2(620*scale, 295*scale), ImGuiCond_Always);
    if (ImGui::BeginPopupModal("Character name##keyboard", nullptr,
                              ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextUnformatted(nameKeyboard_.draft.empty() ? "_" : nameKeyboard_.draft.c_str());
        ImGui::TextUnformatted("2-12 letters. D-pad: select   Cross: type   R2: Done   Circle: Cancel");
        ImGui::Separator();
        const bool ignoreOpeningPress = nameKeyboard_.justOpened;
        nameKeyboard_.justOpened = false;
        const auto pressed = ignoreOpeningPress ? 0u : platform::ps4::padState().pressed;
        if (pressed & ORBIS_PAD_BUTTON_LEFT) nameKeyboard_.move(-1,0);
        if (pressed & ORBIS_PAD_BUTTON_RIGHT) nameKeyboard_.move(1,0);
        if (pressed & ORBIS_PAD_BUTTON_UP) nameKeyboard_.move(0,-1);
        if (pressed & ORBIS_PAD_BUTTON_DOWN) nameKeyboard_.move(0,1);
        int key = -1;
        if (pressed & ORBIS_PAD_BUTTON_CROSS) key = nameKeyboard_.selected;
        if (pressed & ORBIS_PAD_BUTTON_SQUARE) key = 26;
        if (pressed & ORBIS_PAD_BUTTON_R2) key = 28;
        if (pressed & ORBIS_PAD_BUTTON_CIRCLE) key = 29;
        for (auto c : ImGui::GetIO().InputQueueCharacters)
            if (c < 128) nameKeyboard_.letter(static_cast<char>(c));
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) key = 26;
        // Enter chooses the highlighted key too. Only the Done key completes.
        if (!ignoreOpeningPress && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) key = nameKeyboard_.selected;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) key = 29;
        for (int i=0;i<30;++i) {
            char label[24];
            if (i<26) std::snprintf(label,sizeof(label),"%c",'A'+i);
            else std::snprintf(label,sizeof(label),"%s",i==26?"Del":i==27?"Aa":i==28?"Done":"Cancel");
            if (i%10) ImGui::SameLine();
            if (i==nameKeyboard_.selected) ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.5f,.22f,.08f,1));
            if (ImGui::Button(label,ImVec2(51*scale,45*scale))) key=i;
            if (i==nameKeyboard_.selected) ImGui::PopStyleColor();
        }
        const auto result = nameKeyboard_.choose(key);
        if (result != CharacterNameKeyboard::Result::Editing) {
            if (result == CharacterNameKeyboard::Result::Accepted) {
                name_.setText(nameKeyboard_.draft);
                setStatus("");
            }
            ui_.clearFocus();
            nameWasFocused_ = false;
            platform::ps4::setApplicationKeyboardOpen(false);
            LOG_INFO("Character name keyboard closed: ", result == CharacterNameKeyboard::Result::Accepted ? "Done" : "Cancel");
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#endif
}

} // namespace ui
} // namespace wowee
