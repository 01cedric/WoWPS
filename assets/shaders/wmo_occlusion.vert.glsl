#version 450

layout(push_constant) uniform Push {
    mat4 mvp;
} push;

layout(location = 0) in vec3 aPos;

void main() {
    gl_Position = push.mvp * vec4(aPos, 1.0);
}
