// view_state_machine.cpp - Navigation state and transitions for the world map.
// Extracted from WorldMap::zoomIn, zoomOut, enterWorldView, enterCosmicView
// (Phase 6 of refactoring plan).
#include "rendering/world_map/view_state_machine.hpp"

namespace wowee {
namespace rendering {
namespace world_map {

void ViewStateMachine::startTransition(ViewLevel from, ViewLevel to, float duration) {
    transition_.active = true;
    transition_.progress = 0.0f;
    transition_.duration = duration;
    transition_.fromLevel = from;
    transition_.toLevel = to;
}

bool ViewStateMachine::updateTransition(float deltaTime) {
    if (!transition_.active) return false;
    transition_.progress += deltaTime / transition_.duration;
    if (transition_.progress >= 1.0f) {
        transition_.progress = 1.0f;
        transition_.active = false;
    }
    return transition_.active;
}

ViewStateMachine::ZoomResult ViewStateMachine::zoomIn(int hoveredZoneIdx, int playerZoneIdx) {
    ZoomResult result;

    if (level_ == ViewLevel::COSMIC) {
        startTransition(ViewLevel::COSMIC, ViewLevel::WORLD);
        level_ = ViewLevel::WORLD;
        result.changed = true;
        result.newLevel = ViewLevel::WORLD;
        // Caller should call enterWorldView() to determine target index
        return result;
    }

    if (level_ == ViewLevel::WORLD) {
        if (continentIdx_ >= 0) {
            startTransition(ViewLevel::WORLD, ViewLevel::CONTINENT);
            level_ = ViewLevel::CONTINENT;
            currentIdx_ = continentIdx_;
            result.changed = true;
            result.newLevel = ViewLevel::CONTINENT;
            result.targetIdx = continentIdx_;
        }
        return result;
    }

    if (level_ == ViewLevel::CONTINENT) {
        // Prefer the zone the mouse is hovering over; fall back to the player's zone
        int zoneIdx = hoveredZoneIdx >= 0 ? hoveredZoneIdx : playerZoneIdx;
        if (zoneIdx >= 0) {
            startTransition(ViewLevel::CONTINENT, ViewLevel::ZONE);
            level_ = ViewLevel::ZONE;
            currentIdx_ = zoneIdx;
            result.changed = true;
            result.newLevel = ViewLevel::ZONE;
            result.targetIdx = zoneIdx;
        }
    }

    return result;
}

ViewStateMachine::ZoomResult ViewStateMachine::zoomOut() {
    ZoomResult result;

    if (level_ == ViewLevel::ZONE) {
        if (continentIdx_ >= 0) {
            startTransition(ViewLevel::ZONE, ViewLevel::CONTINENT);
            level_ = ViewLevel::CONTINENT;
            currentIdx_ = continentIdx_;
            result.changed = true;
            result.newLevel = ViewLevel::CONTINENT;
            result.targetIdx = continentIdx_;
        }
        return result;
    }

    if (level_ == ViewLevel::CONTINENT) {
        startTransition(ViewLevel::CONTINENT, ViewLevel::WORLD);
        level_ = ViewLevel::WORLD;
        result.changed = true;
        result.newLevel = ViewLevel::WORLD;
        // Caller should call enterWorldView() to determine target index
        return result;
    }

    if (level_ == ViewLevel::WORLD) {
        // Vanilla: cosmic view disabled, don't zoom out further
        if (!cosmicEnabled_) return result;
        startTransition(ViewLevel::WORLD, ViewLevel::COSMIC);
        level_ = ViewLevel::COSMIC;
        result.changed = true;
        result.newLevel = ViewLevel::COSMIC;
        // Caller should call enterCosmicView() to determine target index
    }

    return result;
}

ViewStateMachine::ZoomResult ViewStateMachine::enterWorldView() {
    ZoomResult result;
    level_ = ViewLevel::WORLD;
    result.changed = true;
    result.newLevel = ViewLevel::WORLD;
    // Caller is responsible for finding the root continent and compositing
    return result;
}

ViewStateMachine::ZoomResult ViewStateMachine::enterCosmicView() {
    // Vanilla: cosmic view is disabled - stay in world view
    if (!cosmicEnabled_) {
        return enterWorldView();
    }

    ZoomResult result;
    level_ = ViewLevel::COSMIC;
    result.changed = true;
    result.newLevel = ViewLevel::COSMIC;
    // Caller uses cosmicIdx from DataRepository
    return result;
}

ViewStateMachine::ZoomResult ViewStateMachine::enterZone(int zoneIdx) {
    ZoomResult result;
    startTransition(ViewLevel::CONTINENT, ViewLevel::ZONE);
    level_ = ViewLevel::ZONE;
    currentIdx_ = zoneIdx;
    result.changed = true;
    result.newLevel = ViewLevel::ZONE;
    result.targetIdx = zoneIdx;
    return result;
}

} // namespace world_map
} // namespace rendering
} // namespace wowee
