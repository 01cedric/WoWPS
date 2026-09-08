#version 450

layout(set = 1, binding = 0) uniform sampler2D markerTexture;

layout(push_constant) uniform Push {
    mat4 model;
    float alpha;
    float grayscale;  // 0 = full colour, 1 = fully desaturated (trivial quests)
} push;

layout(location = 0) in vec2 TexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(markerTexture, TexCoord);
    if (texColor.a < 0.1) discard;
    float lum = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
    vec3 rgb = mix(texColor.rgb, vec3(lum), push.grayscale);
    outColor = vec4(rgb, texColor.a * push.alpha);
}
