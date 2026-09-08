#version 450

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

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aColor;
layout(location = 4) in vec4 aTangent;

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 TexCoord;
layout(location = 3) out vec4 VertColor;
layout(location = 4) out vec3 Tangent;
layout(location = 5) out vec3 Bitangent;

void main() {
    vec4 worldPos = push.model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;

    mat3 normalMatrix = mat3(push.model);
    Normal = normalMatrix * aNormal;
    TexCoord = aTexCoord;
    VertColor = aColor;

    // Compute TBN basis vectors for normal mapping
    vec3 T = normalize(normalMatrix * aTangent.xyz);
    vec3 N = normalize(Normal);
    // Gram-Schmidt re-orthogonalize
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * aTangent.w;

    Tangent = T;
    Bitangent = B;

    gl_Position = projection * view * worldPos;
}
