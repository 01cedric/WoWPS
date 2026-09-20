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

layout(set = 1, binding = 2) uniform M2Material {
    int hasTexture;
    int alphaTest;
    int colorKeyBlack;
    float colorKeyThreshold;
    int unlit;
    int blendMode;
    float fadeAlpha;
    float interiorDarken;
    float specularIntensity;
    float emissiveBoost;
    float tintR;
    float tintG;
    float tintB;
};

layout(set = 0, binding = 1) uniform sampler2DShadow uShadowMap;

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;
layout(location = 3) flat in vec3 InstanceOrigin;
layout(location = 4) in float ModelHeight;
layout(location = 5) in float vFadeAlpha;
layout(location = 6) flat in int vSkyMode;

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

// Directional highlights require both the light and eye above the surface.
// Guard the half vector when the eye faces directly away from the light.
float directionalSpecular(vec3 normal, vec3 light, vec3 eye, float intensity) {
    if (dot(normal, light) <= 0.0 || dot(normal, eye) <= 0.0) return 0.0;
    vec3 halfVector = light + eye;
    float halfLength2 = dot(halfVector, halfVector);
    if (halfLength2 <= 1e-8) return 0.0;
    vec3 halfDir = halfVector * inversesqrt(halfLength2);
    return pow(max(dot(normal, halfDir), 0.0), 32.0) * intensity;
}

// Transmission is supplied by the same directional source as diffuse light.
// Authored leaf color filters it; blocked sunlight cannot create leaf emission.
vec3 foliageTransmission(float nDotL, float viewDotLight, float coverage,
                         vec3 albedo, vec3 keyColor, float shadow) {
    float backLit = max(-nDotL, 0.0);
    float amount = backLit * pow(max(viewDotLight, 0.0), 4.0) * 0.35 * coverage;
    return amount * albedo * keyColor * shadow;
}

// 4x4 Bayer dither matrix (normalized to 0..1)
float bayerDither4x4(ivec2 p) {
    int idx = (p.x & 3) + (p.y & 3) * 4;
    float m[16] = float[16](
         0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
        12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
         3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
        15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
    );
    return m[idx];
}

