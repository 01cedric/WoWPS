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
};

layout(push_constant) uniform Push {
    mat4 model;
    float waveAmp;
    float waveFreq;
    float waveSpeed;
    float liquidBasicType;
    vec2 screenSize;  // size of the target being drawn into
    vec2 depthRange;  // the camera's own near and far, for linearising SceneDepth
    vec2 captureValid; // x: refraction capture; y: planar reflection rendered
} push;

layout(set = 1, binding = 0) uniform WaterMaterial {
    vec4 waterColor;
    float waterAlpha;
    float shimmerStrength;
    float alphaScale;
};

layout(set = 2, binding = 0) uniform sampler2D SceneColor;
layout(set = 2, binding = 1) uniform sampler2D SceneDepth;
layout(set = 2, binding = 2) uniform sampler2D ReflectionColor;
// Wake points are laid down behind anything wading through the shallows. Each
// is a churn site that spreads and dies, so a running character leaves a trail
// of froth rather than a disc stuck under its feet.
const int MAX_WAKE_POINTS = 32;

layout(set = 2, binding = 3) uniform WaterFrameData {
    mat4 reflViewProj;
    vec4 wakeBounds;                  // xy = centre, z = cull radius, w = count
    vec4 wakePoints[MAX_WAKE_POINTS]; // xy = world pos, z = age 0..1, w = strength
};

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;
layout(location = 3) in float WaveOffset;
layout(location = 4) in vec2 ScreenUV;

layout(location = 0) out vec4 outColor;

// ============================================================
// Dual-scroll detail normals (multi-octave ripple overlay)
// ============================================================
float noiseValue(vec2 p);  // defined below; used to bend the wave fronts

vec3 dualScrollWaveNormal(vec2 p, float time) {
    // Three wave octaves at different angles, frequencies, and speeds.
    // Directions are non-axis-aligned to prevent visible tiling patterns.
    // Frequency increases and amplitude decreases per octave (standard
    // multi-octave noise layering for natural water appearance).
    vec2 d1 = normalize(vec2(0.86, 0.51));   // ~30° from +X
    vec2 d2 = normalize(vec2(-0.47, 0.88));  // ~118° (opposing cross-wave)
    vec2 d3 = normalize(vec2(0.32, -0.95));  // ~-71° (third axis for variety)
    float f1 = 0.19, f2 = 0.43, f3 = 0.72;  // spatial frequency (higher = tighter ripples)
    float s1 = 0.95, s2 = 1.73, s3 = 2.40;  // scroll speed (higher octaves move faster)
    float a1 = 0.22, a2 = 0.10, a3 = 0.05;  // amplitude (decreasing for natural falloff)

    vec2 p1 = p + d1 * (time * s1 * 4.0);
    vec2 p2 = p + d2 * (time * s2 * 4.0);
    vec2 p3 = p + d3 * (time * s3 * 4.0);

    // Bend the wave fronts. Each octave on its own is a pure cosine, which is a
    // set of infinitely long straight ridges - in perspective those read as
    // bright parallel lines running to the horizon, and the specular highlight
    // rides along each crest. Perturbing the phase with low-frequency noise
    // makes the crests wander, at the same wavelength, amplitude and speed. The
    // noise is much coarser than the waves, so it curves them rather than
    // roughening them.
    float warp = noiseValue(p * 0.018 + vec2(time * 0.015)) * 6.28318;
    float c1 = cos(dot(p1, d1) * f1 + warp);
    float c2 = cos(dot(p2, d2) * f2 + warp * 1.7);
    float c3 = cos(dot(p3, d3) * f3 + warp * 2.6);

    float dHx = c1 * d1.x * f1 * a1 + c2 * d2.x * f2 * a2 + c3 * d3.x * f3 * a3;
    float dHy = c1 * d1.y * f1 * a1 + c2 * d2.y * f2 * a2 + c3 * d3.y * f3 * a3;

    return normalize(vec3(-dHx, -dHy, 1.0));
}

