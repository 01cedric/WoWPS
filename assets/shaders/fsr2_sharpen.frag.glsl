#version 450

layout(location = 0) in vec2 TexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 0, binding = 0) uniform sampler2D inputImage;

layout(push_constant) uniform PushConstants {
    vec4 params;  // x = 1/width, y = 1/height, z = sharpness (0-2), w = unused
} pc;

void main() {
    vec2 tc = TexCoord;

    vec2 texelSize = pc.params.xy;
    float sharpness = pc.params.z;

    // RCAS: Robust Contrast-Adaptive Sharpening
    // 5-tap cross pattern
    vec3 center = texture(inputImage, tc).rgb;
    vec3 north  = texture(inputImage, tc + vec2(0.0, -texelSize.y)).rgb;
    vec3 south  = texture(inputImage, tc + vec2(0.0,  texelSize.y)).rgb;
    vec3 west   = texture(inputImage, tc + vec2(-texelSize.x, 0.0)).rgb;
    vec3 east   = texture(inputImage, tc + vec2( texelSize.x, 0.0)).rgb;

    // Compute local contrast (min/max of neighborhood)
    vec3 minRGB = min(center, min(min(north, south), min(west, east)));
    vec3 maxRGB = max(center, max(max(north, south), max(west, east)));

    // Adaptive sharpening weight based on local contrast
    // High contrast = less sharpening (prevent ringing)
    vec3 range = maxRGB - minRGB;
    vec3 rcpRange = 1.0 / (range + 0.001);

    // AMD FidelityFX RCAS-style weight computation:
    // Compute per-channel sharpening weight from local contrast
    vec3 rcpM = 1.0 / (4.0 * range + 0.001);
    // Weight capped at sharpness, inversely proportional to contrast
    float w = min(min(rcpM.r, min(rcpM.g, rcpM.b)), sharpness);

    // Apply sharpening: negative lobe on neighbors
    vec3 sharpened = (center * (1.0 + 4.0 * w) - (north + south + west + east) * w)
                   / (1.0 + 4.0 * w - 4.0 * w);
    // Simplified: center + w * (4*center - north - south - west - east)
    sharpened = center + w * (4.0 * center - north - south - west - east);

    // Soft clamp: allow some overshoot for sharpness, prevent extreme ringing
    vec3 overshoot = 0.1 * (maxRGB - minRGB);
    sharpened = clamp(sharpened, minRGB - overshoot, maxRGB + overshoot);

    FragColor = vec4(sharpened, 1.0);
}
