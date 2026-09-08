#pragma once

#include <cstdint>

namespace wowee {
namespace game {

class World {
public:
    World() = default;
    ~World() = default;

    void update(float deltaTime);
};

} // namespace game
} // namespace wowee
