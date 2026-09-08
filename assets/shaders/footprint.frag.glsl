#version 450

layout(set = 1, binding = 0) uniform sampler2D footprintTexture;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 tint;
} push;

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

void main() {
    float mask = texture(footprintTexture, texCoord).a;
    float alpha = mask * push.tint.a;
    if (alpha < 0.015) discard;
    outColor = vec4(push.tint.rgb, alpha);
}
