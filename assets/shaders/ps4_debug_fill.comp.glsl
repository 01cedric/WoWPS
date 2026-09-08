#version 450
layout(local_size_x = 8, local_size_y = 8) in;

// PS4 black-screen investigation: a compute shader writes directly into the
// swapchain image's own backing memory (see vkPs4GetSwapchainImageMemory in
// ps4_vulkan) via a storage buffer aliasing that memory, bypassing the
// graphics-pipeline color-buffer export entirely. CB export has been proven,
// across every tile mode / memory source / draw type tested, to never
// produce visible output on this hardware - this is a different GPU write
// path (buffer store, not CB export) to determine whether that one reaches
// the display.
layout(push_constant) uniform PC {
    uint width;
    uint height;
    uint pitchPixels;
    float time;
} pc;

layout(set = 0, binding = 0, std430) buffer PixelBuffer {
    uint pixels[];
};

void main() {
    uint x = gl_GlobalInvocationID.x;
    uint y = gl_GlobalInvocationID.y;
    if (x >= pc.width || y >= pc.height) return;

    // Animated diagonal stripes: unmistakably not a stale image or a solid
    // clear color, and moving over time proves the writes are live.
    float stripe = mod(float(x + y) * 0.05 + pc.time * 20.0, 32.0);
    uint r, g, b;
    if (stripe < 16.0) {
        r = 255u; g = 32u; b = 32u;
    } else {
        r = 32u; g = 255u; b = 32u;
    }
    uint a = 255u;

    // Swapchain format is VK_FORMAT_B8G8R8A8_UNORM: byte order in memory is
    // B,G,R,A. Packed little-endian into a uint32: (A<<24)|(R<<16)|(G<<8)|B.
    uint packedColor = (a << 24) | (r << 16) | (g << 8) | b;

    uint idx = y * pc.pitchPixels + x;
    pixels[idx] = packedColor;
}
