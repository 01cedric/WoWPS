#version 450
layout(location=0) in vec2 TexCoord;
layout(location=0) out vec4 FragColor;
layout(set=0,binding=0) uniform sampler2D sourceImage;
layout(std140,set=0,binding=1) uniform BloomUniforms { vec4 parameters; } bloom;
void main() {
    // Screen blending uses only the separate blurred source. Scene attachment
    // remains the destination, never a sampled input to its own draw.
    FragColor=vec4(clamp(texture(sourceImage,TexCoord).rgb*bloom.parameters.w,
                         vec3(0.0),vec3(1.0)),0.0);
}