// ============================================================
// GGX/Cook-Torrance BRDF
// ============================================================
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * denom * denom + 1e-7);
}

float GeometrySmith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float ggx1 = NdotV / (NdotV * (1.0 - k) + k);
    float ggx2 = NdotL / (NdotL * (1.0 - k) + k);
    return ggx1 * ggx2;
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ============================================================
// Linearize depth
// ============================================================
float linearizeDepth(float d, float near, float far) {
    return near * far / (far - d * (far - near));
}

// Optical helpers shared with the host regression fixture by extraction.
float waterFresnel(float cosine) {
    return 0.02 + 0.98 * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}
float waterRayDistance(float behindDepth, float surfaceDepth, float axialCos) {
    return clamp((behindDepth - surfaceDepth) / max(axialCos, 0.05), 0.0, 120.0);
}
vec3 waterTransmission(float distanceInWater, float basicType) {
    vec3 sigma = basicType > 0.5 ? vec3(0.22, 0.065, 0.035) : vec3(0.30, 0.10, 0.055);
    return exp(-sigma * distanceInWater);
}
bool waterRefractionForeground(float refractDepth, float surfaceDepth) {
    return refractDepth < surfaceDepth + 0.02;
}

// ============================================================
// Noise functions for foam
// ============================================================
float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float hash22x(vec2 p) {
    return fract(sin(dot(p, vec2(269.5, 183.3))) * 43758.5453);
}

float noiseValue(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbmNoise(vec2 p, float time) {
    float v = 0.0;
    v += noiseValue(p * 3.0 + time * 0.3) * 0.5;
    v += noiseValue(p * 6.0 - time * 0.5) * 0.25;
    v += noiseValue(p * 12.0 + time * 0.7) * 0.125;
    return v;
}

// ============================================================
// Gradient (Perlin) noise + rotated fractal - for the magma/slime flow
// ============================================================
// Value noise (noiseValue above) samples a scalar per lattice point and
// interpolates it, so its features sit square on the integer grid and read as
// axis-aligned blocks up close - which is what made the slime motion look like
// tiles. Gradient noise instead interpolates a random gradient that is zero at
// each lattice point, so neighbouring cells blend through zero without the flat
// plateaus, and rotating every octave keeps the separate lattices from ever
// lining up. The result is a fractal flow rather than a grid of squares.
vec2 gradHash(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

float gradNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = dot(gradHash(i + vec2(0.0, 0.0)), f - vec2(0.0, 0.0));
    float b = dot(gradHash(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0));
    float c = dot(gradHash(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0));
    float d = dot(gradHash(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 0.5 + 0.5;
}

// Same octave structure and value range as fbmNoise, but on gradient noise and
// with a small rotation between octaves so no two lattices align.
float fbmGrad(vec2 p, float time) {
    const mat2 R = mat2(0.80, -0.60, 0.60, 0.80);   // ~37° per octave
    float v = 0.0;
    v += gradNoise(p * 3.0 + time * 0.3) * 0.5;   p = R * p;
    v += gradNoise(p * 6.0 - time * 0.5) * 0.25;  p = R * p;
    v += gradNoise(p * 12.0 + time * 0.7) * 0.125;
    return v;
}

// Voronoi-like cellular noise for foam particles
// jitter parameter controls how much cell points deviate from grid centers
// (0.0 = regular grid, 1.0 = fully random within cell)
float cellularFoam(vec2 p, float jitter) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float minDist = 1.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 neighbor = vec2(float(x), float(y));
            vec2 cellId = i + neighbor;
            // Jittered cell point - higher jitter = more irregular placement
            vec2 point = vec2(hash21(cellId), hash22x(cellId)) * jitter
                       + vec2(0.5) * (1.0 - jitter);
            float d = length(neighbor + point - f);
            minDist = min(minDist, d);
        }
    }
    return minDist;
}
float cellularFoam(vec2 p) { return cellularFoam(p, 1.0); }

