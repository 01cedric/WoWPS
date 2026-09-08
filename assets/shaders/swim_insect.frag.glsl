#version 450

layout(location = 0) in float vAlpha;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 p = gl_PointCoord - vec2(0.5);
    float dist = length(p);
    if (dist > 0.5) discard;
    float alpha = smoothstep(0.5, 0.05, dist) * vAlpha;
    // Dark brown/black insect color
    outColor = vec4(0.12, 0.08, 0.05, alpha);
}
