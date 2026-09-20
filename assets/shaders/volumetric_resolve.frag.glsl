#version 450
// Integrate shadowed single scattering along a reconstructed world-space ray.
// The depth endpoint stops rays at opaque geometry. Shadow-frustum exits add
// no light: guessing 'unshadowed' there would illuminate buildings and caves.
layout(set=0,binding=0) uniform sampler2D sceneDepth;
layout(set=0,binding=1) uniform sampler2D scattering;
layout(set=0,binding=3) uniform sampler2D shadowDepth;
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

// Scene material fog is already applied. This bounded LDR resolve adds only
// directional in-scattering; it is not a second atmospheric extinction pass.
float volumeDisplayScale(float peakRadiance) {
    return 1.0/(1.0+max(peakRadiance,0.0));
}
// Analytic Beer-Lambert integration of a height-stratified world-space medium.
// Density is constant below the local ground plane and decays above it. This
// handles horizon-parallel and ground-crossing segments without sample bands.
float heightFogOpticalDepth(vec3 eyeToSurface) {
    float distance = min(length(eyeToSurface),v.fogParameters.w);
    if (distance <= 0.0001 || v.fogParameters.x <= 0.0) return 0.0;
    float dz = eyeToSurface.z * distance/max(length(eyeToSurface),0.0001);
    float start = v.camera.z-v.fogParameters.y;
    float finish = start+dz;
    float low = min(start,finish);
    float high = max(start,finish);
    float integral = distance;
    if (high > 0.0) {
        float belowFraction = low < 0.0 ? -low/max(high-low,0.0001) : 0.0;
        float aboveLength = distance*(1.0-belowFraction);
        float aboveLow = max(low,0.0);
        float span = (high-aboveLow)*v.fogParameters.z;
        // Stable limit for horizontal rays; no cancellation at the horizon.
        float meanDensity = span < 0.001 ? 1.0-0.5*span+span*span/6.0 : (1.0-exp(-span))/span;
        integral = distance*belowFraction + aboveLength*exp(-aboveLow*v.fogParameters.z)*meanDensity;
    }
    return clamp(v.fogParameters.x*integral,0.0,0.65);
}

