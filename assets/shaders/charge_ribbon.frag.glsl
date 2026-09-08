#version 450

layout(location = 0) in float vAlpha;
layout(location = 1) in float vHeat;
layout(location = 2) in float vHeight;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 top = vec3(1.0, 0.2, 0.0);
    vec3 mid = vec3(1.0, 0.5, 0.0);
    vec3 color = mix(mid, top, vHeight);
    color = mix(color, vec3(1.0, 0.8, 0.3), vHeat * 0.5);
    float alpha = vAlpha * smoothstep(0.0, 0.3, vHeight);
    outColor = vec4(color, alpha);
}
