#version 450

// GPU Frustum + HiZ Occlusion Culling for M2 doodads (Phase 6.3).
//
// Two-level culling:
//   1. Frustum - current-frame planes from viewProj.
//   2. HiZ occlusion - projects bounding sphere into the PREVIOUS frame's
//      screen space via prevViewProj and samples the Hierarchical-Z pyramid
//      (built from said previous depth).  Conservative safeguards:
//        • Only objects that were visible last frame get the HiZ test.
//        • AABB must be fully inside the screen (no border sampling).
//        • Bounding sphere is inflated by 50 % for the HiZ AABB.
//        • A depth bias is applied before the occlusion comparison.
//        • Nearest depth is projected via prevViewProj from sphere center
//          (avoids toCam mismatch between current and previous cameras).
//
// Falls back gracefully: if hizEnabled == 0, behaves identically to frustum-only.

layout(local_size_x = 64) in;

struct CullInstance {
    vec4  sphere;              // xyz = world position, w = padded radius
    float effectiveMaxDistSq;
    uint  flags;               // bit 0 = valid, bit 1 = smoke, bit 2 = invisibleTrap,
                               // bit 3 = previouslyVisible
    float _pad0;
    float _pad1;
};

layout(std140, set = 0, binding = 0) uniform CullUniforms {
    vec4  frustumPlanes[6];
    vec4  cameraPos;           // xyz = camera position, w = maxPossibleDistSq
    uint  instanceCount;
    uint  hizEnabled;
    uint  hizMipLevels;
    uint  _pad2;
    vec4  hizParams;           // x = pyramidWidth, y = pyramidHeight, z = nearPlane, w = unused
    mat4  viewProj;            // current frame view-projection
    mat4  prevViewProj;        // PREVIOUS frame's view-projection for HiZ reprojection
};

layout(std430, set = 0, binding = 1) readonly buffer CullInput {
    CullInstance cullInstances[];
};

layout(std430, set = 0, binding = 2) buffer CullOutput {
    uint visibility[];
};

layout(set = 1, binding = 0) uniform sampler2D hizPyramid;

// Screen-edge margin - skip HiZ if the AABB touches this border.
// Depth data at screen edges is from unrelated geometry → false culls.
const float SCREEN_EDGE_MARGIN = 0.02;

// Sphere inflation factor for HiZ screen AABB (50 % larger → very conservative).
const float HIZ_SPHERE_INFLATE = 1.5;

// Depth bias - push nearest depth closer to camera so only objects
// significantly behind occluders are culled.
const float HIZ_DEPTH_BIAS = 0.02;

// Minimum screen-space size (pixels) for HiZ to engage.
const float HIZ_MIN_SCREEN_PX = 6.0;

