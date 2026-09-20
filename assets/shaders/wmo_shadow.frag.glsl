#version 450
// Same material descriptor used by the main pass, bound here at set 0.
layout(set = 0, binding = 0) uniform sampler2D uTexture;
layout(set = 0, binding = 1) uniform WMOMaterial {
    int hasTexture;
    int alphaTest;
};
layout(location = 0) in vec2 TexCoord;
void main() {
    // Explicit PS4 depth export; discarded fragments still produce no depth.
    // Set before the opaque fast return as every surviving path must write it.
    gl_FragDepth = gl_FragCoord.z;
    // Opaque layers keep their merged geometry-only path. For authored cutouts,
    // use the same diffuse texture/sampler, base UV and 0.5 cutoff as the scene.
    if (alphaTest != 0) {
        vec4 texColor = hasTexture != 0 ? texture(uTexture, TexCoord) : vec4(1.0);
        if (alphaTest != 0 && texColor.a < 0.5) discard;
    }
}
