#version 450

// Hierarchical-Z depth pyramid builder.
// Builds successive mip levels from the scene depth buffer.
// Each 2×2 block is reduced to its MAXIMUM depth (farthest/largest value).
// This is conservative for occlusion: an object is only culled when its nearest
// depth exceeds the farthest occluder depth in the pyramid region.
//
// Two modes controlled by push constant:
//   mipLevel == 0: Sample from the source depth texture (mip 0 of the full-res depth).
//   mipLevel  > 0: Sample from the previous HiZ mip level.

layout(local_size_x = 8, local_size_y = 8) in;

// Source depth texture (full-resolution scene depth, or previous mip via same image)
layout(set = 0, binding = 0) uniform sampler2D srcDepth;

// Destination mip level (written as storage image)
layout(r32f, set = 0, binding = 1) uniform writeonly image2D dstMip;

layout(push_constant) uniform PushConstants {
    ivec2 dstSize;  // Width and height of the destination mip level
    int   mipLevel; // Current mip level being built (0 = from scene depth)
};

void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    if (pos.x >= dstSize.x || pos.y >= dstSize.y) return;

    // Each output texel covers a 2×2 block of the source.
    // Use texelFetch for precise texel access (no filtering).
    ivec2 srcPos = pos * 2;

    float d00, d10, d01, d11;

    if (mipLevel == 0) {
        // Sample from full-res scene depth (sampler2D, lod 0)
        d00 = texelFetch(srcDepth, srcPos + ivec2(0, 0), 0).r;
        d10 = texelFetch(srcDepth, srcPos + ivec2(1, 0), 0).r;
        d01 = texelFetch(srcDepth, srcPos + ivec2(0, 1), 0).r;
        d11 = texelFetch(srcDepth, srcPos + ivec2(1, 1), 0).r;
    } else {
        // Sample from previous HiZ mip level (mipLevel - 1)
        d00 = texelFetch(srcDepth, srcPos + ivec2(0, 0), mipLevel - 1).r;
        d10 = texelFetch(srcDepth, srcPos + ivec2(1, 0), mipLevel - 1).r;
        d01 = texelFetch(srcDepth, srcPos + ivec2(0, 1), mipLevel - 1).r;
        d11 = texelFetch(srcDepth, srcPos + ivec2(1, 1), mipLevel - 1).r;
    }

    // Conservative maximum (standard depth buffer: 0=near, 1=far).
    // We store the farthest (largest) depth in each 2×2 block.
    // An object is occluded only when its nearest depth > the farthest occluder
    // depth in the covered screen region - guaranteeing it's behind EVERYTHING.
    float maxDepth = max(max(d00, d10), max(d01, d11));

    imageStore(dstMip, pos, vec4(maxDepth));
}
