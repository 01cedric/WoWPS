#version 450

layout(push_constant) uniform Push {
    vec4 rect; // x, y, w, h in 0..1 screen space
} push;

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

layout(location = 0) out vec2 TexCoord;

void main() {
    TexCoord = aUV;
    vec2 screenPos = push.rect.xy + aPos * push.rect.zw;
    gl_Position = vec4(screenPos * 2.0 - 1.0, 0.0, 1.0);
}
