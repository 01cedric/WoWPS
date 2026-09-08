#version 450

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 color;
} push;

layout(location = 0) in vec2 vLocalPos;

layout(location = 0) out vec4 outColor;

void main() {
    float r = length(vLocalPos);
    float ring = smoothstep(0.93, 0.97, r) * smoothstep(1.0, 0.97, r);
    float inward = (1.0 - smoothstep(0.0, 0.93, r)) * 0.15;
    float alpha = max(ring, inward);
    if (alpha < 0.01) discard;
    outColor = vec4(push.color.rgb, alpha);
}
