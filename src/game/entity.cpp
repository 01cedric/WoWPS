#include "game/entity.hpp"
#include "core/logger.hpp"
#include <algorithm>

namespace wowee {
namespace game {

void EntityManager::addEntity(uint64_t guid, std::shared_ptr<Entity> entity) {
    if (!entity) {
        LOG_WARNING("Attempted to add null entity with GUID: 0x", std::hex, guid, std::dec);
        return;
    }

    const int type = static_cast<int>(entity->getType());
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entities[guid] = std::move(entity);
        spatialDirty_ = true;
    }

    LOG_DEBUG("Added entity: GUID=0x", std::hex, guid, std::dec,
              ", Type=", type);
}

void EntityManager::removeEntity(uint64_t guid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities.find(guid);
    if (it != entities.end()) {
        LOG_DEBUG("Removed entity: GUID=0x", std::hex, guid, std::dec);
        entities.erase(it);
        spatialDirty_ = true;
    }
}

std::shared_ptr<Entity> EntityManager::getEntity(uint64_t guid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities.find(guid);
    return (it != entities.end()) ? it->second : nullptr;
}

bool EntityManager::hasEntity(uint64_t guid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entities.find(guid) != entities.end();
}

std::vector<std::shared_ptr<Entity>> EntityManager::getEntitiesNear(
        float x, float y, float radius) const {
    std::vector<std::shared_ptr<Entity>> result;
    getEntitiesNear(x, y, radius, result);
    return result;
}

void EntityManager::getEntitiesNear(float x, float y, float radius,
                                    std::vector<std::shared_ptr<Entity>>& result) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (spatialDirty_ || lastSpatialRebuild_.time_since_epoch().count() == 0 ||
        now - lastSpatialRebuild_ >= std::chrono::milliseconds(250)) {
        spatialCells_.clear();
        spatialCells_.reserve(entities.size() / 4 + 1);
        auto cellKey = [](int32_t cx, int32_t cy) -> int64_t {
            return static_cast<int64_t>((static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
                                        static_cast<uint32_t>(cy));
        };
        for (const auto& [guid, entity] : entities) {
            (void)guid;
            if (!entity) continue;
            const int32_t cx = static_cast<int32_t>(std::floor(entity->getLatestX() / kSpatialCellSize));
            const int32_t cy = static_cast<int32_t>(std::floor(entity->getLatestY() / kSpatialCellSize));
            spatialCells_[cellKey(cx, cy)].push_back(entity);
        }
        lastSpatialRebuild_ = now;
        spatialDirty_ = false;
    }

    const int32_t minX = static_cast<int32_t>(std::floor((x - radius) / kSpatialCellSize));
    const int32_t maxX = static_cast<int32_t>(std::floor((x + radius) / kSpatialCellSize));
    const int32_t minY = static_cast<int32_t>(std::floor((y - radius) / kSpatialCellSize));
    const int32_t maxY = static_cast<int32_t>(std::floor((y + radius) / kSpatialCellSize));
    const float radiusSq = radius * radius;
    result.clear();
    result.reserve(std::min(entities.size(), size_t(256)));
    auto cellKey = [](int32_t cx, int32_t cy) -> int64_t {
        return static_cast<int64_t>((static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
                                    static_cast<uint32_t>(cy));
    };
    for (int32_t cy = minY; cy <= maxY; ++cy) {
        for (int32_t cx = minX; cx <= maxX; ++cx) {
            auto it = spatialCells_.find(cellKey(cx, cy));
            if (it == spatialCells_.end()) continue;
            for (const auto& entity : it->second) {
                const float dx = entity->getLatestX() - x;
                const float dy = entity->getLatestY() - y;
                if (dx * dx + dy * dy <= radiusSq) result.push_back(entity);
            }
        }
    }
}

} // namespace game
} // namespace wowee
