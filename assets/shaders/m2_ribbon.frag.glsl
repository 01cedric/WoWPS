#version 450

// M2 ribbon emitter fragment shader.
// Samples the ribbon texture, multiplied by vertex color and alpha.
// Uses additive blending (pipeline-level) for magic/spell trails.

layout(set = 1, binding = 0) uniform sampler2D uTexture;

layout(location = 0) in vec3 vColor;
layout(location = 1) in float vAlpha;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vFogFactor;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 tex = texture(uTexture, vUV);
    // For additive ribbons alpha comes from texture luminance; multiply by vertex alpha.
    float a = tex.a * vAlpha;
    if (a < 0.01) discard;
    vec3 rgb = tex.rgb * vColor;
    // Ribbons fade slightly with fog (additive blend attenuated toward black = invisible in fog).
    rgb *= vFogFactor;
    outColor = vec4(rgb, a);
}
