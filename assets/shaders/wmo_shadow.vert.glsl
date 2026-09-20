#version 450
layout(push_constant) uniform Push {
    mat4 lightSpaceMatrix;
    mat4 model;
} push;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 0) out vec2 TexCoord;
void main() {
    TexCoord = aTexCoord;
    gl_Position = push.lightSpaceMatrix * push.model * vec4(aPos, 1.0);
}
