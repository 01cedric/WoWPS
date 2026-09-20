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

layout(set = 1, binding = 0) uniform sampler2D uTexture;

layout(set = 1, binding = 1) uniform WMOMaterial {
    int hasTexture;
    int alphaTest;
    int unlit;
    int isInterior;
    float specularIntensity;
    int isWindow;
    int enableNormalMap;
    int enablePOM;
    float pomScale;
    int pomMaxSamples;
    float heightMapVariance;
    float normalMapStrength;
    int isLava;
    float wmoAmbientR;
    float wmoAmbientG;
    float wmoAmbientB;
    int emissive;
    int unfogged;
    int padding1;
    int padding2;
};

layout(set = 1, binding = 2) uniform sampler2D uNormalHeightMap;

layout(set = 0, binding = 1) uniform sampler2DShadow uShadowMap;

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;
layout(location = 3) in vec4 VertColor;
layout(location = 4) in vec3 Tangent;
layout(location = 5) in vec3 Bitangent;

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
        vec3 lightVector = toLight / max(dist, 0.001);
        float attenuation = 1.0 - dist / radius;
        attenuation *= attenuation;
        float wrappedDiffuse = 0.22 + 0.78 * max(dot(normal, lightVector), 0.0);
        sum += albedo * localLightColorIntensity[i].rgb *
               (localLightColorIntensity[i].w * attenuation * wrappedDiffuse);
    }
    return sum;
}

// LOD factor from screen-space UV derivatives
float computeLodFactor() {
    vec2 dx = dFdx(TexCoord);
    vec2 dy = dFdy(TexCoord);
    float texelDensity = max(dot(dx, dx), dot(dy, dy));
    // Low density = close/head-on = full detail (0)
    // High density = far/steep = vertex normals only (1)
    return smoothstep(0.0001, 0.005, texelDensity);
}

// Parallax Occlusion Mapping with angle-adaptive sampling
vec2 parallaxOcclusionMap(vec2 uv, vec3 viewDirTS, float lodFactor) {
    float VdotN = abs(viewDirTS.z);  // 1=head-on, 0=grazing

    // Fade out POM at grazing angles to avoid distortion
    if (VdotN < 0.15) return uv;

    float angleFactor = clamp(VdotN, 0.15, 1.0);
    int maxS = pomMaxSamples;
    int minS = max(maxS / 4, 4);
    int numSamples = int(mix(float(minS), float(maxS), angleFactor));
    numSamples = int(mix(float(minS), float(numSamples), 1.0 - lodFactor));

    float layerDepth = 1.0 / float(numSamples);
    float currentLayerDepth = 0.0;

    // Direction to shift UV per layer - clamp denominator to prevent explosion at grazing angles
    vec2 P = viewDirTS.xy / max(VdotN, 0.15) * pomScale;
    // Hard-clamp total UV offset to prevent texture swimming
    float maxOffset = pomScale * 3.0;
    P = clamp(P, vec2(-maxOffset), vec2(maxOffset));
    vec2 deltaUV = P / float(numSamples);

    vec2 currentUV = uv;
    float currentDepthMapValue = 1.0 - texture(uNormalHeightMap, currentUV).a;

    // Ray march through layers
    for (int i = 0; i < 64; i++) {
        if (i >= numSamples || currentLayerDepth >= currentDepthMapValue) break;
        currentUV -= deltaUV;
        currentDepthMapValue = 1.0 - texture(uNormalHeightMap, currentUV).a;
        currentLayerDepth += layerDepth;
    }

    // Interpolate between last two layers for smooth result
    vec2 prevUV = currentUV + deltaUV;
    float afterDepth = currentDepthMapValue - currentLayerDepth;
    float beforeDepth = (1.0 - texture(uNormalHeightMap, prevUV).a) - currentLayerDepth + layerDepth;
    float weight = afterDepth / (afterDepth - beforeDepth + 0.0001);
    vec2 result = mix(currentUV, prevUV, weight);

    // Fade toward original UV at grazing angles for smooth transition
    float fadeFactor = smoothstep(0.15, 0.35, VdotN);
    return mix(uv, result, fadeFactor);
}

// Same front-face/half-vector contract as M2 and character highlights.
float directionalSpecular(vec3 normal, vec3 light, vec3 eye, float intensity, float exponent) {
    if (dot(normal, light) <= 0.0 || dot(normal, eye) <= 0.0) return 0.0;
    vec3 halfVector = light + eye;
    float halfLength2 = dot(halfVector, halfVector);
    if (halfLength2 <= 1e-8) return 0.0;
    vec3 halfDir = halfVector * inversesqrt(halfLength2);
    return pow(max(dot(normal, halfDir), 0.0), exponent) * intensity;
}

vec3 outdoorUnlitLighting(vec3 albedo, vec3 ambient) {
    return albedo * ambient;
}

vec3 bakedInteriorLighting(vec3 vertexLight, vec3 rootAmbient) {
    return max(vertexLight,max(rootAmbient,vec3(0.0)));
}