void main() {
    float time = fogParams.z;
    float basicType = push.liquidBasicType;

    // ============================================================
    // Magma / Slime - self-luminous flowing surfaces, skip water path
    // ============================================================
    if (basicType > 1.5) {
        float dist = length(viewPos.xyz - FragPos);
        vec2 flowUV = FragPos.xy;

        bool isMagma = basicType < 2.5;

        // Multi-octave flowing noise for organic lava/slime look. Gradient-noise
        // fractal (fbmGrad/gradNoise) rather than value noise (fbmNoise/
        // noiseValue): same scales, weights and scroll, but the flow reads as a
        // fractal instead of the axis-aligned squares the grid basis produced.
        float n1 = fbmGrad(flowUV * 0.06 + vec2(time * 0.02, time * 0.03), time * 0.4);
        float n2 = fbmGrad(flowUV * 0.10 + vec2(-time * 0.015, time * 0.025), time * 0.3);
        float n3 = gradNoise(flowUV * 0.25 + vec2(time * 0.04, -time * 0.02));
        float flow = n1 * 0.45 + n2 * 0.35 + n3 * 0.20;

        // Dark crust vs bright molten core
        vec3 crustColor, hotColor, coreColor;
        if (isMagma) {
            crustColor = vec3(0.15, 0.04, 0.01);   // dark cooled rock
            hotColor   = vec3(1.0, 0.45, 0.05);     // orange molten
            coreColor  = vec3(1.0, 0.85, 0.3);      // bright yellow-white core
        } else {
            crustColor = vec3(0.05, 0.15, 0.02);
            hotColor   = vec3(0.3, 0.8, 0.15);
            coreColor  = vec3(0.5, 1.0, 0.3);
        }

        // Three-tier color: crust → molten → hot core
        float crustMask = smoothstep(0.25, 0.50, flow);
        float coreMask  = smoothstep(0.60, 0.80, flow);
        vec3 color = mix(crustColor, hotColor, crustMask);
        color = mix(color, coreColor, coreMask);

        // Subtle pulsing emissive glow
        float pulse = 1.0 + 0.15 * sin(time * 1.5 + flow * 6.0);
        color *= pulse;

        // Emissive brightening for hot areas
        color *= 1.0 + coreMask * 0.6;

        float fogFactor = clamp((fogParams.y - dist) / (fogParams.y - fogParams.x), 0.0, 1.0);
        color = mix(fogColor.rgb, color, fogFactor);
        outColor = vec4(color, 0.97);
        return;
    }

    // Derived from the render target, NOT from textureSize(SceneColor): the
    // refraction copy is kept at half resolution, so its size would put every
    // screen-space lookup at twice the intended UV.
    vec2 screenUV = gl_FragCoord.xy / max(push.screenSize, vec2(1.0));

    float dist = length(viewPos.xyz - FragPos);

    // --- Normal computation ---
    vec3 meshNorm = normalize(Normal);
    vec3 detailNorm = dualScrollWaveNormal(FragPos.xy, time);
    // Wave detail is a scrolling pattern with a finite period. Held at full
    // strength to the horizon it does two things the eye picks up immediately:
    // the tiling repeats visibly, and the highlights land smaller than a pixel
    // and alias into shimmer. Fade the detail back toward the flat surface
    // normal with distance so what is left far away is a smooth sheet for the
    // fog to take, which is also where the specular sparkle goes quiet.
    float detailFade = 1.0 - smoothstep(250.0, 1400.0, dist);
    vec3 norm = normalize(mix(meshNorm, detailNorm, 0.55 * detailFade));

    // Player interaction ripple normal perturbation
    vec2 rippleOrigin = playerPos.xy;
    float rippleStrength = fogParams.w;
    float d = length(FragPos.xy - rippleOrigin);
    float rippleEnv = rippleStrength * exp(-d * 0.12);
    if (rippleEnv > 0.001) {
        vec2 radialDir = (FragPos.xy - rippleOrigin) / max(d, 0.01);
        float dHdr = rippleEnv * 0.12 * (-0.12 * sin(d * 2.5 - time * 6.0) + 2.5 * cos(d * 2.5 - time * 6.0));
        norm = normalize(norm + vec3(-radialDir * dHdr, 0.0));
    }

    vec3 viewDir = normalize(viewPos.xyz - FragPos);
    vec3 ldir = normalize(-lightDir.xyz);
    float NdotV = max(dot(norm, viewDir), 0.001);
    float NdotL = max(dot(norm, ldir), 0.0);

    // --- Schlick Fresnel ---
    const vec3 F0 = vec3(0.02);
    float fresnel = waterFresnel(NdotV);

    // ============================================================
    // Refraction (screen-space from scene history)
    // ============================================================
    float near = push.depthRange.x;
    float far = push.depthRange.y;
    float waterLinDepth = linearizeDepth(gl_FragCoord.z, near, far);
    bool hasSceneData = push.captureValid.x > 0.5;
    float shoreLinDepth = hasSceneData
        ? linearizeDepth(texture(SceneDepth, screenUV).r, near, far)
        : waterLinDepth + 20.0;
    // Linear depth is view-axis distance, not distance along the pixel ray.
    float axialCos = max(abs((view * vec4(-viewDir, 0.0)).z), 0.05);
    float shorePath = max(shoreLinDepth - waterLinDepth, 0.0) / axialCos;
    float shoreDepth = shorePath * abs(viewDir.z);

    // Ramp distortion to zero at shore contact. Foreground silhouettes must
    // never be refracted into the water (characters, bridge decks, dry banks).
    vec2 halfTexel = 0.5 / max(push.screenSize, vec2(1.0));
    vec2 refractUV = clamp(screenUV + norm.xy * 0.025 * smoothstep(0.0, 1.5, shorePath),
                          halfTexel, vec2(1.0) - halfTexel);
    float refractLinDepth = hasSceneData
        ? linearizeDepth(texture(SceneDepth, refractUV).r, near, far)
        : shoreLinDepth;
    if (waterRefractionForeground(refractLinDepth, waterLinDepth)) {
        refractUV = screenUV;
        refractLinDepth = shoreLinDepth;
    }
    float opticalPath = waterRayDistance(refractLinDepth, waterLinDepth, axialCos);
    vec3 sceneRefract = hasSceneData ? texture(SceneColor, refractUV).rgb : vec3(0.0);

    // Beer-Lambert extinction is along the ray THROUGH water. Vertical depth
    // would incorrectly make grazing water as transparent as a top-down view.
    vec3 transmittance = waterTransmission(opticalPath, basicType);
    vec3 waterBody = clamp(waterColor.rgb * vec3(0.38, 0.65, 0.82), vec3(0.005), vec3(0.65));
    vec3 litBase = waterBody * (ambientColor.rgb + lightColor.rgb * NdotL * 0.45);
    vec3 refractedColor = hasSceneData
        ? sceneRefract * transmittance + litBase * (vec3(1.0) - transmittance)
        : litBase;
    float depthFade = 1.0 - exp(-opticalPath * 0.15);

    // ============================================================
    // Shoreline - wet sand and moving sediment
    // ============================================================
    // The last half metre of depth is where a beach reads as wet rather than
    // submerged: sand darkens and loses a little saturation, and light moving
    // on the surface throws slow banding across it. Both are keyed on depth, so
    // they exist only at the edge and leave open water untouched.
    // The surf front, as a depth contour that runs up the beach and back. Both
    // the wet sand and the foam key off this so they move together; the phase is
    // offset by low-frequency noise along the shore so one stretch breaks before
    // another rather than the whole shoreline pulsing as a ring.
    float swashPhase = sin(time * 0.55 + noiseValue(FragPos.xy * 0.12) * 6.28);
    float swashDepth = 0.30 + 0.18 * swashPhase;

    float wetBand = 1.0 - smoothstep(0.0, 0.70, shoreDepth);
    float shallowBand = 1.0 - smoothstep(0.0, 2.20, shoreDepth);
    // How thoroughly the surf currently covers this point. Sand deeper than the
    // front is under the run-up and soaked; sand shallower has just been
    // uncovered and is draining, so it lightens as the water pulls back. Keying
    // the darkening on depth alone left the wet zone fixed while the water
    // visibly moved over it.
    float coveredness = smoothstep(swashDepth - 0.12, swashDepth + 0.08, shoreDepth);
    if (basicType < 1.5 && shallowBand > 0.001) {
        float sediment = noiseValue(FragPos.xy * 1.6 + time * vec2(0.10, 0.06)) * 0.6
                       + noiseValue(FragPos.xy * 3.4 - time * vec2(0.07, 0.13)) * 0.4;
        float banding = smoothstep(0.42, 0.78, sediment);
        refractedColor *= 1.0 + banding * 0.18 * shallowBand;
        float soak = wetBand * (0.42 + 0.58 * coveredness);
        refractedColor = mix(refractedColor,
                             refractedColor * vec3(0.62, 0.60, 0.56),
                             soak * 0.80);
    }

    // ============================================================
    // Reflection: physical Fresnel with a zone-lit sky fallback. This uses
    // existing planar capture when available, never creates another pass.
    // A black reflection is valid at night; availability comes from the CPU.
    // ============================================================
    vec3 reflectedDir = reflect(-viewDir, norm);
    float skyHeight = clamp(reflectedDir.z, 0.0, 1.0);
    vec3 skyHorizon = max(fogColor.rgb, ambientColor.rgb * 0.45);
    vec3 skyZenith = ambientColor.rgb * vec3(0.55, 0.72, 1.0);
    vec3 envReflect = mix(skyHorizon, skyZenith, skyHeight);
    vec4 reflClip = reflViewProj * vec4(FragPos, 1.0);
    if (push.captureValid.y > 0.5 && reflClip.w > 0.1) {
        vec2 reflUV = reflClip.xy / reflClip.w * 0.5 + 0.5;
        reflUV.y = 1.0 - reflUV.y;
        reflUV += norm.xy * 0.015;
        float edgeFade = smoothstep(0.0, 0.12, reflUV.x) * (1.0 - smoothstep(0.88, 1.0, reflUV.x))
                       * smoothstep(0.0, 0.12, reflUV.y) * (1.0 - smoothstep(0.88, 1.0, reflUV.y));
        vec3 texReflect = texture(ReflectionColor, clamp(reflUV, vec2(0.002), vec2(0.998))).rgb;
        envReflect = mix(envReflect, texReflect, edgeFade);
    }

    // ============================================================
    // GGX Specular
    // ============================================================
    float roughness = mix(0.16, 0.30, 1.0 - detailFade);
    vec3 halfDir = normalize(ldir + viewDir);
    float D = DistributionGGX(norm, halfDir, roughness);
    float G = GeometrySmith(NdotV, NdotL, roughness);
    vec3 F = fresnelSchlickRoughness(max(dot(halfDir, viewDir), 0.0), F0, roughness);
    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.001) * lightColor.rgb * NdotL;
    specular = min(specular, vec3(2.0));

    // The animated normals already generate sun/moon highlights. No extra
    // uncorrelated sparkle noise: it aliases and costs three noise octaves.

    // ============================================================
    // Subsurface scattering
    // ============================================================
    float sssBase = pow(max(dot(viewDir, -ldir), 0.0), 4.0);
    float sss = sssBase * max(0.0, WaveOffset * 3.0) * 0.25;
    vec3 sssColor = vec3(0.05, 0.55, 0.35) * sss * lightColor.rgb;

    // ============================================================
    // Combine - reflection only where valid, no dark fallback
    // ============================================================
    // Reflection remains available at grazing angles even without a planar pass.
    float reflectWeight = clamp(fresnel, 0.0, 1.0);
    vec3 color = mix(refractedColor, envReflect, reflectWeight);
    color += specular + sssColor;

    float crest = smoothstep(0.5, 1.0, WaveOffset) * 0.04;
    color += vec3(crest);

    // Carried out of the shoreline block so the alpha below can make the foam
    // opaque. Tinting alone left it invisible: at the water's edge alpha sits
    // near its floor, so foam was being blended over the sand at 15%.
    float shorelineFoam = 0.0;

    // ============================================================
    // Shoreline foam - scattered particles, not smooth bands
    // Only on terrain water (waveAmp > 0); WMO water (canals, indoor)
    // has waveAmp == 0 and should not show shoreline interaction.
    // ============================================================
    if (hasSceneData && shoreDepth > 0.001 && shoreDepth < 0.9 && push.waveAmp > 0.0) {
        // Two advected gradient-noise taps replace five 3x3 cellular searches
        // (45 candidate cells/pixel). Broad broken foam survives 720p filtering.
        vec2 foamUV = FragPos.xy + vec2(0.25, -0.17) * time;
        float foamNoise = gradNoise(foamUV * 2.2) * 0.65
                        + gradNoise(foamUV.yx * 4.7 - time * 0.11) * 0.35;
        float swashBand = 1.0 - smoothstep(0.03, 0.16, abs(shoreDepth - swashDepth));
        shorelineFoam = swashBand * smoothstep(0.30, 0.62, foamNoise)
                      * (1.0 - smoothstep(0.45, 0.9, shoreDepth)) * 0.65;
        vec3 foamLight = clamp(ambientColor.rgb + lightColor.rgb * NdotL, vec3(0.0), vec3(1.0));
        color = mix(color, vec3(0.86, 0.91, 0.95) * foamLight, shorelineFoam);
    }

    // ============================================================
    // Wake froth - water churned up by something moving through it
    // Not gated on shoreDepth: wading happens wherever the water is shallow
    // enough to stand in, which is not only at the beach.
    // ============================================================
    float wakeFoam = 0.0;
    if (wakeBounds.w > 0.5) {
        // One cheap reject for the whole trail keeps this off every other water
        // pixel on screen - the trail covers a few yards, the sheet covers miles.
        vec2 toWake = FragPos.xy - wakeBounds.xy;
        if (dot(toWake, toWake) < wakeBounds.z * wakeBounds.z) {
            // Clamped, not trusted: an unmapped UBO would leave a garbage count
            // here and run this loop off the end of the array.
            int wakeCount = clamp(int(wakeBounds.w), 0, MAX_WAKE_POINTS);
            for (int i = 0; i < wakeCount; ++i) {
                vec4 wp = wakePoints[i];
                // Churn spreads as it dies, the way a disturbed patch does.
                float radius = mix(0.40, 1.45, wp.z);
                float d = length(FragPos.xy - wp.xy);
                // Falls off from the centre out, with no plateau. A flat middle
                // is what made these read as painted discs rather than froth.
                float disc = 1.0 - smoothstep(0.0, radius, d);
                wakeFoam += disc * disc * wp.w * (1.0 - wp.z);
            }
            wakeFoam = clamp(wakeFoam, 0.0, 1.0);

            if (wakeFoam > 0.002) {
                vec2 churnWarp = vec2(
                    noiseValue(FragPos.xy * 3.4 + time * 0.5) - 0.5,
                    noiseValue(FragPos.xy * 3.4 + vec2(23.0) - time * 0.44) - 0.5
                ) * 0.9;
                vec2 churnUV = FragPos.xy + churnWarp;

                // cellularFoam returns distance to the nearest cell point, so a
                // low threshold marks only the few pixels sitting on a point -
                // specks, which left the patch between them to be filled solid.
                // Thresholding across the middle of the range inverts that: most
                // of the patch is foam and the gaps between cells are the
                // texture, which is what aerated water actually looks like.
                float cells = cellularFoam(churnUV * 12.0 + time * vec2(0.25, -0.18));
                float cover = 1.0 - smoothstep(0.08, 0.38, cells);

                float fine = cellularFoam(churnUV * 29.0 - time * vec2(0.14, 0.30));
                cover = max(cover * 0.85, (1.0 - smoothstep(0.05, 0.22, fine)) * 0.5);

                // Clumping, so the trail thins and thickens along its length.
                cover *= 0.55 + 0.45 * noiseValue(churnUV * 2.2 + time * 0.35);

                wakeFoam *= cover;
                // Aerated water is lighter than the surface around it but it is
                // still water. Near-opaque white read as paint lying on top.
                wakeFoam = min(wakeFoam, 0.60);
                color = mix(color, vec3(0.86, 0.91, 0.95) * clamp(ambientColor.rgb + lightColor.rgb * NdotL, vec3(0.0), vec3(1.0)), wakeFoam);
            }
        }
    }

    // ============================================================
    // Wave crest foam (ocean only) - particle-based
    // ============================================================
    if (basicType > 0.5 && basicType < 1.5 && push.waveAmp > 0.0) {
        float crestMask = smoothstep(0.5, 1.0, WaveOffset);
        vec2 crestWarp = vec2(
            noiseValue(FragPos.xy * 1.8 + time * 0.1) - 0.5,
            noiseValue(FragPos.xy * 1.8 + vec2(53.0) + time * 0.07) - 0.5
        ) * 2.0;
        float crestCells = cellularFoam((FragPos.xy + crestWarp) * 6.0 + time * vec2(0.12, 0.08));
        float crestFoam = (1.0 - smoothstep(0.0, 0.18, crestCells)) * crestMask;
        float crestNoise = noiseValue(FragPos.xy * 3.0 - time * 0.3);
        crestFoam *= smoothstep(0.3, 0.6, crestNoise);
        color = mix(color, vec3(0.68, 0.78, 0.88), crestFoam * 0.30);
    }

    // ============================================================
    // Alpha and fog
    // ============================================================
    float baseAlpha = mix(waterAlpha, min(1.0, waterAlpha * 1.5), depthFade);
    float alpha = mix(baseAlpha, min(1.0, baseAlpha * 1.3), fresnel) * alphaScale;
    alpha = hasSceneData ? 1.0 : clamp(alpha, 0.15, 0.92);
    // Wet sand is a band on the beach, not a film of water, so it needs enough
    // presence to darken what is under it; foam has to be close to opaque or it
    // does not read at all.
    alpha = max(alpha, wetBand * 0.42 * smoothstep(0.0, 0.05, shoreDepth));
    // Foam opacity, kept in step with the colour mix above. Both come down
    // together - a fifth, then a fifth again - because lowering one alone
    // leaves the foam as opaque as it was and merely paler.
    alpha = max(alpha, shorelineFoam * 0.58);
    alpha = max(alpha, wakeFoam * 0.55);
    // Dissolve the sheet before the water geometry runs out, so the ocean fades
    // into the horizon haze instead of ending on a hard line. This has to come
    // after the clamp - clamping afterwards restored the 0.15 floor and put the
    // edge straight back.
    alpha *= (1.0 - smoothstep(600.0, 2400.0, dist));

    float fogFactor = clamp((fogParams.y - dist) / (fogParams.y - fogParams.x), 0.0, 1.0);
    color = mix(fogColor.rgb, color, fogFactor);

    outColor = vec4(color, alpha);
}
