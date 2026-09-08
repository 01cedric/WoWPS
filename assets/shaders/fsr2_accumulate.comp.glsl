#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 0, binding = 1) uniform sampler2D depthBuffer;
layout(set = 0, binding = 2) uniform sampler2D motionVectors;
layout(set = 0, binding = 3) uniform sampler2D historyInput;
layout(set = 0, binding = 4, rgba16f) uniform writeonly image2D historyOutput;

layout(push_constant) uniform PushConstants {
    vec4 internalSize;   // xy = internal resolution, zw = 1/internal
    vec4 displaySize;    // xy = display resolution, zw = 1/display
    vec4 jitterOffset;   // xy = current jitter (NDC-space), zw = unused
    vec4 params;         // x = resetHistory, y = sharpness, z = convergenceFrame, w = unused
} pc;

vec3 tonemap(vec3 c) {
    float luma = max(dot(c, vec3(0.299, 0.587, 0.114)), 0.0);
    return c / (1.0 + luma);
}

vec3 inverseTonemap(vec3 c) {
    float luma = max(dot(c, vec3(0.299, 0.587, 0.114)), 0.0);
    return c / max(1.0 - luma, 1e-4);
}

vec3 rgbToYCoCg(vec3 rgb) {
    float y  = 0.25 * rgb.r + 0.5 * rgb.g + 0.25 * rgb.b;
    float co = 0.5  * rgb.r                - 0.5  * rgb.b;
    float cg = -0.25 * rgb.r + 0.5 * rgb.g - 0.25 * rgb.b;
    return vec3(y, co, cg);
}

vec3 yCoCgToRgb(vec3 ycocg) {
    float y  = ycocg.x;
    float co = ycocg.y;
    float cg = ycocg.z;
    return vec3(y + co - cg, y + cg, y - co - cg);
}

vec3 clipAABB(vec3 aabbMin, vec3 aabbMax, vec3 history) {
    vec3 center = 0.5 * (aabbMax + aabbMin);
    vec3 extents = 0.5 * (aabbMax - aabbMin) + 0.001;
    vec3 offset = history - center;
    vec3 absUnits = abs(offset / extents);
    float maxUnit = max(absUnits.x, max(absUnits.y, absUnits.z));
    if (maxUnit > 1.0)
        return center + offset / maxUnit;
    return history;
}

// Lanczos2 kernel: sharper than bicubic, preserves high-frequency detail
float lanczos2(float x) {
    if (abs(x) < 1e-6) return 1.0;
    if (abs(x) >= 2.0) return 0.0;
    float px = 3.14159265 * x;
    return sin(px) * sin(px * 0.5) / (px * px * 0.5);
}

// Lanczos2 upsampling: sharper than Catmull-Rom bicubic
vec3 sampleLanczos(sampler2D tex, vec2 uv, vec2 texSize) {
    vec2 invTexSize = 1.0 / texSize;
    vec2 texelPos = uv * texSize - 0.5;
    ivec2 base = ivec2(floor(texelPos));
    vec2 f = texelPos - vec2(base);

    vec3 result = vec3(0.0);
    float totalWeight = 0.0;
    for (int y = -1; y <= 2; y++) {
        for (int x = -1; x <= 2; x++) {
            vec2 samplePos = (vec2(base + ivec2(x, y)) + 0.5) * invTexSize;
            float wx = lanczos2(float(x) - f.x);
            float wy = lanczos2(float(y) - f.y);
            float w = wx * wy;
            result += texture(tex, samplePos).rgb * w;
            totalWeight += w;
        }
    }
    return result / totalWeight;
}

