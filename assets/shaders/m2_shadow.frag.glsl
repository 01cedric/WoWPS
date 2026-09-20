#version 450

layout(set = 1, binding = 0) uniform sampler2D uTexture;
layout(set = 1, binding = 2) uniform M2Material {
    int hasTexture;
    int alphaTest;
    int colorKeyBlack;
    float colorKeyThreshold;
    int unlit;
    int blendMode;
    float fadeAlpha;
    float interiorDarken;
    float specularIntensity;
    float emissiveBoost;
    float tintR;
    float tintG;
    float tintB;
};
layout(push_constant) uniform Push {
    layout(offset = 124) uint maskMode;
} push;
layout(location = 0) in vec2 TexCoord;

void main() {
    // Explicit PS4 depth export; discarded fragments still produce no depth.
    // Set before the opaque fast return as every surviving path must write it.
    gl_FragDepth = gl_FragCoord.z;
    if (push.maskMode == 0u) return;
    vec4 texColor = textureLod(uTexture, TexCoord, 0.0);
    uint alphaMode = push.maskMode & 3u;
    // Binary depth uses the scene material's coverage midpoint. Scene MSAA
    // smooths the edge; the shadow map remains a single binary depth sample.
    float cutoff = alphaMode == 3u ? 0.25 : 0.4;
    if (alphaMode != 0u && texColor.a < cutoff) discard;
    if ((push.maskMode & 4u) != 0u) {
        float lum = dot(texColor.rgb * vec3(tintR, tintG, tintB), vec3(0.299, 0.587, 0.114));
        if (lum < colorKeyThreshold) discard;
    }
}
