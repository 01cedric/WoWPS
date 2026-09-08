#version 450

layout(set = 0, binding = 0) uniform sampler2D uTileTexture;

layout(push_constant) uniform PushConstants {
    layout(offset = 16) vec4 tintColor;
};

layout(location = 0) in vec2 TexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texel = texture(uTileTexture, TexCoord);
    outColor = texel * tintColor;
}
