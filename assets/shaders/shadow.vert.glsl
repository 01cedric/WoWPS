#version 450

layout(push_constant) uniform Push {
    mat4 lightSpaceMatrix;
    mat4 model;
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
layout(location = 2) in vec4 aBoneWeights;
layout(location = 3) in vec4 aBoneIndicesF;

layout(location = 0) out vec2 TexCoord;
layout(location = 1) out vec3 WorldPos;

void main() {
    vec4 pos = vec4(aPos, 1.0);

    // Wind vertex displacement for foliage (matches m2.vert.glsl)
    if (foliageSway != 0) {
        vec3 worldRef = push.model[3].xyz;
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

    vec4 worldPos = push.model * pos;
    WorldPos = worldPos.xyz;
    TexCoord = aTexCoord;
    gl_Position = push.lightSpaceMatrix * worldPos;
}
