#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wowee {

namespace ui { class UIManager; }
namespace game { class GameHandler; class ExpansionRegistry; }
namespace auth { class AuthHandler; }
namespace pipeline { class AssetManager; }

namespace core {

// Forward-declared in application.hpp
enum class AppState;

/// Handles authentication, realm selection, character selection/creation UI callbacks.
/// Owns pendingCreatedCharacterName_.
class UIScreenCallbackHandler {
public:
    using SetStateFn = std::function<void(AppState)>;

    UIScreenCallbackHandler(ui::UIManager& uiManager,
                            game::GameHandler& gameHandler,
                            auth::AuthHandler& authHandler,
                            game::ExpansionRegistry* expansionRegistry,
                            pipeline::AssetManager* assetManager,
                            SetStateFn setState);

    void setupCallbacks();

private:
    ui::UIManager& uiManager_;
    game::GameHandler& gameHandler_;
    auth::AuthHandler& authHandler_;
    game::ExpansionRegistry* expansionRegistry_;
    pipeline::AssetManager* assetManager_;
    SetStateFn setState_;

    std::string pendingCreatedCharacterName_;  // Auto-select after character creation
    std::vector<uint8_t> authenticatedSessionKey_;
};

} // namespace core
} // namespace wowee
