// Renamed from PlayerAnimator/NpcAnimator dual-map → unified CharacterAnimator registry.
// NpcAnimator methods removed - all characters use CharacterAnimator.
#include "rendering/animation/animation_manager.hpp"

namespace wowee {
namespace rendering {

CharacterAnimator* AnimationManager::get(uint32_t instanceId) {
    auto it = animators_.find(instanceId);
    return it != animators_.end() ? it->second.get() : nullptr;
}

void AnimationManager::remove(uint32_t instanceId) {
    animators_.erase(instanceId);
}

} // namespace rendering
} // namespace wowee
