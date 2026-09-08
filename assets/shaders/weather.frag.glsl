#version 450

layout(push_constant) uniform Push {
    float particleSize;
    float pad0;
    float pad1;
    float pad2;
    vec4 particleColor;
} push;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 p = gl_PointCoord - vec2(0.5);
    float dist = length(p);
    if (dist > 0.5) discard;
    float alpha = push.particleColor.a * smoothstep(0.5, 0.2, dist);
    outColor = vec4(push.particleColor.rgb, alpha);
}
