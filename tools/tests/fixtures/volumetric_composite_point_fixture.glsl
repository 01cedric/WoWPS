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

// Shared verbatim with the composite fallback: both integrate the same world
// ray, clipping, shadow visibility, extinction and normalized phase.
vec3 integrateScattering(vec2 uv, float depth) {
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
    int count = int(v.parameters.z);
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
    for (int i=0; i<12; ++i) {
        if (i >= count) break;
        // The directional shadow camera is affine (orthographic, w=1).
        // Reuse the transform already needed by clipping instead of repeating
        // a world-space reconstruction and mat4 multiply for every sample.
        float stratumOffset = fract(offset+float(i)*0.61803398875);
        float sampleDistance = enter+(float(i)+stratumOffset)*stepSize;
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
                nearVisible = nearShadow.z - 0.0001 <= blocker ? 1.0 : 0.0;
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
                farVisible = shadow.z - 0.0001 <= blocker ? 1.0 : 0.0;
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
void main() {
    ivec2 sceneSize = textureSize(sceneDepth,0);
    ivec2 source = clamp(ivec2(TexCoord*vec2(sceneSize)),ivec2(0),sceneSize-1);
    vec2 uv = (vec2(source)+0.5)/vec2(sceneSize);
    float depth = texelFetch(sceneDepth,source,0).r;
    vec4 endpoint = v.inverseRelativeViewProjection*vec4(uv*2.0-1.0,depth,1.0);
    float eyeDepth = min(1.0/max(endpoint.w,0.000001),65000.0);
    // Debug uses a separate blend-disabled pipeline. Never feed raw depth or
    // diagnostic colors into normal lighting; UI labels the selected map.
    if (v.sunColor.w>0.5 && v.sunColor.w<1.5) {
        outColor = vec4(vec3(clamp(eyeDepth/v.parameters.x,0.0,1.0)),1.0);
        return;
    }
    if (v.sunColor.w>1.5 && v.sunColor.w<2.5) {
        outColor = vec4(vec3(textureLod(shadowDepth,uv,0.0).r),1.0);
        return;
    }
    if (v.sunColor.w>2.5) {
        vec3 diagnostic = textureLod(scattering,uv,0.0).rgb;
        outColor = vec4(diagnostic*volumeDisplayScale(max(diagnostic.r,max(diagnostic.g,diagnostic.b))),1.0);
        return;
    }
    ivec2 size = textureSize(scattering,0);
    vec2 pixel = vec2(source)+0.5;
    int factor = int(v.sunDirection.w);
    vec2 position = vec2(volumeCoordinate(pixel.x,sceneSize.x,size.x,factor),
                         volumeCoordinate(pixel.y,sceneSize.y,size.y,factor));
    ivec2 base = ivec2(floor(position));
    vec2 fraction = fract(position);
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
    if (source.x==0 || source.y==0 || source.x==sceneSize.x-1 || source.y==sceneSize.y-1)
        gradient=vec2(0.0);
    vec3 sum = vec3(0.0);
    bool compatible = true;
    for (int y=0;y<2;++y) for (int x=0;x<2;++x) {
        ivec2 samplePixel = clamp(base+ivec2(x,y),ivec2(0),size-1);
        vec4 tap = texelFetch(scattering,samplePixel,0);
        vec2 weights = mix(vec2(1.0)-fraction,fraction,vec2(x,y));
        float weight = weights.x*weights.y;
        // Read original full-precision source depth, not the FP16 guide. This
        // avoids a quantization-dependent gate and identifies close door layers.
        ivec2 guidePixel = min(samplePixel*factor+ivec2(factor/2),sceneSize-1);
        vec2 guideUV = (vec2(guidePixel)+0.5)/vec2(sceneSize);
        float guideDepth = texelFetch(sceneDepth,guidePixel,0).r;
        vec4 guideEndpoint = v.inverseRelativeViewProjection*vec4(guideUV*2.0-1.0,guideDepth,1.0);
        float guideEyeDepth = min(1.0/max(guideEndpoint.w,0.000001),65000.0);
        float predictedDepth = depth+dot(gradient,vec2(guidePixel-source));
        vec4 predictedEndpoint = v.inverseRelativeViewProjection*vec4(guideUV*2.0-1.0,predictedDepth,1.0);
        float predictedEyeDepth = min(1.0/max(predictedEndpoint.w,0.000001),65000.0);
        // Clamping at the march range permits reuse beyond the visible air
        // segment, while preserving all foreground/background separations that
        // can affect this volume. No FP16 depth gate or 8.3% layer tolerance.
        float guideRange = min(guideEyeDepth,v.parameters.x);
        float predictedRange = min(predictedEyeDepth,v.parameters.x);
        float tolerance = max(0.0005,predictedRange*0.0002);
        if (weight>0.00001 && abs(guideRange-predictedRange)>tolerance)
            compatible = false;
        sum += tap.rgb*weight;
    }
    vec3 radiance = compatible ? sum : integrateScattering(uv,depth);
    radiance = max(radiance,vec3(0.0));
    float peakRadiance = max(radiance.r,max(radiance.g,radiance.b));
    vec3 displayed = radiance*volumeDisplayScale(peakRadiance);
    if (v.fogParameters.x > 0.0) {
        vec3 eyeToSurface = endpoint.xyz/max(endpoint.w,0.000001);
        float transmittance = exp(-heightFogOpticalDepth(eyeToSurface));
        // Premultiplied ambient in-scatter plus bounded directional radiance.
        // Matching pipeline attenuates destination by T*(1-rayCoverage).
        // Fog-off keeps the
        // historical screen blend exactly; debug uses its unblended pipeline.
        float rayCoverage = max(displayed.r,max(displayed.g,displayed.b));
        outColor = vec4(v.fogColor.rgb*(1.0-transmittance)+displayed*transmittance,
                        1.0-transmittance*(1.0-rayCoverage));
    } else {
        outColor = vec4(displayed,1.0);
    }
}
