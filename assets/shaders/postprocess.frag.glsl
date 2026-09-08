#version 450

layout(set = 0, binding = 0) uniform sampler2D uScene;

layout(location = 0) in vec2 TexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 hdr = texture(uScene, TexCoord).rgb;
    // Shoulder tone map
    vec3 mapped = hdr;
    for (int i = 0; i < 3; i++) {
        if (mapped[i] > 0.9) {
            float excess = mapped[i] - 0.9;
            mapped[i] = 0.9 + 0.1 * excess / (excess + 0.1);
        }
    }
    outColor = vec4(mapped, 1.0);
}
