#version 450
// Integrate shadowed single scattering along a reconstructed world-space ray.
// The depth endpoint stops rays at opaque geometry. Shadow-frustum exits add
// no light: guessing 'unshadowed' there would illuminate buildings and caves.
layout(set=0,binding=0) uniform sampler2D sceneDepth;
layout(set=0,binding=1) uniform sampler2D shadowDepth;
layout(std140,set=0,binding=2) uniform Volume {
    mat4 inverseRelativeViewProjection;
    mat4 lightMatrix;
    vec4 camera;
    vec4 sunDirection;
    vec4 sunColor;
    vec4 parameters;
    mat4 nearLightMatrix;
    vec4 fogParameters; // extinction, base height, height falloff, maximum distance
    vec4 fogColor;
} v;
layout(location=0) in vec2 TexCoord;
layout(location=0) out vec4 outColor;

// Restrict work to the intersection of this eye ray and the actual shadow
// volume. Sky pixels still integrate real shadowed air; samples behind a wall
// or outside the shadow map never invent illumination.
bool clipAxis(float origin, float direction, float lo, float hi,
              inout float enter, inout float leave) {
    if (abs(direction) < 0.000001) return origin >= lo && origin <= hi;
    float a = (lo-origin)/direction;
    float b = (hi-origin)/direction;
    enter = max(enter, min(a,b));
    leave = min(leave, max(a,b));
    return leave > enter;
}

// Surface shaders use authoredLight * albedo * NdotL: the implicit Lambert
// convention absorbs 1/pi into authoredLight. Undo it for a normalized phase
// function. These authored colors are not calibrated physical irradiances.
float directionalScatteringScale(float mu, float scatteringAlbedo) {
    // Normalized aerosol + molecular mixture: lateral world-space shafts stay
    // visible when the light source is outside the view or above the camera.
    // Shadow visibility still determines where the actual shafts exist.
    const float g = 0.65;
    float cosine = clamp(mu,-1.0,1.0);
    float mie = (1.0-g*g) / (12.5663706*pow(1.0+g*g-2.0*g*cosine,1.5));
    float rayleigh = 3.0*(1.0+cosine*cosine)/50.2654824;
    float phase = 0.7*mie + 0.3*rayleigh;
    return 3.14159265 * clamp(scatteringAlbedo,0.0,1.0) * phase;
}

// Artistic exposure is separate from the normalized scattering phase.
// Calibrate the sourceward peak to 10x the original HG(g=.75) contribution.
// A nonzero lateral floor reveals shafts across objects without adding an
// unshadowed fog term or increasing extinction. Fully blocked air stays black.
float shaftExposure(float mu) {
    float forward = max(clamp(mu,-1.0,1.0),0.0);
    const float peakCalibration = 28.0/(0.7*1.65/(0.35*0.35)+0.3*1.5);
    return peakCalibration*(4.0+6.0*forward*forward);
}

// Stable, decorrelated pixel seed. The previous linear interleaved gradient
// noise forms diagonal iso-value bands which survive the small march budget
// and become visible at strong shaft exposure. Avalanche integer arithmetic
// removes that directional structure without a time seed or texture fetch.
float volumePixelOffset(vec2 uv) {
    uvec2 pixel = uvec2(uv * vec2(textureSize(sceneDepth,0)));
    uint seed = pixel.x * 0x9e3779b9u ^ pixel.y * 0x85ebca6bu;
    seed ^= seed >> 16u;
    seed *= 0x7feb352du;
    seed ^= seed >> 15u;
    seed *= 0x846ca68bu;
    seed ^= seed >> 16u;
    // Top 24 bits convert exactly to float and remain strictly below one.
    return float(seed >> 8u) * (1.0 / 16777216.0);
}

