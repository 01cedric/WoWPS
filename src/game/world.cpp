#include "game/world.hpp"

namespace wowee {
namespace game {

void World::update([[maybe_unused]] float deltaTime) {
    // World state updates are handled by Application (terrain streaming, entity sync,
    // camera, etc.) and GameHandler (server packet processing). World is a thin
    // ownership token; per-frame logic lives in those subsystems.
}
} // namespace game
} // namespace wowee
