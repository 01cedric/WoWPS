#version 450
// FSR 1.0 EASU (Edge Adaptive Spatial Upsampling) - Fragment Shader
// Based on AMD FidelityFX Super Resolution 1.0
// Implements edge-adaptive bilinear upsampling with directional filtering

layout(set = 0, binding = 0) uniform sampler2D uInput;

layout(push_constant) uniform FSRConstants {
    vec4 con0; // inputSize.xy, 1/inputSize.xy
    vec4 con1; // inputSize.xy / outputSize.xy, 0.5 * inputSize.xy / outputSize.xy
    vec4 con2; // outputSize.xy, 1/outputSize.xy
    vec4 con3; // sharpness, 0, 0, 0
} fsr;

layout(location = 0) in vec2 TexCoord;
layout(location = 0) out vec4 outColor;

// Fetch a texel with offset (in input pixels)
vec3 fsrFetch(vec2 p, vec2 off) {
    return textureLod(uInput, (p + off + 0.5) * fsr.con0.zw, 0.0).rgb;
}

void main() {
    vec2 tc = TexCoord;

    // Map output pixel to input space
    vec2 pp = tc * fsr.con2.xy; // output pixel position
    vec2 ip = pp * fsr.con1.xy - 0.5; // input pixel position (centered)
    vec2 fp = floor(ip);
    vec2 ff = ip - fp;

    // 12-tap filter: 4x3 grid around the pixel
    //  b c
    // e f g h
    // i j k l
    //  n o
    vec3 b = fsrFetch(fp, vec2( 0, -1));
    vec3 c = fsrFetch(fp, vec2( 1, -1));
    vec3 e = fsrFetch(fp, vec2(-1,  0));
    vec3 f = fsrFetch(fp, vec2( 0,  0));
    vec3 g = fsrFetch(fp, vec2( 1,  0));
    vec3 h = fsrFetch(fp, vec2( 2,  0));
    vec3 i = fsrFetch(fp, vec2(-1,  1));
    vec3 j = fsrFetch(fp, vec2( 0,  1));
    vec3 k = fsrFetch(fp, vec2( 1,  1));
    vec3 l = fsrFetch(fp, vec2( 2,  1));
    vec3 n = fsrFetch(fp, vec2( 0,  2));
    vec3 o = fsrFetch(fp, vec2( 1,  2));

    // Luma (use green channel as good perceptual approximation)
    float bL = b.g, cL = c.g, eL = e.g, fL = f.g;
    float gL = g.g, hL = h.g, iL = i.g, jL = j.g;
    float kL = k.g, lL = l.g, nL = n.g, oL = o.g;

    // Directional edge detection
    // Compute gradients in 4 directions (N-S, E-W, NE-SW, NW-SE)
    float dc = cL - jL;
    float db = bL - kL;
    float de = eL - hL;
    float di = iL - lL;

    // Length of the edge in each direction
    float lenH = abs(eL - fL) + abs(fL - gL) + abs(iL - jL) + abs(jL - kL);
    float lenV = abs(bL - fL) + abs(fL - jL) + abs(cL - gL) + abs(gL - kL);

    // Determine dominant edge direction
    float dirH = lenV / (lenH + lenV + 1e-7);
    float dirV = lenH / (lenH + lenV + 1e-7);

    // Bilinear weights
    float w1 = (1.0 - ff.x) * (1.0 - ff.y);
    float w2 = ff.x * (1.0 - ff.y);
    float w3 = (1.0 - ff.x) * ff.y;
    float w4 = ff.x * ff.y;

    // Edge-aware sharpening: boost weights along edges
    float sharpness = fsr.con3.x;
    float edgeStr = max(abs(lenH - lenV) / (lenH + lenV + 1e-7), 0.0);
    float sharp = mix(0.0, sharpness, edgeStr);

    // Sharpen bilinear by pulling toward nearest texel
    float maxW = max(max(w1, w2), max(w3, w4));
    w1 = mix(w1, float(w1 == maxW), sharp * 0.25);
    w2 = mix(w2, float(w2 == maxW), sharp * 0.25);
    w3 = mix(w3, float(w3 == maxW), sharp * 0.25);
    w4 = mix(w4, float(w4 == maxW), sharp * 0.25);

    // Normalize
    float wSum = w1 + w2 + w3 + w4;
    w1 /= wSum; w2 /= wSum; w3 /= wSum; w4 /= wSum;

    // Final color: weighted blend of the 4 nearest texels with edge awareness
    vec3 color = f * w1 + g * w2 + j * w3 + k * w4;

    // Optional: blend in some of the surrounding texels for anti-aliasing
    float aa = 0.125 * edgeStr;
    color = mix(color, (b + c + e + h + i + l + n + o) / 8.0, aa * 0.15);

    outColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
