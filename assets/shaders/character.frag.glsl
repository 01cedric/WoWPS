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
};

layout(set = 1, binding = 0) uniform sampler2D uTexture;

layout(set = 1, binding = 1) uniform CharMaterial {
    float opacity;
    int alphaTest;
    int colorKeyBlack;
    int unlit;
    float emissiveBoost;
    // Keep these as scalar floats to match the C++ UBO packing. A std140 vec3
    // would insert padding here and shift the following material flags.
    float emissiveTintR;
    float emissiveTintG;
    float emissiveTintB;
    float specularIntensity;
    int enableNormalMap;
    int enablePOM;
    float pomScale;
    int pomMaxSamples;
    float heightMapVariance;
    float normalMapStrength;
    int hairMaterial;
    vec4 uvRow0;
    vec4 uvRow1;
};

layout(set = 1, binding = 2) uniform sampler2D uNormalHeightMap;

layout(set = 0, binding = 1) uniform sampler2DShadow uShadowMap;

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;
layout(location = 3) in vec3 Tangent;
layout(location = 4) in vec3 Bitangent;

layout(location = 0) out vec4 outColor;

const int PREVIEW_SIMPLE_TEXTURE_MODE = -31336;

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

float sampleShadowPCF(sampler2DShadow smap, vec3 coords, float texel) {
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            shadow += texture(smap, vec3(coords.xy + vec2(x, y) * texel, coords.z));
        }
    }
    return shadow / 9.0;
}

vec3 localLightContribution(vec3 pos, vec3 normal, vec3 albedo) {
    vec3 sum = vec3(0.0);
    for (int i = 0; i < min(localLightMeta.x, 64); ++i) {
        vec3 toLight = localLightPosRadius[i].xyz - pos;
        float dist = length(toLight);
        float radius = localLightPosRadius[i].w;
        if (dist >= radius || radius <= 0.0) continue;
        float attenuation = 1.0 - dist / radius;
        attenuation *= attenuation;
        float wrappedDiffuse = 0.22 + 0.78 * max(dot(normal, toLight / max(dist, 0.001)), 0.0);
        sum += albedo * localLightColorIntensity[i].rgb *
               (localLightColorIntensity[i].w * attenuation * wrappedDiffuse);
    }
    return sum;
}

// LOD factor from screen-space UV derivatives
float computeLodFactor(vec2 uv) {
    vec2 dx = dFdx(uv);
    vec2 dy = dFdy(uv);
    float texelDensity = max(dot(dx, dx), dot(dy, dy));
    return smoothstep(0.0001, 0.005, texelDensity);
}

vec3 safeNormalize(vec3 v, vec3 fallback) {
    float len2 = dot(v, v);
    if (len2 > 1e-8) {
        return v * inversesqrt(len2);
    }
    return fallback;
}

vec3 fallbackTangent(vec3 n) {
    vec3 axis = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    return safeNormalize(cross(axis, n), vec3(1.0, 0.0, 0.0));
}

bool finiteVec3(vec3 v) {
    return all(equal(v, v)) && all(lessThan(abs(v), vec3(1e10)));
}

bool isMagentaKeyColor(vec4 color) {
    return color.r >= 0.58 && color.b >= 0.58 && color.g <= 0.48 &&
           color.r >= color.g + 0.22 && color.b >= color.g + 0.22 &&
           abs(color.r - color.b) <= 0.38;
}

// B7: previews use the same normalized UV sampling contract as scene
// materials. The previous integer texel/textureSize path forced repeat and
// nearest sampling, bypassing the texture's real address/filter/mip settings.
// Native sampling also avoids the preview-only image-query/fetch compiler path.
vec4 samplePreviewTexture(sampler2D tex, vec2 uv) {
    return texture(tex, uv);
}