// Integrate a depth crossing only when BOTH stratum endpoints occupy the
// fetched shadow texel. Otherwise its blocker cannot represent the interval:
// retain the original sampled visibility (no invented light across leaves).
float stratumVisibility(float blocker, float sampledZ, float startZ, float endZ,
                        vec2 startUV, vec2 endUV, float opticalStep) {
    float a=startZ-0.0001;
    float b=endZ-0.0001;
    // These agree with sampled visibility even across texels, because the
    // sampled depth lies between the stratum endpoints. Most steps stop here.
    if (max(a,b)<=blocker) return 1.0;
    if (min(a,b)>blocker) return 0.0;
    if (min(startZ,endZ)<0.0 || max(startZ,endZ)>1.0 ||
        any(notEqual(floor(startUV),floor(endUV))))
        return sampledZ-0.0001<=blocker ? 1.0 : 0.0;
    float fraction=clamp((blocker-a)/(b-a),0.0,1.0);
    // Exact Beer--Lambert energy before/after the crossing. Avoid cancellation
    // for very thin intervals using the linear limit with first-order correction.
    float prefix = opticalStep<0.001 ?
        fraction*(1.0+0.5*opticalStep*(1.0-fraction)) :
        (1.0-exp(-opticalStep*fraction))/(1.0-exp(-opticalStep));
    return b>a ? prefix : 1.0-prefix;
}

