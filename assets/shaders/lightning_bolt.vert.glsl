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
    float brightness;
} push;

layout(location = 0) in vec3 aPos;

layout(location = 0) out float vBrightness;

void main() {
    vBrightness = push.brightness;
    gl_Position = projection * view * vec4(aPos, 1.0);
}
