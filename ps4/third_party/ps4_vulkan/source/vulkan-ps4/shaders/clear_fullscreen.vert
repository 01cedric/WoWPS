#version 450
// Explicit oversized triangle, covering all four viewport corners. No fetch,
// descriptors or firmware-embedded shader geometry is involved.
void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