void main() {
    // Evaluate derivatives before material discard/divergent lighting paths.
    vec3 geometricNormal = cross(dFdx(FragPos), dFdy(FragPos));
    float geometricLength2 = dot(geometricNormal, geometricNormal);
    vec3 shadowNormal = geometricLength2 > 1e-12
        ? geometricNormal * inversesqrt(geometricLength2) : normalize(Normal);
    if (dot(shadowNormal, Normal) < 0.0) shadowNormal = -shadowNormal;
    float inspectedShadow = 1.0; // Exact attenuation already evaluated by normal lighting.
    vec4 texColor = hasTexture != 0 ? texture(uTexture, TexCoord) : vec4(1.0);
    // The batch's authored colour. A glow card is painted white and coloured
    // here - Orgrimmar's bonfire carries (1.0, 0.329, 0.0) - so without it
    // every fire in the world burns white.
    texColor.rgb *= vec3(tintR, tintG, tintB);

    // Original client sky M2s carry their authored colour and alpha, and are
    // taken as they are. They are camera-centered and unlit, and must not be
    // swallowed by world-distance fog.
    //
    // This used to sit below the three discards, which meant the sky was not
    // taken as it is. The alpha test in particular rescales alpha by its own
    // screen-space derivative:
    //
    //     float aGrad = fwidth(texColor.a);
    //     texColor.a = clamp((texColor.a - alphaCutoff) / max(aGrad, 0.001) ...
    //
    // fwidth is how fast alpha changes from one pixel to the next, so it
    // changes whenever the view does - and a nebula's alpha ramp is gentle,
    // which makes the divisor tiny and the result a hard edge. Turning the
    // camera moved that edge, so Hellfire's sky flickered while the view moved
    // and stood still when it did not, on its blended layers alone. The rescale
    // is for foliage cutouts, where a hard edge is the point.
    if (vSkyMode != 0) {
        outColor = vec4(shadowParams.w > 0.5 ? vec3(1.0, 0.0, 1.0) : texColor.rgb, texColor.a * vFadeAlpha);
        return;
    }

    // Flag 0x8 selects binary coverage for the single-sample cutout pipeline.
    // Keep the authored mode in the low bits; sky returned above untouched.
    int alphaMode = alphaTest & 7;
    bool hardCutout = (alphaTest & 8) != 0;
    float alphaCutoff = alphaMode == 3 ? 0.25 : 0.4;
    // Test source alpha BEFORE any derivative/LOD-dependent remapping. With
    // one depth sample, a tiny positive coverage still writes a solid card;
    // fwidth sharpening can even turn source alpha zero into positive alpha.
    // Inverted comparison also rejects non-finite alpha on this binary path.
    if (hardCutout && alphaMode != 0 && !(texColor.a >= alphaCutoff)) discard;
    bool isFoliage = (alphaMode == 2);

    // Fix DXT fringe: transparent edge texels have garbage (black) RGB.
    // At low alpha the original RGB is untrustworthy - replace with the
    // averaged color from nearby opaque texels (high mip).  The lower
    // the alpha the more we distrust the original color.
    if (alphaMode != 0 && texColor.a > 0.01 && texColor.a < 1.0) {
        vec3 mipColor = textureLod(uTexture, TexCoord, 4.0).rgb * vec3(tintR, tintG, tintB);
        // trust = 0 at alpha 0, trust = 1 at alpha ~0.9
        float trust = smoothstep(0.0, 0.9, texColor.a);
        texColor.rgb = mix(mipColor, texColor.rgb, trust);
    }

    // Mip-alpha preservation: alpha mips average downward, thinning distant
    // canopies to skeletons. Boost alpha with mip level so perceived leaf
    // density stays constant with distance.
    if (!hardCutout && isFoliage && hasTexture != 0) {
        float mip = textureQueryLod(uTexture, TexCoord).x;
        texColor.a *= 1.0 + clamp(mip, 0.0, 4.0) * 0.18;
    }
    if (alphaMode != 0) {
        // Screen-space sharpened alpha: rescale so the cutoff maps to the
        // texel boundary. With MSAA + alpha-to-coverage on the cutout
        // pipeline this dithers the edge band across samples, smoothing
        // leaf silhouettes instead of the old hard binary discard.
        if (hardCutout) {
            texColor.a = 1.0;
        } else {
            float aGrad = fwidth(texColor.a);
            texColor.a = clamp((texColor.a - alphaCutoff) / max(aGrad, 0.001) * 0.5 + 0.5, 0.0, 1.0);
            if (texColor.a < 1.0 / 255.0) discard;
        }
    }
    if (colorKeyBlack != 0) {
        float lum = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
        if (lum < colorKeyThreshold) discard;
    }
    if (blendMode == 1 && texColor.a < 0.004) discard;

    // Per-instance color variation (foliage only)
    if (isFoliage) {
        float hash = fract(sin(dot(InstanceOrigin.xy, vec2(127.1, 311.7))) * 43758.5453);
        float hueShiftR = 1.0 + (hash - 0.5) * 0.16;       // ±8% red
        float hueShiftB = 1.0 + (fract(hash * 7.13) - 0.5) * 0.16; // ±8% blue
        float brightness = 0.85 + hash * 0.30;               // 85–115%
        texColor.rgb *= vec3(hueShiftR, 1.0, hueShiftB) * brightness;
    }

    vec3 norm = normalize(Normal);
    bool foliageTwoSided = (alphaMode == 2);

    // Detail normal perturbation (foliage only) - UV-based only so wind doesn't cause flicker
    if (isFoliage) {
        float nx = sin(TexCoord.x * 12.0 + TexCoord.y * 5.3) * 0.10;
        float ny = sin(TexCoord.y * 14.0 + TexCoord.x * 4.7) * 0.10;
        norm = normalize(norm + vec3(nx, ny, 0.0));
    }

    vec3 ldir = normalize(-lightDir.xyz);
        float nDotL = dot(norm, ldir);
        float diff = foliageTwoSided ? abs(nDotL) : max(nDotL, 0.0);

    vec3 result;
    if (unlit != 0) {
        result = texColor.rgb * emissiveBoost;
        if (emissiveBoost > 1.0) {
            // Weighted by the texel's own brightness. Added flat it lit the
            // whole quad, and a glow card is black everywhere but its middle,
            // so the card's rectangle appeared as an orange panel hanging on
            // whatever was behind the fire. Black has nothing to boost.
            float emissiveWeight =
                dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
            result += vec3(0.32, 0.14, 0.025) * (emissiveBoost - 1.0) *
                      emissiveWeight;
        }
    } else {
        vec3 viewDir = normalize(viewPos.xyz - FragPos);

        float spec = 0.0;
        float shadow = 1.0;
        if (!isFoliage) {
            spec = directionalSpecular(norm, ldir, viewDir, specularIntensity);
        }

        if (shadowParams.x > 0.5) {
            shadow = outdoorShadow(FragPos, shadowNormal, ldir);
        }
        inspectedShadow = shadow;

        // Leaf subsurface scattering (foliage only) - uses stable normal, no FragPos dependency
        vec3 sss = vec3(0.0);
        if (isFoliage) {
            sss = foliageTransmission(nDotL, dot(viewDir, -ldir), texColor.a,
                                      texColor.rgb, lightColor.rgb, shadow);
        }

        // Sky-bounce ambient for foliage: upward-facing leaves catch more
        // ambient than the canopy underside, giving the crown depth instead
        // of a uniformly-lit blob.
        vec3 ambientTerm = ambientColor.rgb;
        if (isFoliage) {
            ambientTerm *= 0.82 + 0.30 * clamp(norm.z, 0.0, 1.0);
        }
        result = ambientTerm * texColor.rgb
               + shadow * (diff * lightColor.rgb * texColor.rgb + spec * lightColor.rgb)
               + sss;

        if (interiorDarken > 0.0) {
            result *= mix(1.0, 0.5, interiorDarken);
        }
    }

    // Canopy ambient occlusion (foliage only)
    if (isFoliage) {
        float normalizedHeight = clamp(ModelHeight / 18.0, 0.0, 1.0);
        float aoFactor = mix(0.55, 1.0, smoothstep(0.0, 0.6, normalizedHeight));
        result *= aoFactor;
    }

    if (unlit == 0) result += localLightContribution(FragPos, norm, texColor.rgb);

    float dist = length(viewPos.xyz - FragPos);
    float fogFactor = clamp((fogParams.y - dist) / (fogParams.y - fogParams.x), 0.0, 1.0);
    if (blendMode >= 3) {
        // Additive. Mixing toward the fog colour would give the card's black
        // corners the fog's colour, and additive then adds that to the scene -
        // the whole quad shows up as a lit rectangle hanging in the air, which
        // is what Orgrimmar's bonfire glow was doing to the wall behind it.
        // Distance can only take an additive contribution away.
        result *= fogFactor;
    } else {
        result = mix(fogColor.rgb, result, fogFactor);
    }

    float outAlpha = texColor.a * vFadeAlpha;
    // Cutout materials output the sharpened coverage alpha computed above -
    // alpha-to-coverage turns it into per-sample coverage for smooth edges.
    // Color-key-only materials have no meaningful texture alpha; keep them
    // opaque after the discard.
    if (colorKeyBlack != 0 && alphaMode == 0) {
        outAlpha = vFadeAlpha;
    }
    if (shadowParams.w > 0.5) result = (shadowParams.x > 0.5 && unlit == 0)
        ? vec3(inspectedShadow) : vec3(1.0, 0.0, 1.0);
    outColor = vec4(result, outAlpha);
}
