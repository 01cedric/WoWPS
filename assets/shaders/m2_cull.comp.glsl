#version 450

// GPU Frustum Culling for M2 doodads
// Each compute thread tests one M2 instance against 6 frustum planes.
// Input:  per-instance bounding sphere + flags.
// Output: uint visibility array (1 = visible, 0 = culled).

layout(local_size_x = 64) in;

// Per-instance cull data (uploaded from CPU each frame)
struct CullInstance {
    vec4  sphere;              // xyz = world position, w = padded radius
    float effectiveMaxDistSq;  // adaptive distance cull threshold
    uint  flags;               // bit 0 = valid, bit 1 = smoke, bit 2 = invisibleTrap
    float _pad0;
    float _pad1;
};

layout(std140, set = 0, binding = 0) uniform CullUniforms {
    vec4  frustumPlanes[6]; // xyz = normal, w = distance
    vec4  cameraPos;        // xyz = camera position, w = maxPossibleDistSq
    uint  instanceCount;
    uint  _pad0;
    uint  _pad1;
    uint  _pad2;
};

layout(std430, set = 0, binding = 1) readonly buffer CullInput {
    CullInstance cullInstances[];
};

layout(std430, set = 0, binding = 2) writeonly buffer CullOutput {
    uint visibility[];
};

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

    // Frustum cull: sphere vs 6 planes
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

    visibility[id] = 1u;
}
