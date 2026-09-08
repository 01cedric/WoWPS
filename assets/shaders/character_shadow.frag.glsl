#version 450

layout(set = 0, binding = 0) uniform sampler2D uTexture;

layout(set = 0, binding = 1) uniform ShadowParams {
    int alphaTest;
    int colorKeyBlack;
};

layout(location = 0) in vec2 TexCoord;

void main() {
    vec4 texColor = texture(uTexture, TexCoord);
    if (alphaTest != 0 && texColor.a < 0.5) discard;
    if (colorKeyBlack != 0) {
        float lum = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
        if (lum < 0.12) discard;
    }
}
