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

layout(set = 1, binding = 0) uniform sampler2D uTexture;

layout(set = 1, binding = 1) uniform BasicMaterial {
    vec4 color;
    vec3 lightPos;
    int useTexture;
};

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 ambient = 0.3 * vec3(1.0);
    vec3 norm = normalize(Normal);
    vec3 lightDir2 = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir2), 0.0);
    vec3 diffuse = diff * vec3(1.0);

    vec3 viewDir2 = normalize(viewPos.xyz - FragPos);
    vec3 reflectDir = reflect(-lightDir2, norm);
    float spec = pow(max(dot(viewDir2, reflectDir), 0.0), 32.0);
    vec3 specular = 0.5 * spec * vec3(1.0);

    vec3 result = ambient + diffuse + specular;

    if (useTexture != 0) {
        outColor = texture(uTexture, TexCoord) * vec4(result, 1.0);
    } else {
        outColor = color * vec4(result, 1.0);
    }
}
