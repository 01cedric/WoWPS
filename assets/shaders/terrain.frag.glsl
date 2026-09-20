#version 450

layout(set = 0, binding = 0) uniform PerFrame {
    mat4 view;
    mat4 projection;
    mat4 lightSpaceMatrix;
    vec4 lightDir;
    vec4 lightColor;
    vec4 ambientColor;
    vec4 viewPos;
    vec4 fogColor;
    vec4 fogParams;
    vec4 shadowParams;
    vec4 playerPos;   // xyz = player world position, w = horizontal speed
    vec4 playerWake;  // xyz = trailing player position (springback reference)
    vec4 localLightPosRadius[64];
    vec4 localLightColorIntensity[64];
    ivec4 localLightMeta;
    mat4 nearLightSpaceMatrix;
    vec4 shadowAtlasParams; // near/far world texel, near distance, atlas enabled
};

layout(set = 1, binding = 0) uniform sampler2D uBaseTexture;
layout(set = 1, binding = 1) uniform sampler2D uLayer1Texture;
layout(set = 1, binding = 2) uniform sampler2D uLayer2Texture;
layout(set = 1, binding = 3) uniform sampler2D uLayer3Texture;
layout(set = 1, binding = 4) uniform sampler2D uLayer1Alpha;
layout(set = 1, binding = 5) uniform sampler2D uLayer2Alpha;
layout(set = 1, binding = 6) uniform sampler2D uLayer3Alpha;

layout(set = 1, binding = 7) uniform TerrainParams {
    int layerCount;
    int hasLayer1;
    int hasLayer2;
    int hasLayer3;
};

layout(set = 0, binding = 1) uniform sampler2DShadow uShadowMap;

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;
layout(location = 3) in vec2 LayerUV;

layout(location = 0) out vec4 outColor;

// One shadow-map texel, in the map's own UV, from the renderer rather than
// compiled in here.
//
// It was 1.0/4096, from when 4096 was the only map this renderer built. It has
// not been since extShadowQuality began choosing the size, and the console
// never uses it: at 1024 a three-by-three kernel stepping in 4096ths spans
// three quarters of one texel, so all nine taps land in the same texel and the
// filter returns exactly what one tap would - a hard aliased edge on the
// platform this was tuned for. The same number scales the normal offset below,
// so that was a quarter of what it should be there too. The stair-stepped edge
// and the acne were one bug.
//
// The fallback is for a frame UBO written with no shadow map at all, which the
// character preview does - there shadowParams.x is zero and none of this runs.
float shadowTexelSize() {
    return shadowParams.z > 0.0 ? shadowParams.z : (1.0 / 4096.0);
}

// Atlas-local PCF coordinates are clamped before atlas conversion so a
// filter footprint never reads the neighbouring cascade or unused atlas space.
vec2 shadowAtlasUV(vec2 uv, float texel, bool nearCascade) {
    uv = clamp(uv, vec2(0.5 * texel), vec2(1.0 - 0.5 * texel));
    if (shadowAtlasParams.w < 0.5) return uv;
    return nearCascade ? uv * vec2(0.5, 1.0)
                       : vec2(0.5, 0.0) + uv * vec2(0.25, 0.5);
}

// Orthographic receiver-plane gradient in cascade-local UV coordinates.
// Geometric normals keep normal-map detail out of shadow depth comparisons.
vec2 shadowDepthGradient(mat4 lightMatrix, vec3 normal) {
    vec3 rowX = vec3(lightMatrix[0][0], lightMatrix[1][0], lightMatrix[2][0]);
    vec3 rowY = vec3(lightMatrix[0][1], lightMatrix[1][1], lightMatrix[2][1]);
    vec3 rowZ = vec3(lightMatrix[0][2], lightMatrix[1][2], lightMatrix[2][2]);
    float zScale = length(rowZ);
    float facing = dot(normal, rowZ / max(zScale, 1e-8));
    // Bound the correction on surfaces almost parallel to the light rays.
    float denominator = (facing < 0.0 ? -1.0 : 1.0) * max(abs(facing), 0.1);
    return -2.0 * zScale / denominator * vec2(
        dot(normal, rowX) / max(dot(rowX, rowX), 1e-12),
        dot(normal, rowY) / max(dot(rowY, rowY), 1e-12));
}

float sampleShadowPlane(sampler2DShadow smap, vec3 coords, vec2 tap,
                        vec2 gradient, float texel, bool nearCascade) {
    vec2 localUV = clamp(tap, vec2(0.5 * texel), vec2(1.0 - 0.5 * texel));
    // Tap exactly at a texel centre: each comparison has its own receiver
    // plane depth. No mixed-reference hardware PCF footprint bias is needed.
    float depth = coords.z + dot(gradient, localUV - coords.xy);
    return texture(smap, vec3(shadowAtlasUV(localUV, texel, nearCascade), depth));
}

