#version 450

// M2 ribbon emitter vertex shader.
// Ribbon geometry is generated CPU-side as a triangle strip.
// Vertex format: pos(3) + color(3) + alpha(1) + uv(2) = 9 floats.

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
layout(location = 1) in vec3 aColor;
layout(location = 2) in float aAlpha;
layout(location = 3) in vec2 aUV;

layout(location = 0) out vec3 vColor;
layout(location = 1) out float vAlpha;
layout(location = 2) out vec2 vUV;
layout(location = 3) out float vFogFactor;

void main() {
    vec4 worldPos = vec4(aPos, 1.0);
    vec4 viewPos4  = view * worldPos;
    gl_Position    = projection * viewPos4;

    float dist      = length(viewPos4.xyz);
    float fogStart  = fogParams.x;
    float fogEnd    = fogParams.y;
    vFogFactor      = clamp((fogEnd - dist) / max(fogEnd - fogStart, 0.001), 0.0, 1.0);

    vColor = aColor;
    vAlpha = aAlpha;
    vUV    = aUV;
}
