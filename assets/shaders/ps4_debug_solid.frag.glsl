#version 450

layout(push_constant) uniform PC {
    float time;
} pc;

layout(location = 0) out vec4 outColor;

// Animated color gradient: unmistakably not black, not a stale frame, and
// distinct enough in space and time to tell a real per-fragment execution
// from a solid fill that could come from anywhere else.
void main() {
    vec3 color = 0.5 + 0.5 * cos(pc.time + vec3(0.0, 2.0, 4.0) + gl_FragCoord.x * 0.002);
    outColor = vec4(color, 1.0);
}