// Parallax Occlusion Mapping with angle-adaptive sampling
vec2 parallaxOcclusionMap(vec2 uv, vec3 viewDirTS, float lodFactor) {
    float VdotN = abs(viewDirTS.z);

    if (VdotN < 0.15) return uv;

    float angleFactor = clamp(VdotN, 0.15, 1.0);
    int maxS = pomMaxSamples;
    int minS = max(maxS / 4, 4);
    int numSamples = int(mix(float(minS), float(maxS), angleFactor));
    numSamples = int(mix(float(minS), float(numSamples), 1.0 - lodFactor));

    float layerDepth = 1.0 / float(numSamples);
    float currentLayerDepth = 0.0;

    vec2 P = viewDirTS.xy / max(VdotN, 0.15) * pomScale;
    float maxOffset = pomScale * 3.0;
    P = clamp(P, vec2(-maxOffset), vec2(maxOffset));
    vec2 deltaUV = P / float(numSamples);

    vec2 currentUV = uv;
    float currentDepthMapValue = 1.0 - texture(uNormalHeightMap, currentUV).a;

    for (int i = 0; i < 64; i++) {
        if (i >= numSamples || currentLayerDepth >= currentDepthMapValue) break;
        currentUV -= deltaUV;
        currentDepthMapValue = 1.0 - texture(uNormalHeightMap, currentUV).a;
        currentLayerDepth += layerDepth;
    }

    vec2 prevUV = currentUV + deltaUV;
    float afterDepth = currentDepthMapValue - currentLayerDepth;
    float beforeDepth = (1.0 - texture(uNormalHeightMap, prevUV).a) - currentLayerDepth + layerDepth;
    float weight = afterDepth / (afterDepth - beforeDepth + 0.0001);
    vec2 result = mix(currentUV, prevUV, weight);

    float fadeFactor = smoothstep(0.15, 0.35, VdotN);
    return mix(uv, result, fadeFactor);
}

