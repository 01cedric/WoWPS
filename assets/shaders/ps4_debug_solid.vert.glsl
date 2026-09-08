#version 450

// PS4 black-screen investigation, second angle: a maximally minimal
// graphics-pipeline draw through the real CB-export path (fullscreen
// triangle from gl_VertexIndex - no vertex buffer, no descriptor sets),
// stripped of every bit of complexity our normal rendering has, to test
// whether descriptor/pipeline complexity itself is what keeps CB-exported
// content from reaching the display on this hardware.
void main() {
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
