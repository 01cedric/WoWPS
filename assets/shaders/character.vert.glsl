#version 450

#define MAX_BONES 512u  // must match CharacterRenderer::MAX_BONES

layout(set = 0, binding = 0) uniform PerFrame {
    mat4 view;
    mat4 projection;
    mat4 lightSpaceMatrix;
    vec4 lightDir;
    vec4 lightColor;
    vec4 ambientColor;
    vec4 viewPos;
    vec4 fogColor;
    vec4 fogParams;
    vec4 shadowParams;
};

layout(push_constant) uniform Push {
    mat4 model;
} push;

layout(set = 2, binding = 0) readonly buffer BoneSSBO {
    mat4 bones[];
};

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aBoneWeights;
layout(location = 2) in uvec4 aBoneIndices;
layout(location = 3) in vec3 aNormal;
layout(location = 4) in vec2 aTexCoord;
layout(location = 5) in vec4 aTangent;

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 TexCoord;
layout(location = 3) out vec3 Tangent;
layout(location = 4) out vec3 Bitangent;

vec3 safeNormalize(vec3 v, vec3 fallback) {
    float len2 = dot(v, v);
    if (len2 > 1e-8) {
        return v * inversesqrt(len2);
    }
    return fallback;
}

vec3 fallbackTangent(vec3 n) {
    vec3 axis = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    return safeNormalize(cross(axis, n), vec3(1.0, 0.0, 0.0));
}

void main() {
    // Bone slots past the model's own count stay identity, so clamping keeps a
    // stray index harmless instead of reading past the buffer.
    uvec4 bi = min(aBoneIndices, uvec4(MAX_BONES - 1u));
    mat4 skinMat = bones[bi.x] * aBoneWeights.x
                 + bones[bi.y] * aBoneWeights.y
                 + bones[bi.z] * aBoneWeights.z
                 + bones[bi.w] * aBoneWeights.w;
    // Static scene vertices may have no bone influence at all.
    if (dot(aBoneWeights, vec4(1.0)) < 0.0001) skinMat = mat4(1.0);

    vec4 skinnedPos = skinMat * vec4(aPos, 1.0);
    vec3 skinnedNorm = mat3(skinMat) * aNormal;
    vec3 skinnedTan = mat3(skinMat) * aTangent.xyz;

    vec4 worldPos = push.model * skinnedPos;
    mat3 modelMat3 = mat3(push.model);
    FragPos = worldPos.xyz;
    Normal = modelMat3 * skinnedNorm;
    TexCoord = aTexCoord;

    // Gram-Schmidt re-orthogonalize tangent w.r.t. normal
    vec3 N = safeNormalize(Normal, vec3(0.0, 0.0, 1.0));
    vec3 T = safeNormalize(modelMat3 * skinnedTan, fallbackTangent(N));
    T = safeNormalize(T - dot(T, N) * N, fallbackTangent(N));
    vec3 B = safeNormalize(cross(N, T) * aTangent.w, safeNormalize(cross(N, fallbackTangent(N)), vec3(0.0, 1.0, 0.0)));

    Tangent = T;
    Bitangent = B;

    gl_Position = projection * view * worldPos;
}
