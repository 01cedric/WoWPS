#version 450
// M2-specific masks retain the scene material texture, UV channel and animation.
// Affine row packing preserves the complete 128-byte Vulkan push ABI.

layout(push_constant) uniform Push {
    layout(offset = 0) vec4 lightRows[3];
    layout(offset = 48) vec4 modelRows[3];
    layout(offset = 96) vec4 uvLinear;
    layout(offset = 112) vec2 uvOffset;
    layout(offset = 120) uint texCoordSet;
    layout(offset = 124) uint maskMode;
} push;

layout(set = 0, binding = 1) uniform ShadowParams {
    int useBones;
    int useTexture;
    int alphaTest;
    int foliageSway;
    float windTime;
    float foliageMotionDamp;
};

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 4) in vec2 aTexCoord1;
layout(location = 0) out vec2 TexCoord;

void main() {
    vec4 pos = vec4(aPos, 1.0);
    vec3 worldRefOrigin = vec3(push.modelRows[0].w, push.modelRows[1].w, push.modelRows[2].w);
    // Wind vertex displacement for foliage (matches m2.vert.glsl)
    if (foliageSway != 0) {
        vec3 worldRef = worldRefOrigin;
        float heightFactor = clamp(pos.z / 20.0, 0.0, 1.0);
        heightFactor *= heightFactor;

        // Layer 1: Trunk sway
        float trunkPhase = windTime * 0.8 + dot(worldRef.xy, vec2(0.1, 0.13));
        float trunkSwayX = sin(trunkPhase) * 0.35 * heightFactor;
        float trunkSwayY = cos(trunkPhase * 0.7) * 0.25 * heightFactor;

        // Layer 2: Branch sway
        float branchPhase = windTime * 1.7 + dot(worldRef.xy, vec2(0.37, 0.71));
        float branchSwayX = sin(branchPhase + pos.y * 0.4) * 0.15 * heightFactor;
        float branchSwayY = cos(branchPhase * 1.1 + pos.x * 0.3) * 0.12 * heightFactor;

        // Layer 3: Leaf flutter
        float leafPhase = windTime * 4.5 + dot(aPos, vec3(1.7, 2.3, 0.9));
        float leafFlutterX = sin(leafPhase) * 0.06 * heightFactor;
        float leafFlutterY = cos(leafPhase * 1.3) * 0.05 * heightFactor;

        pos.x += trunkSwayX + branchSwayX + leafFlutterX;
        pos.y += trunkSwayY + branchSwayY + leafFlutterY;
    }

    vec4 worldPos = vec4(dot(push.modelRows[0], pos),
                         dot(push.modelRows[1], pos),
                         dot(push.modelRows[2], pos), 1.0);
    vec2 uv = push.texCoordSet == 1u ? aTexCoord1 : aTexCoord;
    TexCoord = mat2(push.uvLinear.xy, push.uvLinear.zw) * uv + push.uvOffset;
    gl_Position = vec4(dot(push.lightRows[0], worldPos),
                       dot(push.lightRows[1], worldPos),
                       dot(push.lightRows[2], worldPos), 1.0);
}