float sampleShadowPCF(sampler2DShadow smap, vec3 coords, float texel, bool nearCascade, vec2 gradient) {
    vec2 texelPos = coords.xy / texel - 0.5;
    vec2 base = floor(texelPos);
    vec2 f = texelPos - base;
    vec2 posLo = (base + 0.5) * texel;
    vec2 posHi = posLo + texel;
    // Reconstruct a continuous 2x2 visibility filter with four individually
    // plane-corrected comparisons; retain contact instead of widening bias.
    float lo = mix(sampleShadowPlane(smap, coords, posLo, gradient, texel, nearCascade),
                   sampleShadowPlane(smap, coords, vec2(posHi.x, posLo.y), gradient, texel, nearCascade), f.x);
    float hi = mix(sampleShadowPlane(smap, coords, vec2(posLo.x, posHi.y), gradient, texel, nearCascade),
                   sampleShadowPlane(smap, coords, posHi, gradient, texel, nearCascade), f.x);
    return mix(lo, hi, f.y);
}

// Bias is specified in world units, not a fixed fraction of a potentially
// 1950-yard depth range. The orthographic matrix's Z row converts it to depth.
vec3 shadowReceiverCoords(mat4 lightMatrix, vec3 pos, vec3 normal, vec3 ldir, float worldTexel) {
    float slope = 1.0 - abs(dot(normal, ldir));
    float normalOffset = min(max(worldTexel, 0.0) * 0.15, 0.025) * slope;
    vec4 lightPos = lightMatrix * vec4(pos + normal * normalOffset, 1.0);
    vec3 projected = lightPos.xyz / lightPos.w;
    projected.xy = projected.xy * 0.5 + 0.5;
    float depthPerWorldUnit = length(vec3(lightMatrix[0][2], lightMatrix[1][2], lightMatrix[2][2]));
    projected.z -= (0.005 + 0.035 * slope) * depthPerWorldUnit;
    return projected;
}

bool insideShadow(vec3 p) {
    return all(greaterThanEqual(p, vec3(0.0))) && all(lessThanEqual(p, vec3(1.0)));
}

float outdoorShadow(vec3 pos, vec3 normal, vec3 ldir) {
    float texel = shadowTexelSize();
    bool atlas = shadowAtlasParams.w > 0.5;
    float nearWeight = 0.0;
    float nearShadow = 1.0;
    if (atlas) {
        vec3 nearCoords = shadowReceiverCoords(nearLightSpaceMatrix, pos, normal, ldir, shadowAtlasParams.x);
        if (insideShadow(nearCoords)) {
            // Blend only at the near tile's outer edge, retaining full contact
            // detail throughout its useful interior. Depth edges fade too.
            vec3 edge = min(nearCoords, vec3(1.0) - nearCoords);
            nearWeight = smoothstep(0.0, 0.08, min(edge.x, edge.y))
                       * smoothstep(0.0, 0.02, edge.z);
            nearShadow = sampleShadowPCF(uShadowMap, nearCoords, texel, true, shadowDepthGradient(nearLightSpaceMatrix, normal));
            if (nearWeight >= 1.0) return mix(1.0, nearShadow, shadowParams.y);
        }
    }
    float farShadow = 1.0;
    vec3 farCoords = shadowReceiverCoords(lightSpaceMatrix, pos, normal, ldir, shadowAtlasParams.y);
    if (insideShadow(farCoords)) {
        farShadow = sampleShadowPCF(uShadowMap, farCoords, atlas ? texel * 2.0 : texel, false, shadowDepthGradient(lightSpaceMatrix, normal));
    }
    return mix(1.0, mix(farShadow, nearShadow, nearWeight), shadowParams.y);
}

vec3 localLightContribution(vec3 pos, vec3 normal, vec3 albedo) {
    vec3 sum = vec3(0.0);
    for (int i = 0; i < min(localLightMeta.x, 64); ++i) {
        vec3 toLight = localLightPosRadius[i].xyz - pos;
        float radius = localLightPosRadius[i].w;
        // Most city lights are outside this fragment's radius. Reject them
        // before sqrt; keep the authored attenuation for contributing lights.
        float distSquared = dot(toLight, toLight);
        if (radius <= 0.0 || distSquared >= radius * radius) continue;
        float dist = sqrt(distSquared);
        float attenuation = 1.0 - dist / radius;
        attenuation *= attenuation;
        float wrappedDiffuse = 0.22 + 0.78 * max(dot(normal, toLight / max(dist, 0.001)), 0.0);
        sum += albedo * localLightColorIntensity[i].rgb *
               (localLightColorIntensity[i].w * attenuation * wrappedDiffuse);
    }
    return sum;
}

