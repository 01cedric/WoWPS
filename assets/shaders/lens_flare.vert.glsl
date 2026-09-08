#version 450

layout(push_constant) uniform Push {
    vec2 position;
    float size;
    float aspectRatio;
} push;

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

layout(location = 0) out vec2 UV;

void main() {
    UV = aUV;
    vec2 scaled = aPos * push.size;
    scaled.x /= push.aspectRatio;
    gl_Position = vec4(scaled + push.position, 0.0, 1.0);
}
