#pragma once

#include <cstdint>
#include <array>
#include <functional>

namespace wowee {

namespace rendering { class Renderer; }
namespace game { class GameHandler; }
namespace core { class EntitySpawner; }

namespace core {

/// Handles entity spawn/despawn callbacks: creatures, players, game objects.
class EntitySpawnCallbackHandler {
public:
    /// @param isLocalPlayerGuid Returns true if the given GUID is the local player (to skip self-spawn)
    EntitySpawnCallbackHandler(EntitySpawner& entitySpawner,
                               rendering::Renderer& renderer,
                               game::GameHandler& gameHandler,
                               std::function<bool(uint64_t)> isLocalPlayerGuid);

    void setupCallbacks();

private:
    EntitySpawner& entitySpawner_;
    rendering::Renderer& renderer_;
    game::GameHandler& gameHandler_;
    std::function<bool(uint64_t)> isLocalPlayerGuid_;
};

} // namespace core
} // namespace wowee
