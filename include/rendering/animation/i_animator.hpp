#pragma once

#include "rendering/animation/anim_event.hpp"

namespace wowee {
namespace rendering {

// ============================================================================
// IAnimator
//
// Base interface for all entity animators. Common to player + NPC.
// ============================================================================
class IAnimator {
public:
    virtual void onEvent(AnimEvent event) = 0;
    virtual void update(float dt) = 0;
    virtual ~IAnimator() = default;
};

} // namespace rendering
} // namespace wowee
