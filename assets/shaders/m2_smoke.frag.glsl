#version 450

layout(location = 0) in float vLifeRatio;
layout(location = 1) in float vIsSpark;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 p = gl_PointCoord - vec2(0.5);
    float dist = length(p);
    if (dist > 0.5) discard;

    if (vIsSpark > 0.5) {
        float glow = smoothstep(0.5, 0.0, dist);
        float life = 1.0 - vLifeRatio;
        vec3 color = mix(vec3(1.0, 0.6, 0.1), vec3(1.0, 0.2, 0.0), vLifeRatio);
        outColor = vec4(color * glow, glow * life);
    } else {
        float edge = smoothstep(0.5, 0.3, dist);
        float fadeIn = smoothstep(0.0, 0.2, vLifeRatio);
        float fadeOut = 1.0 - smoothstep(0.6, 1.0, vLifeRatio);
        float alpha = edge * fadeIn * fadeOut * 0.4;
        outColor = vec4(vec3(0.5), alpha);
    }
}
