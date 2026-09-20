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
    mat4 model;
    vec4 celestialColor; // xyz = color, w = unused
    float intensity;
    float moonPhase;
    float animTime;
} push;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;

layout(location = 0) out vec2 TexCoord;

void main() {
    TexCoord = aTexCoord;
    // Sky object: remove camera translation so celestial bodies are at infinite distance
    mat4 rotView = mat4(mat3(view));
    // A celestial disc is a camera-facing billboard, not an XY world plane.
    // The old world quad became edge-on looking toward the horizon, exactly
    // where silhouettes and the forward-scattering rays should meet it.
    vec4 center = rotView * vec4(push.model[3].xyz, 1.0);
    center.xy += aPos.xy * vec2(length(push.model[0].xyz), length(push.model[1].xyz));
    gl_Position = projection * center;
    gl_Position.z = gl_Position.w; // sky depth; independent of terrain far clip
}
