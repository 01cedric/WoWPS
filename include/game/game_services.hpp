#pragma once
#include <cstdint>

namespace wowee {
namespace rendering { class Renderer; }
namespace pipeline { class AssetManager; }
namespace audio { class AudioCoordinator; }
namespace game { class ExpansionRegistry; }

namespace game {

// Explicit service dependencies for game handlers.
// Owned by Application, passed by reference to GameHandler at construction.
// Replaces hidden Application::getInstance() singleton access.
struct GameServices {
    rendering::Renderer* renderer = nullptr;
    audio::AudioCoordinator* audioCoordinator = nullptr;
    pipeline::AssetManager* assetManager = nullptr;
    ExpansionRegistry* expansionRegistry = nullptr;
    uint32_t gryphonDisplayId = 0;
    uint32_t wyvernDisplayId = 0;
};

} // namespace game
} // namespace wowee