void main() {
    ivec2 outPixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 outSize = ivec2(pc.displaySize.xy);
    if (outPixel.x >= outSize.x || outPixel.y >= outSize.y) return;

    vec2 outUV = (vec2(outPixel) + 0.5) * pc.displaySize.zw;

    // Lanczos2 upsample: sharper than bicubic, better base image
    vec3 currentColor = sampleLanczos(sceneColor, outUV, pc.internalSize.xy);

    // Temporal accumulation mode.
    const bool kUseTemporal = true;
    if (!kUseTemporal || pc.params.x > 0.5) {
        imageStore(historyOutput, outPixel, vec4(currentColor, 1.0));
        return;
    }

    // Depth-dilated motion vector (3x3 nearest-to-camera)
    vec2 texelSize = pc.internalSize.zw;
    float closestDepth = texture(depthBuffer, outUV).r;
    vec2 closestOffset = vec2(0.0);
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 off = vec2(float(x), float(y)) * texelSize;
            float d = texture(depthBuffer, outUV + off).r;
            if (d < closestDepth) {
                closestDepth = d;
                closestOffset = off;
            }
        }
    }
    vec2 motion = texture(motionVectors, outUV + closestOffset).rg;
    float motionMag = length(motion * pc.displaySize.xy);

    vec2 historyUV = outUV + motion;
    float historyValid = (historyUV.x >= 0.0 && historyUV.x <= 1.0 &&
                          historyUV.y >= 0.0 && historyUV.y <= 1.0) ? 1.0 : 0.0;
    vec3 historyColor = texture(historyInput, historyUV).rgb;

    // Tonemapped space for blending
    vec3 tmCurrent = tonemap(currentColor);
    vec3 tmHistory = tonemap(historyColor);

    // 5-tap cross neighborhood for variance (cheaper than 9-tap, sufficient)
    vec3 s0 = rgbToYCoCg(tmCurrent);
    vec3 s1 = rgbToYCoCg(tonemap(texture(sceneColor, outUV + vec2(-texelSize.x, 0.0)).rgb));
    vec3 s2 = rgbToYCoCg(tonemap(texture(sceneColor, outUV + vec2( texelSize.x, 0.0)).rgb));
    vec3 s3 = rgbToYCoCg(tonemap(texture(sceneColor, outUV + vec2(0.0, -texelSize.y)).rgb));
    vec3 s4 = rgbToYCoCg(tonemap(texture(sceneColor, outUV + vec2(0.0,  texelSize.y)).rgb));

    vec3 m1 = s0 + s1 + s2 + s3 + s4;
    vec3 m2 = s0*s0 + s1*s1 + s2*s2 + s3*s3 + s4*s4;
    vec3 mean = m1 / 5.0;
    vec3 variance = max(m2 / 5.0 - mean * mean, vec3(0.0));
    vec3 stddev = sqrt(variance);

    float gamma = 1.25;
    vec3 boxMin = mean - gamma * stddev;
    vec3 boxMax = mean + gamma * stddev;

    // Variance clip history
    vec3 tmHistYCoCg = rgbToYCoCg(tmHistory);
    vec3 clippedYCoCg = clipAABB(boxMin, boxMax, tmHistYCoCg);
    float clipDist = length(tmHistYCoCg - clippedYCoCg);
    tmHistory = yCoCgToRgb(clippedYCoCg);

    // --- Blend factor ---
    // Base: always start from current frame (sharp Lanczos).
    // Temporal blending only at edges with small fixed weight.
    // This provides AA without blurring smooth areas.

    // Edge detection: luminance variance in YCoCg
    float edgeStrength = smoothstep(0.04, 0.12, stddev.x);

    // Keep temporal reconstruction active continuously instead of freezing after
    // a small convergence window. Favor history on stable pixels and favor
    // current color when edge/motion risk is high to avoid blur/ghosting.
    float motionFactor = smoothstep(0.05, 1.5, motionMag);
    float currentBase = mix(0.12, 0.30, edgeStrength);
    float blendFactor = mix(currentBase, 0.85, motionFactor);

    // Disocclusion: replace stale history
    blendFactor = max(blendFactor, clamp(clipDist * 5.0, 0.0, 0.80));

    // Invalid history: use current frame
    blendFactor = mix(blendFactor, 1.0, 1.0 - historyValid);

    // Blend in tonemapped space, inverse-tonemap back to linear
    vec3 tmResult = mix(tmHistory, tmCurrent, blendFactor);
    vec3 result = inverseTonemap(tmResult);

    imageStore(historyOutput, outPixel, vec4(result, 1.0));
}
