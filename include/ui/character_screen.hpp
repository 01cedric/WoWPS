#pragma once

#include "ui/paper_ui.hpp"
#include "ui/wotlk_glue.hpp"
#include "ui/ui_services.hpp"
#include "game/game_handler.hpp"
#include <imgui.h>
#include <string>
#include <functional>
#include <memory>
#include <utility>

namespace wowee {
namespace pipeline { class AssetManager; }
namespace rendering { class CharacterPreview; }
namespace ui {

/**
 * Character selection screen UI
 *
 * Displays character list and allows user to select one to play
 */
class CharacterScreen {
public:
    CharacterScreen();
    ~CharacterScreen();

    /// Load/upload scene resources before Renderer::beginFrame records draws.
    void prepareScene(game::GameHandler& gameHandler, float deltaTime);
    void releaseSceneResources(); // Before Vulkan frame recording only.

    /**
     * Render the UI
     * @param gameHandler Reference to game handler
     */
    void render(game::GameHandler& gameHandler);

    void setAssetManager(pipeline::AssetManager* am);

    /**
     * Set callback for character selection
     * @param callback Function to call when character is selected (receives character GUID)
     */
    void setOnCharacterSelected(std::function<void(uint64_t)> callback) {
        onCharacterSelected = std::move(callback);
    }

    void setOnCreateCharacter(std::function<void()> cb) { onCreateCharacter = std::move(cb); }
    /// The list is this console's saved local-realm characters rather than a
    /// realm's answer: no connection state to wait for or to refresh.
    void setLocalRealmMode(bool on) { localRealmMode_ = on; }
    [[nodiscard]] bool localRealmMode() const { return localRealmMode_; }
    void setOnBack(std::function<void()> cb) { onBack = std::move(cb); }
    void setOnDeleteCharacter(std::function<void(uint64_t)> cb) { onDeleteCharacter = std::move(cb); }

    /// Set services (dependency injection)
    void setServices(const UIServices& services) { services_ = services; }

    /**
     * Reset selection state (e.g., when switching servers)
     */
    void reset() {
        selectedCharacterIndex = -1;
        characterSelected = false;
        selectedCharacterGuid = 0;
        restoredLastCharacter = false;
        newlyCreatedCharacterName.clear();
        statusMessage.clear();
        statusIsError = false;
        deleteConfirmStage = 0;
        previewInitAttempted_ = false;
        previewGuid_ = 0;
        previewAppearanceBytes_ = 0;
        previewFacialFeatures_ = 0;
        previewUseFemaleModel_ = false;
        previewEquipHash_ = 0;
        previewEmptyScene_ = false;
        previewError_.clear();
        deferredDeleteGuid_ = 0;
    }

    /**
     * Check if a character has been selected
     */
    [[nodiscard]] bool hasSelection() const { return characterSelected; }

    /**
     * Get selected character GUID
     */
    [[nodiscard]] uint64_t getSelectedGuid() const { return selectedCharacterGuid; }

    /**
     * Update status message
     */
    void setStatus(const std::string& message, bool isError = false);

    /**
     * Select character by name (used after character creation)
     */
    void selectCharacterByName(const std::string& name);

private:
    UIServices services_;  // Injected service references

    // UI state
    int selectedCharacterIndex = -1;
    bool characterSelected = false;
    uint64_t selectedCharacterGuid = 0;
    bool restoredLastCharacter = false;
    std::string newlyCreatedCharacterName;  // Auto-select this character if set

    // Status
    std::string statusMessage;
    bool statusIsError = false;

    // Callbacks
    std::function<void(uint64_t)> onCharacterSelected;
    std::function<void()> onCreateCharacter;
    std::function<void()> onBack;
    std::function<void(uint64_t)> onDeleteCharacter;
    int deleteConfirmStage = 0;  // 0=none, 1=first warning, 2=final warning

    /**
     * Get faction color based on race
     */
    [[nodiscard]] ImVec4 getFactionColor(game::Race race) const;

    /// The controls. See paper_ui.hpp - this screen is drawn rather than
    /// asked for, the same as the login and realm screens before it.
    PaperUI ui_;
    WotlkGlue glue_;
    void synchronizeSelection(const std::vector<game::Character>& characters);

    /// The states with nothing to choose from - still loading, disconnected,
    /// or an account with no characters on this realm. One small sheet
    /// between them, since each is a sentence and a row of buttons.
    void renderNotice(game::GameHandler& gameHandler, float screenW, float screenH);
    /// Both halves of "are you sure", over a scrim.
    void renderDeleteConfirm(const game::Character& character, float screenW,
                             float screenH);

    /// AddOns management sheet (list + enable/disable), opened from the footer.
    bool showAddonsWindow_ = false;
    void renderAddonsSheet(float screenW, float screenH);

    /**
     * Persist / restore last selected character GUID
     */
    static std::string getConfigDir();
    void saveLastCharacter(uint64_t guid);
    uint64_t loadLastCharacter();

    bool localRealmMode_ = false;

    // Preview (3D character portrait)
    pipeline::AssetManager* assetManager_ = nullptr;
    std::unique_ptr<rendering::CharacterPreview> preview_;
    bool previewInitialized_ = false;
    bool previewInitAttempted_ = false;
    bool previewAssetReset_ = false;
    bool previewEmptyScene_ = false;
    std::string previewError_;
    uint64_t deferredDeleteGuid_ = 0;
    uint64_t previewGuid_ = 0;
    uint32_t previewAppearanceBytes_ = 0;
    uint8_t previewFacialFeatures_ = 0;
    bool previewUseFemaleModel_ = false;
    uint64_t previewEquipHash_ = 0;
};

} // namespace ui
} // namespace wowee
