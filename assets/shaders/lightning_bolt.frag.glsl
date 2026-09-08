#version 450

layout(location = 0) in float vBrightness;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 color = mix(vec3(0.6, 0.8, 1.0), vec3(1.0), vBrightness * 0.5);
    outColor = vec4(color, vBrightness);
}
