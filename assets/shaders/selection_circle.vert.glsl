#version 450

layout(push_constant) uniform Push {
    mat4 mvp;
} push;

layout(location = 0) in vec3 aPos;

layout(location = 0) out vec2 vLocalPos;

void main() {
    // The selection disc is authored in the XY plane. Using XZ collapses the
    // radial distance to one axis and renders as a thin line on some GPUs.
    vLocalPos = aPos.xy;
    gl_Position = push.mvp * vec4(aPos, 1.0);
}