vec3 outdoorGlassReflection(vec3 worldAmbient, vec3 worldDirect) {
    return worldAmbient + worldDirect * 0.25;
}

void main() {
    // Evaluate derivatives before material discard/divergent lighting paths.
    vec3 geometricNormal = cross(dFdx(FragPos), dFdy(FragPos));
    float geometricLength2 = dot(geometricNormal, geometricNormal);
    vec3 shadowNormal = geometricLength2 > 1e-12
        ? geometricNormal * inversesqrt(geometricLength2) : normalize(Normal);
    if (dot(shadowNormal, Normal) < 0.0) shadowNormal = -shadowNormal;
    float lodFactor = computeLodFactor();

    vec3 vertexNormal = normalize(Normal);

    // Compute final UV (with POM if enabled)
    vec2 finalUV = TexCoord;

    // Lava/magma: scroll UVs for flowing effect
    if (isLava != 0) {
        float time = fogParams.z;
        // Scroll both axes - pools get horizontal flow, waterfalls get vertical flow
        // (UV orientation depends on mesh, so animate both)
        finalUV += vec2(time * 0.04, time * 0.06);
    }

    // Build TBN matrix
    vec3 T = normalize(Tangent);
    vec3 B = normalize(Bitangent);
    vec3 N = vertexNormal;
    mat3 TBN = mat3(T, B, N);

    if (enablePOM != 0 && heightMapVariance > 0.001 && lodFactor < 0.99) {
        mat3 TBN_inv = transpose(TBN);
        vec3 viewDirWorld = normalize(viewPos.xyz - FragPos);
        vec3 viewDirTS = TBN_inv * viewDirWorld;
        finalUV = parallaxOcclusionMap(TexCoord, viewDirTS, lodFactor);
    }

    vec4 texColor = hasTexture != 0 ? texture(uTexture, finalUV) : vec4(1.0);
    if (alphaTest != 0 && texColor.a < 0.5) discard;

    // Compute normal (with normal mapping if enabled)
    vec3 norm = vertexNormal;
    if (enableNormalMap != 0 && lodFactor < 0.99 && normalMapStrength > 0.001) {
        vec3 mapNormal = texture(uNormalHeightMap, finalUV).rgb * 2.0 - 1.0;
        mapNormal = normalize(mapNormal);
        vec3 worldNormal = normalize(TBN * mapNormal);
        // Linear blend: strength controls how much normal map detail shows,
        // LOD fades out at distance. Both multiply for smooth falloff.
        float blend = clamp(normalMapStrength, 0.0, 1.0) * (1.0 - lodFactor);
        norm = normalize(mix(vertexNormal, worldNormal, blend));
    }

    vec3 result;

    // Authored baked interior light and emissive lamp/lava do not depend on
    // the outdoor shadow map. The backlit clock still has a lit surface.
    float shadow = 1.0;
    bool needsOutdoorShadow = emissive == 2 || (emissive == 0 && isLava == 0 && isInterior == 0);
    if (shadowParams.x > 0.5 && needsOutdoorShadow) {
        vec3 ldir = normalize(-lightDir.xyz);
        shadow = outdoorShadow(FragPos, shadowNormal, ldir);
    }

    if (emissive == 1) {
        // Authored luminous glass must remain bright in direct sun and shadow.
        // A small warm bias keeps low-valued texels from reading as dark glass.
        vec3 glass = texColor.rgb * 2.0 + vec3(0.16, 0.07, 0.015);

        // Gentle guttering, weaker than the clock's open fire - these are steady
        // lamps, not flames in the wind.
        //
        // Every lamp in a building shares one batch, so a uniform phase would
        // pulse a whole street in lockstep. The phase is hashed from the lamp's
        // world position instead, quantised into cells a few units across: large
        // enough that one lamp's glass falls in a single cell, small enough that
        // neighbouring lamps land in different ones.
        vec3 cell = floor(FragPos * 0.2);
        float h = fract(sin(dot(cell, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
        float phase = h * 6.2831853;
        float t = fogParams.z;
        float flicker = 0.93
                      + 0.05 * sin(t * 1.3 + phase)
                      + 0.02 * sin(t * 2.9 + phase * 1.7);
        result = glass * flicker;
    } else if (emissive == 2) {
        // Firelit from behind (Darkshire's clock face): the surface is still lit
        // by the sun so it belongs to the building by day, with a warm glow
        // seeping through it as if a fire burned in the tower.
        vec3 ldir = normalize(-lightDir.xyz);
        float diff = max(dot(norm, ldir), 0.0);
        vec3 lit = texColor.rgb * (ambientColor.rgb + lightColor.rgb * diff * shadow);

        // Three detuned sines: a slow breathing sway, a quicker wobble, and a
        // faint fast jitter. Their periods share no common multiple over any
        // watchable span, so the flame never visibly loops.
        float t = fogParams.z;
        float flicker = 0.84
                      + 0.10 * sin(t * 1.3)
                      + 0.05 * sin(t * 2.9 + 1.7)
                      + 0.03 * sin(t * 6.7 + 0.6);

        // Firelight only competes with daylight once the sun is down, so fade the
        // glow up as the scene darkens. A small floor keeps it faintly visible in
        // daytime shade rather than switching on at dusk.
        float daylight = clamp(dot(ambientColor.rgb + lightColor.rgb,
                                   vec3(0.299, 0.587, 0.114)), 0.0, 1.0);
        float night = mix(1.0, 0.22, daylight);

        const vec3 kFireColor = vec3(1.0, 0.58, 0.22);
        result = lit + kFireColor * (0.30 * flicker * night);

        // Glass covering the dial: a tight sun highlight plus a Fresnel sheen
        // that picks up sky colour at grazing angles, which is what sells a pane
        // in front of the face rather than paint on stone. Both are additive and
        // unaffected by the fire, since they live on the outer surface.
        vec3 viewDir = normalize(viewPos.xyz - FragPos);
        float gloss = directionalSpecular(norm, ldir, viewDir, 1.0, 96.0);
        float fresnel = pow(1.0 - clamp(dot(norm, viewDir), 0.0, 1.0), 4.0);
        result += lightColor.rgb * (gloss * 0.55 * shadow)
                + ambientColor.rgb * (fresnel * 0.35);
    } else if (isLava != 0) {
        // Lava is self-luminous - bright emissive, no shadows
        result = texColor.rgb * 1.5;
    } else if (isInterior != 0) {
        // Only genuinely interior-lit groups with complete authored MOCV
        // enter this path. Exterior-lit groups and missing colors use the same
        // world light as terrain. Keep authored baked illumination, without
        // inventing a 0.35 ambient floor or replacing it with night exposure.
        vec3 mocv = bakedInteriorLighting(VertColor.rgb,vec3(wmoAmbientR,wmoAmbientG,wmoAmbientB));
        result = texColor.rgb * mocv;
    } else if (unlit != 0) {
        // MOMT unlit removes the directional term; it does not imply emission.
        // Keep regional ambient; raw MOCV additive normalization is deferred.
        result = outdoorUnlitLighting(texColor.rgb, ambientColor.rgb);
    } else {
        vec3 ldir = normalize(-lightDir.xyz);
        float diff = max(dot(norm, ldir), 0.0);

        vec3 viewDir = normalize(viewPos.xyz - FragPos);
        float spec = directionalSpecular(norm, ldir, viewDir, specularIntensity, 32.0);

        result = ambientColor.rgb * texColor.rgb
               + shadow * (diff * lightColor.rgb * texColor.rgb + spec * lightColor.rgb);

        // Compatibility fallback until raw MOCV receives its complete
        // batch normalization and additive lighting contract.
        result *= max(VertColor.rgb, vec3(0.5));
    }

    if (isWindow == 0 && isLava == 0)
        result += localLightContribution(FragPos, norm, texColor.rgb);

    float dist = length(viewPos.xyz - FragPos);
    float fogFactor = clamp((fogParams.y - dist) / (fogParams.y - fogParams.x), 0.0, 1.0);

    float alpha = texColor.a;

    // Window glass: opaque but simulates dark tinted glass with reflections.
    if (isWindow != 0) {
        vec3 viewDir = normalize(viewPos.xyz - FragPos);
        float NdotV = abs(dot(norm, viewDir));
        float fresnel = 0.08 + 0.92 * pow(1.0 - NdotV, 4.0);

        vec3 ldir = normalize(-lightDir.xyz);
        vec3 reflectDir = reflect(-viewDir, norm);
        float sunGlint = dot(norm, ldir) > 0.0 ?
            pow(max(dot(reflectDir, ldir), 0.0), 32.0) * shadow : 0.0;

        float baseBrightness = mix(0.3, 0.9, sunGlint);
        vec3 glass = result * baseBrightness;

        // Reflection must follow the outdoor key light too. A fixed blue
        // daylight term made non-emissive windows glow at midnight.
        vec3 reflectTint = outdoorGlassReflection(ambientColor.rgb,lightColor.rgb);
        glass = mix(glass, reflectTint, fresnel * 0.8);

        float spec = directionalSpecular(norm, ldir, viewDir, 1.0, 256.0) * shadow;
        glass += spec * lightColor.rgb * 0.8;

        float specBroad = directionalSpecular(norm, ldir, viewDir, 1.0, 12.0) * shadow;
        glass += specBroad * lightColor.rgb * 0.12;

        result = glass;
        if (isWindow == 2) {
            // Instance/dungeon glass: mostly transparent to see through
            alpha = mix(0.12, 0.35, fresnel);
        } else {
            alpha = mix(0.4, 0.95, NdotV);
        }
    }

    // Fog attenuates the complete material, including reflected key light.
    if (unfogged == 0) result = mix(fogColor.rgb, result, fogFactor);
    if (shadowParams.w > 0.5) result = (shadowParams.x > 0.5 && needsOutdoorShadow)
        ? vec3(shadow) : vec3(1.0, 0.0, 1.0);
    outColor = vec4(result, alpha);
}