// Shared verbatim with the composite fallback: both integrate the same world
// ray, clipping, shadow visibility, extinction and normalized phase.
vec3 integrateScatteringQuality(vec2 uv, float depth, int refinement) {
    if (dot(v.sunColor.rgb,v.sunColor.rgb) <= 0.0) return vec3(0.0);
    vec4 endpoint = v.inverseRelativeViewProjection * vec4(uv*2.0-1.0, depth, 1.0);
    // Perspective inverse homogeneous w is reciprocal eye-space depth.
    // This guide is independent of ray angle, unlike radial distance, so the
    // full-resolution pass can compare depths measured at different UVs.
    float eyeDepth = 1.0 / max(endpoint.w, 0.000001);
    vec3 delta = endpoint.xyz * eyeDepth;
    float surfaceDistance = length(delta);
    vec3 ray = delta / max(surfaceDistance, 0.0001);
    float distanceLimit = min(surfaceDistance, v.parameters.x);
    int count = clamp(int(v.parameters.z) * refinement,1,48);
    float enter = 0.0;
    float leave = distanceLimit;
    vec3 lightOrigin = (v.lightMatrix * vec4(v.camera.xyz,1.0)).xyz;
    vec3 lightRay = (v.lightMatrix * vec4(ray,0.0)).xyz;
    vec3 nearOrigin = (v.nearLightMatrix * vec4(v.camera.xyz,1.0)).xyz;
    vec3 nearRay = (v.nearLightMatrix * vec4(ray,0.0)).xyz;
    bool intersects = clipAxis(lightOrigin.x,lightRay.x,-1.0,1.0,enter,leave);
    intersects = clipAxis(lightOrigin.y,lightRay.y,-1.0,1.0,enter,leave) && intersects;
    intersects = clipAxis(lightOrigin.z,lightRay.z,0.0,1.0,enter,leave) && intersects;
    if (!intersects) {
        return vec3(0.0);
    }
    float stepSize = (leave-enter) / float(count);
    // Stable spatial dither only. Each stratum has its own fractional offset
    // to break correlated sampling layers; no temporal history or shimmer seed.
    float offset = volumePixelOffset(uv);
    float transmittance = exp(-v.parameters.y * enter);
    float stepTransmittance = exp(-v.parameters.y * stepSize);
    float integral = 0.0;
    vec2 atlasTexel = 1.0 / vec2(textureSize(shadowDepth,0));
    for (int i=0; i<48; ++i) {
        if (i >= count) break;
        // The directional shadow camera is affine (orthographic, w=1).
        // Reuse the transform already needed by clipping instead of repeating
        // a world-space reconstruction and mat4 multiply for every sample.
        float stratumOffset = fract(offset+float(i)*0.61803398875);
        float stratumStart = enter+float(i)*stepSize;
        float sampleDistance = stratumStart+stratumOffset*stepSize;
        vec3 nearShadow = nearOrigin + nearRay * sampleDistance;
        nearShadow.xy = nearShadow.xy*0.5+0.5;
        float nearWeight = 0.0;
        float nearVisible = 0.0;
        if (all(greaterThanEqual(nearShadow,vec3(0.0))) && all(lessThanEqual(nearShadow,vec3(1.0)))) {
            // Match the receiver cascade transition. The near tile preserves
            // tree/character/gate detail that the coarse far map cannot resolve.
            vec3 edge = min(nearShadow,vec3(1.0)-nearShadow);
            nearWeight = smoothstep(0.0,0.08,min(edge.x,edge.y)) * smoothstep(0.0,0.02,edge.z);
            vec2 nearUV = clamp(nearShadow.xy * vec2(0.5,1.0),
                                atlasTexel*0.5,vec2(0.5,1.0)-atlasTexel*0.5);
            if (nearWeight > 0.0) {
                float blocker = textureLod(shadowDepth,nearUV,0.0).r;
                vec3 startShadow = nearOrigin + nearRay*stratumStart;
                vec3 endShadow = startShadow + nearRay*stepSize;
                nearVisible = stratumVisibility(blocker,nearShadow.z,startShadow.z,endShadow.z,
                    (vec2(startShadow.x,startShadow.y)*0.5+0.5)*vec2(1.0/atlasTexel.y),
                    (vec2(endShadow.x,endShadow.y)*0.5+0.5)*vec2(1.0/atlasTexel.y),v.parameters.y*stepSize);
            }
        }
        float visible = nearVisible;
        if (nearWeight < 1.0) {
            vec3 shadow = lightOrigin + lightRay * sampleDistance;
            shadow.xy = shadow.xy*0.5+0.5;
            float farVisible = 0.0;
            if (all(greaterThanEqual(shadow,vec3(0.0))) && all(lessThanEqual(shadow,vec3(1.0)))) {
                // Far tile is (N,0)..(1.5N,0.5N) in the 2N x N atlas.
                vec2 atlasUV = clamp(vec2(0.5,0.0) + shadow.xy * vec2(0.25,0.5),
                                     vec2(0.5,0.0) + atlasTexel * 0.5,
                                     vec2(0.75,0.5) - atlasTexel * 0.5);
                float blocker = textureLod(shadowDepth, atlasUV, 0.0).r;
                vec3 startShadow = lightOrigin + lightRay*stratumStart;
                vec3 endShadow = startShadow + lightRay*stepSize;
                farVisible = stratumVisibility(blocker,shadow.z,startShadow.z,endShadow.z,
                    (vec2(startShadow.x,startShadow.y)*0.5+0.5)*vec2(0.5/atlasTexel.y),
                    (vec2(endShadow.x,endShadow.y)*0.5+0.5)*vec2(0.5/atlasTexel.y),v.parameters.y*stepSize);
            }
            visible = mix(farVisible,nearVisible,nearWeight);
        }
        integral += transmittance * (1.0-stepTransmittance) * visible;
        transmittance *= stepTransmittance;
    }
    // Henyey-Greenstein angular phase. Forward light is strongest looking sunward.
    float mu = dot(ray,v.sunDirection.xyz);
    vec3 scatter = v.sunColor.rgb * (integral * directionalScatteringScale(mu,v.parameters.w) * shaftExposure(mu));
    return scatter;
}

vec3 integrateScattering(vec2 uv, float depth) {
    return integrateScatteringQuality(uv,depth,1);
}

void main() {
    // One actual source pixel per 2x2 footprint. No synthesized min/max-depth
    // ray: that mixes foreground geometry with the direction of a sky gap.
    ivec2 sceneSize = textureSize(sceneDepth,0);
    int factor = int(v.sunDirection.w); // Low 4, High 2
    ivec2 source = min(ivec2(gl_FragCoord.xy)*factor+ivec2(factor/2),sceneSize-1);
    vec2 uv = (vec2(source)+0.5)/vec2(sceneSize);
    float depth = texelFetch(sceneDepth,source,0).r;
    // A single directional light gives all scattering the same RGB ratio.
    // Store its peak in R16F; reconstruct color after filtering. No depth
    // channel is needed: reconstruction gates use the original FP32 depth.
    vec3 radiance = integrateScattering(uv,depth);
    outColor = vec4(max(0.0,max(radiance.r,max(radiance.g,radiance.b))),0.0,0.0,1.0);
}