void main() {
    vec4 sourceUV = vec4(TexCoord, 0.0, 1.0);
    vec2 animatedUV = vec2(dot(uvRow0, sourceUV), dot(uvRow1, sourceUV));
    if (enablePOM == PREVIEW_SIMPLE_TEXTURE_MODE) {
        vec4 texColor = samplePreviewTexture(uTexture, animatedUV);
        if (isMagentaKeyColor(texColor)) {
            discard;
        }
        if (alphaTest != 0 && texColor.a < 0.5) {
            discard;
        }
        if (alphaTest != 0 && hairMaterial != 0) {
            texColor.a = 1.0;
        }
        if (colorKeyBlack != 0) {
            float lum = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
            float ck = smoothstep(0.12, 0.30, lum);
            texColor.a *= ck;
            if (texColor.a < 0.01) discard;
        }
        outColor = vec4(texColor.rgb, texColor.a * opacity);
        return;
    }

    float lodFactor = computeLodFactor(animatedUV);

    // Imported meshes already supply outward world-space normals. Raster
    // winding is not a second normal transform (mirrored bases can reverse it).
    vec3 vertexNormal = safeNormalize(Normal, vec3(0.0, 0.0, 1.0));

    vec2 finalUV = animatedUV;

    bool usePOM = enablePOM != 0 &&
                  alphaTest == 0 &&
                  colorKeyBlack == 0 &&
                  heightMapVariance > 0.001 &&
                  lodFactor < 0.99;
    bool useNormalMap = enableNormalMap != 0 &&
                        unlit == 0 &&
                        lodFactor < 0.99 &&
                        normalMapStrength > 0.001;
    mat3 TBN;
    if (usePOM || useNormalMap) {
        vec3 T = safeNormalize(Tangent, fallbackTangent(vertexNormal));
        T = safeNormalize(T - dot(T, vertexNormal) * vertexNormal, fallbackTangent(vertexNormal));
        vec3 B = safeNormalize(Bitangent, safeNormalize(cross(vertexNormal, T), vec3(0.0, 1.0, 0.0)));
        TBN = mat3(T, B, vertexNormal);
    }

    if (usePOM) {
        mat3 TBN_inv = transpose(TBN);
        vec3 viewDirWorld = normalize(viewPos.xyz - FragPos);
        vec3 viewDirTS = TBN_inv * viewDirWorld;
        finalUV = parallaxOcclusionMap(animatedUV, viewDirTS, lodFactor);
    }

    vec4 texColor = texture(uTexture, finalUV);
    // Repair dark DXT fringes on alpha-cut character textures such as hair.
    // Transparent edge texels can carry black/garbage RGB even when alpha is
    // valid; pull color from a coarser mip and trust the source more as alpha
    // approaches opaque. This matches the generic M2 path.
    if (alphaTest != 0 && texColor.a > 0.01 && texColor.a < 1.0) {
        vec3 mipColor = textureLod(uTexture, finalUV, 4.0).rgb;
        float trust = smoothstep(0.0, 0.9, texColor.a);
        texColor.rgb = mix(mipColor, texColor.rgb, trust);
    }

    // Some classic/TBC character textures use bright magenta as a color key.
    // Apply this before any material-specific alpha path because a few preview
    // batches report as opaque/blended even when their texture still carries
    // mask-color texels.
    if (texColor.r > 0.78 && texColor.g < 0.28 && texColor.b > 0.78) {
        discard;
    }

    if (alphaTest != 0 && hairMaterial != 0) {
        if (texColor.a < 0.5) {
            discard;
        }
        texColor.a = 1.0;
    } else if (alphaTest != 0) {
        // Screen-space sharpened alpha for alpha-to-coverage anti-aliasing.
        // Rescales alpha so the 0.5 cutoff maps to exactly the texel boundary,
        // giving smooth edges when MSAA + alpha-to-coverage is active.
        float aGrad = fwidth(texColor.a);
        texColor.a = clamp((texColor.a - 0.5) / max(aGrad, 0.001) * 0.5 + 0.5, 0.0, 1.0);
        if (texColor.a < 1.0 / 255.0) discard;
    }
    if (colorKeyBlack != 0) {
        float lum = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
        float ck = smoothstep(0.12, 0.30, lum);
        texColor.a *= ck;
        if (texColor.a < 0.01) discard;
    }

    // Compute normal (with normal mapping if enabled)
    vec3 norm = vertexNormal;
    if (useNormalMap) {
        vec3 mapNormal = texture(uNormalHeightMap, finalUV).rgb * 2.0 - 1.0;
        mapNormal.xy *= normalMapStrength;
        mapNormal = safeNormalize(mapNormal, vec3(0.0, 0.0, 1.0));
        vec3 worldNormal = safeNormalize(TBN * mapNormal, vertexNormal);
        float blendFactor = max(lodFactor, 1.0 - normalMapStrength);
        norm = safeNormalize(mix(worldNormal, vertexNormal, blendFactor), vertexNormal);
    }

    vec3 result;

    if (unlit != 0) {
        vec3 emissiveTint = vec3(emissiveTintR, emissiveTintG, emissiveTintB);
        vec3 warm = emissiveTint * emissiveBoost;
        result = texColor.rgb * (1.0 + warm);
    } else {
        vec3 ldir = normalize(-lightDir.xyz);
        float diff = max(dot(norm, ldir), 0.0);

        vec3 viewDir = normalize(viewPos.xyz - FragPos);
        vec3 halfDir = normalize(ldir + viewDir);
        float spec = pow(max(dot(norm, halfDir), 0.0), 32.0) * specularIntensity;

        float shadow = 1.0;
        if (shadowParams.x > 0.5) {
            float normalOffset = shadowTexelSize() * 2.0 * (1.0 - abs(dot(norm, ldir)));
            vec3 biasedPos = FragPos + norm * normalOffset;
            vec4 lsPos = lightSpaceMatrix * vec4(biasedPos, 1.0);
            vec3 proj = lsPos.xyz / lsPos.w;
            proj.xy = proj.xy * 0.5 + 0.5;
            if (proj.x >= 0.0 && proj.x <= 1.0 &&
                proj.y >= 0.0 && proj.y <= 1.0 &&
                proj.z >= 0.0 && proj.z <= 1.0) {
                float bias = max(0.0005 * (1.0 - abs(dot(norm, ldir))), 0.00005);
                shadow = sampleShadowPCF(uShadowMap, vec3(proj.xy, proj.z - bias), shadowTexelSize());
            }
            shadow = mix(1.0, shadow, shadowParams.y);
        }

        result = ambientColor.rgb * texColor.rgb
               + shadow * (diff * lightColor.rgb * texColor.rgb + spec * lightColor.rgb);
    }

    if (unlit == 0) result += localLightContribution(FragPos, norm, texColor.rgb);

    float dist = length(viewPos.xyz - FragPos);
    float fogFactor = clamp((fogParams.y - dist) / (fogParams.y - fogParams.x), 0.0, 1.0);
    result = mix(fogColor.rgb, result, fogFactor);
    if (!finiteVec3(result)) {
        result = texColor.rgb;
    }

    outColor = vec4(result, texColor.a * opacity);
}
