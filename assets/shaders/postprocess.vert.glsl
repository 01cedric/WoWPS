#version 450

layout(location = 0) out vec2 TexCoord;

void main() {
    // Fullscreen triangle trick: 3 vertices, no vertex buffer
    TexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(TexCoord * 2.0 - 1.0, 0.0, 1.0);
    // No Y-flip: scene textures use Vulkan convention (v=0 at top),
    // and NDC y=-1 already maps to framebuffer top, so the triangle
    // naturally samples the correct row without any inversion.
}
