#version 450
// FSR 1.0 RCAS (Robust Contrast Adaptive Sharpening) - Fragment Shader
// Based on AMD FidelityFX Super Resolution 1.0
// Applies contrast-adaptive sharpening after EASU upscaling

layout(set = 0, binding = 0) uniform sampler2D uInput;

layout(push_constant) uniform RCASConstants {
    vec4 con0; // 1/outputSize.xy, outputSize.xy
    vec4 con1; // sharpness (x), 0, 0, 0
} rcas;

layout(location = 0) in vec2 TexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    // Fetch center and 4-neighborhood
    vec2 texelSize = rcas.con0.xy;
    vec3 c = texture(uInput, TexCoord).rgb;
    vec3 n = texture(uInput, TexCoord + vec2( 0, -texelSize.y)).rgb;
    vec3 s = texture(uInput, TexCoord + vec2( 0,  texelSize.y)).rgb;
    vec3 w = texture(uInput, TexCoord + vec2(-texelSize.x,  0)).rgb;
    vec3 e = texture(uInput, TexCoord + vec2( texelSize.x,  0)).rgb;

    // Luma (green channel approximation)
    float cL = c.g, nL = n.g, sL = s.g, wL = w.g, eL = e.g;

    // Min/max of neighborhood
    float minL = min(min(nL, sL), min(wL, eL));
    float maxL = max(max(nL, sL), max(wL, eL));

    // Contrast adaptive sharpening weight
    // Higher contrast = less sharpening to avoid ringing
    float contrast = maxL - minL;
    float sharpness = rcas.con1.x;
    float w0 = sharpness * (1.0 - smoothstep(0.0, 0.3, contrast));

    // Apply sharpening: center + w0 * (center - average_neighbors)
    vec3 avg = (n + s + w + e) * 0.25;
    vec3 sharpened = c + w0 * (c - avg);

    outColor = vec4(clamp(sharpened, 0.0, 1.0), 1.0);
}
