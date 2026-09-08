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
layout(location = 1) in vec4 aColor;
layout(location = 2) in float aSize;
layout(location = 3) in float aTile;

layout(location = 0) out vec4 vColor;
layout(location = 1) out float vTile;
layout(location = 2) out float vFogVisibility;

void main() {
    vec4 viewPos4 = view * vec4(aPos, 1.0);
    float dist = -viewPos4.z;
    gl_PointSize = clamp(aSize * 500.0 / max(dist, 1.0), 1.0, 128.0);
    vColor = aColor;
    vTile = aTile;
    float worldDist = length(viewPos.xyz - aPos);
    float fogRange = max(fogParams.y - fogParams.x, 0.001);
    vFogVisibility = clamp((fogParams.y - worldDist) / fogRange, 0.0, 1.0);
    gl_Position = projection * viewPos4;
}