void main() {
    uint id = gl_GlobalInvocationID.x;
    if (id >= instanceCount) return;

    CullInstance inst = cullInstances[id];

    // Flag check: must be valid, not smoke, not invisible trap
    uint f = inst.flags;
    if ((f & 1u) == 0u || (f & 6u) != 0u) {
        visibility[id] = 0u;
        return;
    }

    // Early distance rejection (loose upper bound)
    vec3 toCam = inst.sphere.xyz - cameraPos.xyz;
    float distSq = dot(toCam, toCam);
    if (distSq > cameraPos.w) {
        visibility[id] = 0u;
        return;
    }

    // Accurate per-instance distance cull
    if (distSq > inst.effectiveMaxDistSq) {
        visibility[id] = 0u;
        return;
    }

    // Frustum cull: sphere vs 6 planes (current frame)
    float radius = inst.sphere.w;
    if (radius > 0.0) {
        for (int i = 0; i < 6; i++) {
            float d = dot(frustumPlanes[i].xyz, inst.sphere.xyz) + frustumPlanes[i].w;
            if (d < -radius) {
                visibility[id] = 0u;
                return;
            }
        }
    }

    // --- HiZ Occlusion Test ---
    // Skip for objects not rendered last frame (bit 3 = previouslyVisible).
    bool previouslyVisible = (f & 8u) != 0u;

    if (hizEnabled != 0u && radius > 0.0 && previouslyVisible) {
        // Inflate sphere for conservative screen-space AABB
        float hizRadius = radius * HIZ_SPHERE_INFLATE;

        // Project sphere center into previous frame's clip space
        vec4 clipCenter = prevViewProj * vec4(inst.sphere.xyz, 1.0);
        if (clipCenter.w > 0.0) {
            vec3 ndc = clipCenter.xyz / clipCenter.w;

            // --- Correct sphere → screen AABB using VP row-vector lengths ---
            // The maximum screen-space extent of a world-space sphere is
            //   maxDeltaNdcX = R * ‖row_x(VP)‖ / w
            // where row_x = (VP[0][0], VP[1][0], VP[2][0]) maps world XYZ
            // offsets to clip-X.  Using only the diagonal element (VP[0][0])
            // underestimates the footprint when the camera is rotated,
            // causing false culls at certain view angles.
            float rowLenX = length(vec3(prevViewProj[0][0],
                                        prevViewProj[1][0],
                                        prevViewProj[2][0]));
            float rowLenY = length(vec3(prevViewProj[0][1],
                                        prevViewProj[1][1],
                                        prevViewProj[2][1]));
            float projRadX = hizRadius * rowLenX / clipCenter.w;
            float projRadY = hizRadius * rowLenY / clipCenter.w;
            float projRad  = max(projRadX, projRadY);

            vec2 uvCenter = ndc.xy * 0.5 + 0.5;
            float uvRad   = projRad * 0.5;
            vec2 uvMin    = uvCenter - uvRad;
            vec2 uvMax    = uvCenter + uvRad;

            // **Screen-edge guard**: skip if AABB extends outside safe area.
            // Depth data at borders is from unrelated geometry.
            if (uvMin.x >= SCREEN_EDGE_MARGIN && uvMin.y >= SCREEN_EDGE_MARGIN &&
                uvMax.x <= (1.0 - SCREEN_EDGE_MARGIN) && uvMax.y <= (1.0 - SCREEN_EDGE_MARGIN) &&
                uvMax.x > uvMin.x && uvMax.y > uvMin.y)
            {
                float aabbW = (uvMax.x - uvMin.x) * hizParams.x;
                float aabbH = (uvMax.y - uvMin.y) * hizParams.y;
                float screenSize = max(aabbW, aabbH);

                if (screenSize >= HIZ_MIN_SCREEN_PX) {
                    // Mip level: +1 for conservatism (coarser = bigger depth footprint)
                    float mipLevel = ceil(log2(max(screenSize, 1.0))) + 1.0;
                    mipLevel = clamp(mipLevel, 0.0, float(hizMipLevels - 1u));

                    // Sample HiZ at 4 corners - take MAX (farthest occluder)
                    float pz0 = textureLod(hizPyramid, uvMin, mipLevel).r;
                    float pz1 = textureLod(hizPyramid, vec2(uvMax.x, uvMin.y), mipLevel).r;
                    float pz2 = textureLod(hizPyramid, vec2(uvMin.x, uvMax.y), mipLevel).r;
                    float pz3 = textureLod(hizPyramid, uvMax, mipLevel).r;
                    float pyramidDepth = max(max(pz0, pz1), max(pz2, pz3));

                    // Nearest depth: project sphere center's NDC-Z then subtract
                    // the sphere's depth range.  The depth span uses the Z-row
                    // length of VP (same Cauchy-Schwarz reasoning as X/Y), giving
                    // the correct NDC-Z extent regardless of camera orientation.
                    float rowLenZ = length(vec3(prevViewProj[0][2],
                                                prevViewProj[1][2],
                                                prevViewProj[2][2]));
                    float depthSpan = hizRadius * rowLenZ / clipCenter.w;
                    float centerDepth = ndc.z;
                    float nearestDepth = centerDepth - depthSpan - HIZ_DEPTH_BIAS;

                    if (nearestDepth > pyramidDepth && pyramidDepth < 1.0) {
                        visibility[id] = 0u;
                        return;
                    }
                }
            }
        }
        // fallthrough: conservatively visible
    }

    visibility[id] = 1u;
}
