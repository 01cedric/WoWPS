#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0) uniform sampler2D depthBuffer;
layout(set = 0, binding = 1, rg16f) uniform writeonly image2D motionVectors;

layout(push_constant) uniform PushConstants {
    mat4 prevViewProjection;   // previous jittered VP
    mat4 invCurrentViewProj;   // inverse(current jittered VP)
} pc;

void main() {
    ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 imgSize = imageSize(motionVectors);
    if (pixelCoord.x >= imgSize.x || pixelCoord.y >= imgSize.y) return;

    float depth = texelFetch(depthBuffer, pixelCoord, 0).r;

    // Pixel center UV and NDC
    vec2 uv = (vec2(pixelCoord) + 0.5) / vec2(imgSize);
    vec2 ndc = uv * 2.0 - 1.0;

    // Reconstruct current world position from current frame depth.
    vec4 clipPos = vec4(ndc, depth, 1.0);
    vec4 worldPos = pc.invCurrentViewProj * clipPos;
    if (abs(worldPos.w) < 1e-6) {
        imageStore(motionVectors, pixelCoord, vec4(0.0, 0.0, 0.0, 0.0));
        return;
    }
    worldPos /= worldPos.w;

    // Project reconstructed world position into previous frame clip space.
    vec4 prevClip = pc.prevViewProjection * worldPos;
    if (abs(prevClip.w) < 1e-6) {
        imageStore(motionVectors, pixelCoord, vec4(0.0, 0.0, 0.0, 0.0));
        return;
    }
    vec2 prevNdc = prevClip.xy / prevClip.w;
    vec2 prevUV = prevNdc * 0.5 + 0.5;

    vec2 currentUV = uv;
    vec2 motion = prevUV - currentUV;

    imageStore(motionVectors, pixelCoord, vec4(motion, 0.0, 0.0));
}
