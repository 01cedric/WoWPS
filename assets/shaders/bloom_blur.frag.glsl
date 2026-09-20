#version 450
layout(location=0) in vec2 TexCoord;
layout(location=0) out vec4 FragColor;
layout(set=0,binding=0) uniform sampler2D sourceImage;
layout(std140,set=0,binding=1) uniform BloomUniforms { vec4 parameters; } bloom;

vec3 bloomThreshold(vec3 color) {
    float brightness=max(max(color.r,color.g),color.b);
    // Soft knee preserves colored emissive highlights without lifting shadows.
    return color*smoothstep(0.66,0.90,brightness);
}
vec3 bloomSample(vec2 uv) {
    vec3 color=max(texture(sourceImage,uv).rgb,vec3(0.0));
    return bloom.parameters.z>0.5 ? bloomThreshold(color) : color;
}
void main() {
    vec2 stepUV=bloom.parameters.xy;
    vec3 color=bloomSample(TexCoord)*0.375;
    if (bloom.parameters.z>0.5) {
        // Threshold is nonlinear: retain individual scene samples during
        // extraction, otherwise dim and bright neighbors would mix first.
        color+=(bloomSample(TexCoord-stepUV)+bloomSample(TexCoord+stepUV))*0.25;
        color+=(bloomSample(TexCoord-stepUV*2.0)+bloomSample(TexCoord+stepUV*2.0))*0.0625;
    } else {
        // Linear blur at texel centers: 0.25 at +/-1 plus 0.0625 at
        // +/-2 is exactly 0.3125 at +/-1.2 with the linear clamp sampler.
        // Three fetches replace five, including the clamped edge kernel.
        color+=(bloomSample(TexCoord-stepUV*1.2)+bloomSample(TexCoord+stepUV*1.2))*0.3125;
    }
    FragColor=vec4(color,0.0);
}
