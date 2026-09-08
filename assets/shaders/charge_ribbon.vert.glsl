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
layout(location = 1) in float aAlpha;
layout(location = 2) in float aHeat;
layout(location = 3) in float aHeight;

layout(location = 0) out float vAlpha;
layout(location = 1) out float vHeat;
layout(location = 2) out float vHeight;

void main() {
    vAlpha = aAlpha;
    vHeat = aHeat;
    vHeight = aHeight;
    gl_Position = projection * view * vec4(aPos, 1.0);
}
