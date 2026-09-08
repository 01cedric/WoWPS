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

layout(location = 0) in vec3 aPos;
layout(location = 1) in float aSize;
layout(location = 2) in float aAlpha;

layout(location = 0) out float vAlpha;

void main() {
    gl_PointSize = aSize;
    vAlpha = aAlpha;
    gl_Position = projection * view * vec4(aPos, 1.0);
}
