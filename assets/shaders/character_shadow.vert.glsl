#version 450

#define MAX_BONES 512u  // must match CharacterRenderer::MAX_BONES

layout(push_constant) uniform Push {
    mat4 lightSpaceMatrix;
    mat4 model;
} push;

layout(set = 1, binding = 0) readonly buffer BoneSSBO {
    mat4 bones[];
};

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aBoneWeights;
layout(location = 2) in uvec4 aBoneIndices;
layout(location = 3) in vec2 aTexCoord;

layout(location = 0) out vec2 TexCoord;

void main() {
    // Bone slots past the model's own count stay identity, so clamping keeps a
    // stray index harmless instead of reading past the buffer.
    uvec4 bi = min(aBoneIndices, uvec4(MAX_BONES - 1u));
    mat4 skinMat = bones[bi.x] * aBoneWeights.x
                 + bones[bi.y] * aBoneWeights.y
                 + bones[bi.z] * aBoneWeights.z
                 + bones[bi.w] * aBoneWeights.w;
    if (dot(aBoneWeights, vec4(1.0)) < 0.0001) skinMat = mat4(1.0);
    vec4 skinnedPos = skinMat * vec4(aPos, 1.0);
    TexCoord = aTexCoord;
    gl_Position = push.lightSpaceMatrix * push.model * skinnedPos;
}
