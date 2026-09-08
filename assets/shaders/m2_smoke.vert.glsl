#version 450

layout(set = 0, binding = 0) uniform PerFrame {
    mat4 view;
    mat4 projection;
    mat4 lightSpaceMatrix;
    vec4 lightDir;
    vec4 lightColor;
    vec4 ambientColor;
    vec4 viewPos;
    vec4 fogColor;
    vec4 fogParams;
    vec4 shadowParams;
};

layout(push_constant) uniform Push {
    float screenHeight;
} push;

layout(location = 0) in vec3 aPos;
layout(location = 1) in float aLifeRatio;
layout(location = 2) in float aSize;
layout(location = 3) in float aIsSpark;

layout(location = 0) out float vLifeRatio;
layout(location = 1) out float vIsSpark;

void main() {
    vec4 viewPos4 = view * vec4(aPos, 1.0);
    float dist = -viewPos4.z;
    float scale = aIsSpark > 0.5 ? 0.12 : 0.3;
    gl_PointSize = clamp(aSize * scale * push.screenHeight / max(dist, 1.0), 1.0, 64.0);
    vLifeRatio = aLifeRatio;
    vIsSpark = aIsSpark;
    gl_Position = projection * viewPos4;
}
