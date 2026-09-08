#version 450
// Explicit UBO layout is provided to PSBC by vk_ps4_clear_compile_options.
layout(set=0, binding=0, std140) uniform ClearColor { vec4 color; } pc;
layout(location=0) out vec4 outColor;
void main() { outColor = pc.color; }