float volumeCoordinate(float pixel, int sceneSize, int volumeSize, int factor) {
    if (volumeSize<=1) return 0.0;
    float center = float(factor/2)+0.5;
    float previous = float(volumeSize-2)*float(factor)+center;
    float last = min(float(volumeSize-1)*float(factor)+center,float(sceneSize)-0.5);
    if (pixel>previous)
        return float(volumeSize-2)+(pixel-previous)/(last-previous);
    return (pixel-center)/float(factor);
}
// A continuous quadratic B-spline kernel spans three volume texels per axis.
// Unlike a box or nearest-centre Gaussian, its weights remain continuous when
// the footprint advances. Invalid depth layers contribute no weight or light.
float volumeFilterWeight(float distance) {
    float x = abs(distance);
    if (x < 0.5) return 0.75-x*x;
    float tail = max(1.5-x,0.0);
    return 0.5*tail*tail;
}
void main() {
    ivec2 sceneSize = textureSize(sceneDepth,0);
    ivec2 source = clamp(ivec2(TexCoord*vec2(sceneSize)),ivec2(0),sceneSize-1);
    vec2 uv = (vec2(source)+0.5)/vec2(sceneSize);
    float depth = texelFetch(sceneDepth,source,0).r;
    vec4 endpoint = v.inverseRelativeViewProjection*vec4(uv*2.0-1.0,depth,1.0);
    float eyeDepth = min(1.0/max(endpoint.w,0.000001),65000.0);
    ivec2 size = textureSize(scattering,0);
    vec2 pixel = vec2(source)+0.5;
    int factor = int(v.sunDirection.w);
    vec2 position = vec2(volumeCoordinate(pixel.x,sceneSize.x,size.x,factor),
                         volumeCoordinate(pixel.y,sceneSize.y,size.y,factor));
    ivec2 center = ivec2(floor(position+0.5));
    // Device depth is affine over a projected plane. Select the one-sided
    // derivative with the smaller change so an adjacent door/branch does not
    // tilt the foreground plane toward the background. Validate every tap
    // against that plane; curved/discontinuous regions use exact rays.
    float left = texelFetch(sceneDepth,max(source-ivec2(1,0),ivec2(0)),0).r;
    float right = texelFetch(sceneDepth,min(source+ivec2(1,0),sceneSize-1),0).r;
    float down = texelFetch(sceneDepth,max(source-ivec2(0,1),ivec2(0)),0).r;
    float up = texelFetch(sceneDepth,min(source+ivec2(0,1),sceneSize-1),0).r;
    vec2 before = vec2(depth-left,depth-down);
    vec2 after = vec2(right-depth,up-depth);
    vec2 gradient = vec2(abs(before.x)<abs(after.x)?before.x:after.x,
                         abs(before.y)<abs(after.y)?before.y:after.y);
    // At the image boundary only one side exists. A single neighbor cannot
    // distinguish a plane from a layer step; require equal depths there, or
    // march the target exactly (only the one-pixel perimeter is affected).
    // Opposing slopes mark an isolated layer, not a tilted receiving plane.
    if (before.x*after.x<=0.0) gradient.x=0.0;
    if (before.y*after.y<=0.0) gradient.y=0.0;
    if (source.x==0 || source.y==0 || source.x==sceneSize.x-1 || source.y==sceneSize.y-1)
        gradient=vec2(0.0);
    float minimumW = max(1.0/min(v.parameters.x,65000.0),0.000001);
    // Camera supplies a perspective projection (optionally XY-jittered).
    // Eye-relative view rotation cannot change its homogeneous bottom row:
    // inverseVP[0][3] and [1][3] are zero, so reciprocal depth has no UV term.
    // Reuse this invariant for every original depth gate.
    float baseW = v.inverseRelativeViewProjection[3][3];
    float depthW = v.inverseRelativeViewProjection[2][3];
    float sum = 0.0;
    float validWeight = 0.0;
    for (int y=-1;y<=1;++y) for (int x=-1;x<=1;++x) {
        ivec2 kernelPixel = center+ivec2(x,y);
        ivec2 samplePixel = clamp(kernelPixel,ivec2(0),size-1);
        vec4 tap = texelFetch(scattering,samplePixel,0);
        vec2 distance = vec2(kernelPixel)-position;
        float weight = volumeFilterWeight(distance.x)*volumeFilterWeight(distance.y);
        // Read original FP32 source depth; R16F stores peak radiance only.
        // This avoids a quantization-dependent gate and resolves close doors.
        ivec2 guidePixel = min(samplePixel*factor+ivec2(factor/2),sceneSize-1);
        float guideDepth = texelFetch(sceneDepth,guidePixel,0).r;
        float predictedDepth = depth+dot(gradient,vec2(guidePixel-source));
        // Compare reciprocal depths directly: exactly the same positive-depth
        // relative/absolute tolerance as the eye-depth gate, without per-tap
        // reciprocals. Clamp both at the original capped march range.
        float guideW = max(baseW+depthW*guideDepth,minimumW);
        float predictedW = max(baseW+depthW*predictedDepth,minimumW);
        if (abs(predictedW-guideW)<=max(0.0005*guideW*predictedW,0.0002*guideW)) {
            sum += tap.r*weight;
            validWeight += weight;
        }
    }
    // A single mismatched foreground/background tap no longer cancels all
    // smoothing. Only matching layers are normalized and reconstructed.
    // Thin unsupported silhouettes still integrate their own exact endpoint;
    // four times the strata reduce noise there without borrowing sky light.
    // This costs 32/48 shadow steps only where no matching guide exists.
    float radiance;
    if (validWeight > 0.000001) {
        radiance = sum/validWeight;
    } else {
        vec3 exact = integrateScatteringQuality(uv,depth,4);
        radiance = max(exact.r,max(exact.g,exact.b));
    }
    // All exact fallback radiance still enters the full-resolution denoise.
    outColor = vec4(max(radiance,0.0),0.0,0.0,1.0);
}
