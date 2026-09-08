#version 450

layout(push_constant) uniform Push {
    float intensity;
} push;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(1.0, 1.0, 1.0, push.intensity * 0.6);
}
