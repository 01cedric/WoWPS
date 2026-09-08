#version 450

// FXAA 3.11 - Fast Approximate Anti-Aliasing post-process pass.
// Reads the resolved scene color and outputs a smoothed result.
// Push constant: rcpFrame, sharpness, intoxication (0=sober, 1=smashed).

layout(set = 0, binding = 0) uniform sampler2D uScene;

layout(location = 0) in vec2 TexCoord;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PC {
    vec2  rcpFrame;
    float sharpness;    // <0 = scale/effects only, 0 = no sharpen, 2 = max
    float intoxication;
} pc;

// Quality tuning
#define FXAA_EDGE_THRESHOLD      (1.0/8.0)   // minimum edge contrast to process
#define FXAA_EDGE_THRESHOLD_MIN  (1.0/24.0)  // ignore very dark regions
#define FXAA_SEARCH_STEPS        12
#define FXAA_SEARCH_THRESHOLD    (1.0/4.0)
#define FXAA_SUBPIX              0.75
#define FXAA_SUBPIX_TRIM         (1.0/4.0)
#define FXAA_SUBPIX_TRIM_SCALE   (1.0/(1.0 - FXAA_SUBPIX_TRIM))
#define FXAA_SUBPIX_CAP          (3.0/4.0)

float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
    float drunk  = clamp(pc.intoxication, 0.0, 1.0);
    vec2 uv = TexCoord;
    // This condition is uniform across the draw. Ordinary play needs no
    // trigonometric work in the full-screen pass.
    if (drunk > 0.0) {
        uv += vec2(sin(TexCoord.y * 34.0) * pc.rcpFrame.x,
                   cos(TexCoord.x * 29.0) * pc.rcpFrame.y) * (3.0 * drunk);
    }
    vec2 rcp     = pc.rcpFrame;

    // --- Centre and cardinal neighbours ---
    vec3 rgbM  = texture(uScene, uv).rgb;
    if (drunk > 0.0) {
        vec2 radius = rcp * mix(1.0, 5.0, drunk);
        vec3 blur = rgbM;
        blur += texture(uScene, uv + vec2( radius.x, 0.0)).rgb;
        blur += texture(uScene, uv + vec2(-radius.x, 0.0)).rgb;
        blur += texture(uScene, uv + vec2(0.0,  radius.y)).rgb;
        blur += texture(uScene, uv + vec2(0.0, -radius.y)).rgb;
        blur += texture(uScene, uv + radius).rgb;
        blur += texture(uScene, uv - radius).rgb;
        blur += texture(uScene, uv + vec2(radius.x, -radius.y)).rgb;
        blur += texture(uScene, uv + vec2(-radius.x, radius.y)).rgb;
        rgbM = mix(rgbM, blur / 9.0, 0.75 * drunk);
    }
    if (pc.sharpness < 0.0) {
        outColor = vec4(rgbM, 1.0);
        return;
    }
    vec3 rgbN  = texture(uScene, uv + vec2( 0.0, -1.0) * rcp).rgb;
    vec3 rgbS  = texture(uScene, uv + vec2( 0.0,  1.0) * rcp).rgb;
    vec3 rgbE  = texture(uScene, uv + vec2( 1.0,  0.0) * rcp).rgb;
    vec3 rgbW  = texture(uScene, uv + vec2(-1.0,  0.0) * rcp).rgb;

    float lumaN = luma(rgbN);
    float lumaS = luma(rgbS);
    float lumaE = luma(rgbE);
    float lumaW = luma(rgbW);
    float lumaM = luma(rgbM);

    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float range   = lumaMax - lumaMin;

    // Early exit on smooth regions
    if (range < max(FXAA_EDGE_THRESHOLD_MIN, lumaMax * FXAA_EDGE_THRESHOLD)) {
        outColor = vec4(rgbM, 1.0);
        return;
    }

    // --- Diagonal neighbours ---
    vec3 rgbNW = texture(uScene, uv + vec2(-1.0, -1.0) * rcp).rgb;
    vec3 rgbNE = texture(uScene, uv + vec2( 1.0, -1.0) * rcp).rgb;
    vec3 rgbSW = texture(uScene, uv + vec2(-1.0,  1.0) * rcp).rgb;
    vec3 rgbSE = texture(uScene, uv + vec2( 1.0,  1.0) * rcp).rgb;

    float lumaNW = luma(rgbNW);
    float lumaNE = luma(rgbNE);
    float lumaSW = luma(rgbSW);
    float lumaSE = luma(rgbSE);

    // --- Sub-pixel blend factor ---
    float lumaL  = (lumaN + lumaS + lumaE + lumaW) * 0.25;
    float rangeL = abs(lumaL - lumaM);
    float blendL = max(0.0, (rangeL / range) - FXAA_SUBPIX_TRIM) * FXAA_SUBPIX_TRIM_SCALE;
    blendL = min(FXAA_SUBPIX_CAP, blendL) * FXAA_SUBPIX;

    // --- Edge orientation (horizontal vs. vertical) ---
    float edgeHorz =
          abs(-2.0*lumaW + lumaNW + lumaSW)
        + 2.0*abs(-2.0*lumaM + lumaN + lumaS)
        + abs(-2.0*lumaE + lumaNE + lumaSE);
    float edgeVert =
          abs(-2.0*lumaS + lumaSW + lumaSE)
        + 2.0*abs(-2.0*lumaM + lumaW + lumaE)
        + abs(-2.0*lumaN + lumaNW + lumaNE);

    bool  horzSpan  = (edgeHorz >= edgeVert);
    float lengthSign = horzSpan ? rcp.y : rcp.x;

    float luma1 = horzSpan ? lumaN : lumaW;
    float luma2 = horzSpan ? lumaS : lumaE;
    float grad1 = abs(luma1 - lumaM);
    float grad2 = abs(luma2 - lumaM);
    lengthSign = (grad1 >= grad2) ? -lengthSign : lengthSign;

    // --- Edge search ---
    vec2 posB  = uv;
    vec2 offNP = horzSpan ? vec2(rcp.x, 0.0) : vec2(0.0, rcp.y);
    if (!horzSpan) posB.x += lengthSign * 0.5;
    if ( horzSpan) posB.y += lengthSign * 0.5;

    float lumaMLSS       = lumaM - (luma1 + luma2) * 0.5;
    float gradientScaled = max(grad1, grad2) * 0.25;

    vec2  posN = posB - offNP;
    vec2  posP = posB + offNP;
    bool  done1 = false, done2 = false;
    float lumaEnd1 = 0.0, lumaEnd2 = 0.0;

    for (int i = 0; i < FXAA_SEARCH_STEPS; ++i) {
        if (!done1) lumaEnd1 = luma(texture(uScene, posN).rgb) - lumaMLSS;
        if (!done2) lumaEnd2 = luma(texture(uScene, posP).rgb) - lumaMLSS;
        done1 = done1 || (abs(lumaEnd1) >= gradientScaled * FXAA_SEARCH_THRESHOLD);
        done2 = done2 || (abs(lumaEnd2) >= gradientScaled * FXAA_SEARCH_THRESHOLD);
        if (done1 && done2) break;
        if (!done1) posN -= offNP;
        if (!done2) posP += offNP;
    }

    float dstN = horzSpan ? (uv.x - posN.x) : (uv.y - posN.y);
    float dstP = horzSpan ? (posP.x - uv.x) : (posP.y - uv.y);
    bool  dirN = (dstN < dstP);
    float lumaEndFinal = dirN ? lumaEnd1 : lumaEnd2;

    float spanLength      = dstN + dstP;
    float pixelOffset     = (dirN ? dstN : dstP) / spanLength;
    bool  goodSpan        = ((lumaEndFinal < 0.0) != (lumaMLSS < 0.0));
    float pixelOffsetFinal = max(goodSpan ? pixelOffset : 0.0, blendL);

    vec2 finalUV = uv;
    if ( horzSpan) finalUV.y += pixelOffsetFinal * lengthSign;
    if (!horzSpan) finalUV.x += pixelOffsetFinal * lengthSign;

    vec3 fxaaResult = texture(uScene, finalUV).rgb;
    fxaaResult = mix(fxaaResult, rgbM, 0.75 * drunk);

    // Post-FXAA contrast-adaptive sharpening (unsharp mask).
    // Counteracts FXAA's sub-pixel blur when sharpness > 0.
    if (pc.sharpness > 0.0) {
        // These exact samples were already needed for the edge test.
        vec3 blur = (rgbW + rgbE + rgbN + rgbS) * 0.25;
        // scale sharpness from [0,2] to a modest [0, 0.3] boost factor
        float s = pc.sharpness * 0.15;
        fxaaResult = clamp(fxaaResult + s * (fxaaResult - blur), 0.0, 1.0);
    }

    outColor = vec4(fxaaResult, 1.0);
}
