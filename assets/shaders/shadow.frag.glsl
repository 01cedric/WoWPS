#version 450

layout(set = 0, binding = 0) uniform sampler2D uTexture;

layout(set = 0, binding = 1) uniform ShadowParams {
    int useBones;
    int useTexture;
    int alphaTest;
    int foliageSway;
    float windTime;
    float foliageMotionDamp;
};

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec3 WorldPos;

void main() {
    // Explicit PS4 depth export; discarded fragments still produce no depth.
    // Set before the opaque fast return as every surviving path must write it.
    gl_FragDepth = gl_FragCoord.z;
    if (useTexture != 0) {
        vec4 texColor = textureLod(uTexture, TexCoord, 0.0);
        if (alphaTest != 0 && texColor.a < 0.5) discard;
    }
}