float sampleAlpha(sampler2D tex, vec2 uv) {
    // Smooth 9-tap box near chunk edges to hide alpha-map seams;
    // blends gradually to avoid a visible ring at the transition.
    // Wider feather (8 texels) makes per-chunk alpha differences
    // bleed across the boundary so the chunk grid stops reading
    // as a hard step.
    vec2 edge = min(uv, 1.0 - uv);
    float border = min(edge.x, edge.y);
    float blurWeight = 1.0 - smoothstep(1.0 / 64.0, 8.0 / 64.0, border);
    float center = texture(tex, uv).r;
    if (blurWeight < 0.001) return center;
    // The nine bilinear taps form a separable four-texel kernel in each
    // axis: (1-f, 1, 1, f), where f is the fractional texel coordinate.
    // Pair adjacent coefficients and let the linear sampler interpolate
    // each pair. Four reads reproduce the same box filter, including the
    // clamp-to-edge border, without baking or enlarging the alpha texture.
    // Fixed +/- offsets are not equivalent at arbitrary sub-texel UVs.
    vec2 texelPos = uv * 64.0 - 0.5;
    vec2 base = floor(texelPos);
    vec2 f = texelPos - base;
    vec2 weightLo = 2.0 - f;
    vec2 weightHi = 1.0 + f;
    vec2 posLo = (base - 0.5 + 1.0 / weightLo) / 64.0;
    vec2 posHi = (base + 1.5 + f / weightHi) / 64.0;
    float avg = texture(tex, vec2(posLo.x, posLo.y)).r * weightLo.x * weightLo.y;
    avg += texture(tex, vec2(posHi.x, posLo.y)).r * weightHi.x * weightLo.y;
    avg += texture(tex, vec2(posLo.x, posHi.y)).r * weightLo.x * weightHi.y;
    avg += texture(tex, vec2(posHi.x, posHi.y)).r * weightHi.x * weightHi.y;
    avg *= 1.0 / 9.0;
    return mix(center, avg, blurWeight);
}

void main() {
    // Evaluate derivatives before material discard/divergent lighting paths.
    vec3 geometricNormal = cross(dFdx(FragPos), dFdy(FragPos));
    float geometricLength2 = dot(geometricNormal, geometricNormal);
    vec3 shadowNormal = geometricLength2 > 1e-12
        ? geometricNormal * inversesqrt(geometricLength2) : normalize(Normal);
    if (dot(shadowNormal, Normal) < 0.0) shadowNormal = -shadowNormal;
    vec4 baseColor = texture(uBaseTexture, TexCoord);

    // WoW terrain: layers are blended sequentially, each on top of the previous result.
    // Alpha=1 means the layer fully covers everything below; alpha=0 means invisible.
    vec4 finalColor = baseColor;
    if (hasLayer1 != 0) {
        float a1 = sampleAlpha(uLayer1Alpha, LayerUV);
        finalColor = mix(finalColor, texture(uLayer1Texture, TexCoord), a1);
    }
    if (hasLayer2 != 0) {
        float a2 = sampleAlpha(uLayer2Alpha, LayerUV);
        finalColor = mix(finalColor, texture(uLayer2Texture, TexCoord), a2);
    }
    if (hasLayer3 != 0) {
        float a3 = sampleAlpha(uLayer3Alpha, LayerUV);
        finalColor = mix(finalColor, texture(uLayer3Texture, TexCoord), a3);
    }

    vec3 norm = normalize(Normal);

    // Derivative-based normal mapping: perturb vertex normal using texture detail.
    // Fade out with distance and near chunk edges (dFdx/dFdy are invalid across
    // chunk draw-call boundaries, producing visible seams if not faded).
    float fragDist = length(viewPos.xyz - FragPos);
    float bumpFade = 1.0 - smoothstep(50.0, 125.0, fragDist);
    float edgeDist = min(min(LayerUV.x, 1.0 - LayerUV.x), min(LayerUV.y, 1.0 - LayerUV.y));
    bumpFade *= smoothstep(0.0, 0.06, edgeDist);
    if (bumpFade > 0.001) {
        float lum = dot(finalColor.rgb, vec3(0.299, 0.587, 0.114));
        float dLdx = dFdx(lum);
        float dLdy = dFdy(lum);
        vec3 dpdx = dFdx(FragPos);
        vec3 dpdy = dFdy(FragPos);
        float bumpStrength = 9.0 * bumpFade;
        vec3 perturbation = (dLdx * cross(norm, dpdy) + dLdy * cross(dpdx, norm)) * bumpStrength;
        vec3 candidate = norm - perturbation;
        float len2 = dot(candidate, candidate);
        norm = (len2 > 1e-8) ? candidate * inversesqrt(len2) : norm;
    }

    vec3 lightDir2 = normalize(-lightDir.xyz);
    vec3 ambient = ambientColor.rgb * finalColor.rgb;
    float diff = max(dot(norm, lightDir2), 0.0);
    vec3 diffuse = diff * lightColor.rgb * finalColor.rgb;

    float shadow = 1.0;
    if (shadowParams.x > 0.5) {
        vec3 ldir = normalize(-lightDir.xyz);
        shadow = outdoorShadow(FragPos, shadowNormal, ldir);
    }

    vec3 result = ambient + shadow * diffuse;
    result += localLightContribution(FragPos, norm, finalColor.rgb);

    float fogFactor = clamp((fogParams.y - fragDist) / (fogParams.y - fogParams.x), 0.0, 1.0);
    result = mix(fogColor.rgb, result, fogFactor);

    if (shadowParams.w > 0.5) result = (shadowParams.x > 0.5)
        ? vec3(shadow) : vec3(1.0, 0.0, 1.0);
    outColor = vec4(result, 1.0);
}
