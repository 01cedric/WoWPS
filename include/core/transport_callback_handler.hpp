#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace wowee {

namespace rendering { class Renderer; }
namespace game { class GameHandler; }
namespace core { class EntitySpawner; class WorldLoader; class AppearanceComposer; }

namespace core {

/// Handles transport-related callbacks: transport spawn/move, taxi, mount.
class TransportCallbackHandler {
public:
    TransportCallbackHandler(EntitySpawner& entitySpawner,
                             rendering::Renderer& renderer,
                             game::GameHandler& gameHandler,
                             AppearanceComposer* appearanceComposer);

    void setupCallbacks();

private:
    EntitySpawner& entitySpawner_;
    rendering::Renderer& renderer_;
    game::GameHandler& gameHandler_;
    AppearanceComposer* appearanceComposer_;
};

} // namespace core
} // namespace wowee
