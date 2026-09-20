// Frozen the implementation traversal oracle; used only by the CPU regression.
void WMORenderer::reference(const ModelData& model,
                                              const glm::vec3& cameraLocalPos,
                                              const glm::vec3& viewerLocalPos,
                                              const Frustum& frustum,
                                              const glm::mat4& modelMatrix,
                                              std::unordered_set<uint32_t>& outVisibleGroups) const {
    constexpr uint32_t WMO_GROUP_FLAG_OUTDOOR = 0x8;
    constexpr uint32_t WMO_GROUP_FLAG_INDOOR = 0x2000;

    // Find camera's containing group
    int cameraGroup = findContainingGroup(model, cameraLocalPos);

    // If camera is outside all groups, fall back to frustum culling only
    if (cameraGroup < 0) {
        // Camera outside WMO - mark all groups as potentially visible
        // (will still be frustum culled in render)
        for (size_t gi = 0; gi < model.groups.size(); gi++) {
            outVisibleGroups.insert(static_cast<uint32_t>(gi));
        }
        return;
    }

    // Outdoor city WMOs (e.g. Stormwind) often have portal graphs that are valid for
    // indoor visibility but too aggressive outdoors, causing direction-dependent popout.
    // Only trust portal traversal when the camera is in an interior-only group.
    if (cameraGroup < static_cast<int>(model.groups.size())) {
        const uint32_t gFlags = model.groups[cameraGroup].groupFlags;
        const bool isIndoor = (gFlags & WMO_GROUP_FLAG_INDOOR) != 0;
        const bool isOutdoor = (gFlags & WMO_GROUP_FLAG_OUTDOOR) != 0;
        if (!isIndoor || isOutdoor) {
            for (size_t gi = 0; gi < model.groups.size(); gi++) {
                outVisibleGroups.insert(static_cast<uint32_t>(gi));
            }
            return;
        }
        // Best-fit group is indoor-only, but the position might also be inside an
        // outdoor group's AABB (e.g., standing on a street near a building whose
        // indoor AABB extends outward).  If any outdoor group also contains the
        // position, treat this as an outdoor location and show all groups.
        for (size_t gi = 0; gi < model.groups.size(); gi++) {
            if (static_cast<int>(gi) == cameraGroup) continue;
            const auto& g = model.groups[gi];
            if (!(g.groupFlags & WMO_GROUP_FLAG_OUTDOOR)) continue;
            if (cameraLocalPos.x >= g.boundingBoxMin.x && cameraLocalPos.x <= g.boundingBoxMax.x &&
                cameraLocalPos.y >= g.boundingBoxMin.y && cameraLocalPos.y <= g.boundingBoxMax.y &&
                cameraLocalPos.z >= g.boundingBoxMin.z && cameraLocalPos.z <= g.boundingBoxMax.z) {
                for (size_t gj = 0; gj < model.groups.size(); gj++) {
                    outVisibleGroups.insert(static_cast<uint32_t>(gj));
                }
                return;
            }
        }
    }

    // If the camera group has no portal refs, it's a dead-end group (utility/transition group).
    // Fall back to showing all groups to avoid the rest of the WMO going invisible.
    if (cameraGroup < static_cast<int>(model.groupPortalRefs.size())) {
        auto [portalStart, portalCount] = model.groupPortalRefs[cameraGroup];
        if (portalCount == 0) {
            for (size_t gi = 0; gi < model.groups.size(); gi++) {
                outVisibleGroups.insert(static_cast<uint32_t>(gi));
            }
            return;
        }
    }

    // Exterior groups are never portal-culled. AABB containment misclassifies
    // doorway/awning thresholds as indoors (interior boxes spill past the
    // door and outdoor boxes don't always overlap them), and a wrong BFS
    // start then hides the whole city. Portal traversal below only decides
    // interior-only groups; streets and facades always draw (distance culling
    // still bounds them).
    for (size_t gi = 0; gi < model.groups.size(); ++gi) {
        const uint32_t f = model.groups[gi].groupFlags;
        if (!(f & WMO_GROUP_FLAG_INDOOR) || (f & WMO_GROUP_FLAG_OUTDOOR))
            outVisibleGroups.insert(static_cast<uint32_t>(gi));
    }

    // BFS through portals from both viewpoints' groups plus every always-visible
    // exterior group, so interiors seen through open doors still draw even when
    // one viewpoint's containing group was misclassified.
    //
    // The character's group is seeded alongside the camera's because in a
    // doorway or a hallway the two are in different rooms. Walking from only one
    // of them, while judging every door against the camera's frustum, is what
    // emptied Ironforge. A second seed can only add groups, never remove any,
    // and drawing a room too many is the failure this should have.
    std::vector<bool> visited(model.groups.size(), false);
    std::vector<uint32_t> queue;
    queue.reserve(model.groups.size());

    auto seed = [&](int gi) {
        if (gi < 0 || gi >= static_cast<int>(model.groups.size())) return;
        if (visited[gi]) return;
        visited[gi] = true;
        queue.push_back(static_cast<uint32_t>(gi));
        outVisibleGroups.insert(static_cast<uint32_t>(gi));
    };
    seed(cameraGroup);
    seed(findContainingGroup(model, viewerLocalPos));

    // Exterior groups were inserted above without being queued; they are portals
    // into the interior too.
    for (uint32_t gi : outVisibleGroups) {
        if (!visited[gi]) {
            visited[gi] = true;
            queue.push_back(gi);
        }
    }

    size_t queueIdx = 0;
    while (queueIdx < queue.size()) {
        uint32_t currentGroup = queue[queueIdx++];

        // Get portal refs for this group
        if (currentGroup >= model.groupPortalRefs.size()) continue;
        auto [portalStart, portalCount] = model.groupPortalRefs[currentGroup];

        for (uint16_t pi = 0; pi < portalCount; pi++) {
            uint16_t refIdx = portalStart + pi;
            if (refIdx >= model.portalRefs.size()) continue;

            const auto& ref = model.portalRefs[refIdx];
            uint32_t targetGroup = ref.groupIndex;

            if (targetGroup >= model.groups.size()) continue;
            if (visited[targetGroup]) continue;

            // Check if portal is visible from camera
            if (isPortalVisible(model, ref.portalIndex, cameraLocalPos, frustum, modelMatrix)) {
                visited[targetGroup] = true;
                outVisibleGroups.insert(targetGroup);
                queue.push_back(targetGroup);
            }
        }
    }
}
