#version 450

layout(location = 0) in float vAlpha;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 p = gl_PointCoord - vec2(0.5);
    float dist = length(p);
    if (dist > 0.5) discard;
    float ring = smoothstep(0.5, 0.4, dist) - smoothstep(0.38, 0.28, dist);
    float highlight = smoothstep(0.3, 0.1, length(p - vec2(-0.15, 0.15))) * 0.5;
    float alpha = (ring + highlight) * vAlpha;
    outColor = vec4(0.8, 0.9, 1.0, alpha);
}
