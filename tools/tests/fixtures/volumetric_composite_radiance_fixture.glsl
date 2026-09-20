#version 450
// Denoise complete scene-resolution directional radiance, then apply
// the bounded display mapping and analytic height fog before UI rendering.
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

// Filter raw radiance after reconstruction AND exact fallback. Filtering only
// the reduced-resolution texture leaves unsupported leaf/building pixels noisy.
// Endpoint gating uses original FP32 depth; R16F stores radiance only.
// Equal projected planes retain continuous filtering.
vec3 filteredRadiance(ivec2 source, ivec2 sceneSize, float depth) {
    float left = texelFetch(sceneDepth,max(source-ivec2(1,0),ivec2(0)),0).r;
    float right = texelFetch(sceneDepth,min(source+ivec2(1,0),sceneSize-1),0).r;
    float down = texelFetch(sceneDepth,max(source-ivec2(0,1),ivec2(0)),0).r;
    float up = texelFetch(sceneDepth,min(source+ivec2(0,1),sceneSize-1),0).r;
    vec2 before = vec2(depth-left,depth-down);
    vec2 after = vec2(right-depth,up-depth);
    vec2 gradient = vec2(abs(before.x)<abs(after.x)?before.x:after.x,
                         abs(before.y)<abs(after.y)?before.y:after.y);
    // A one-pixel foreground/background extremum has two opposing slopes;
    // neither describes its surface. Zero that axis to prevent a fictitious
    // plane crossing the silhouette and accepting light from another layer.
    if (before.x*after.x<=0.0) gradient.x=0.0;
    if (before.y*after.y<=0.0) gradient.y=0.0;
    if (source.x==0 || source.y==0 || source.x==sceneSize.x-1 || source.y==sceneSize.y-1)
        gradient=vec2(0.0);
    float sum = 0.0;
    float validWeight = 0.0;
    // High: 5x5 sigma 1.5; Low: 3x3 sigma 1.0. Both filter exact fallback.
    // Only homogeneous w is needed for eye depth; avoid two mat4 products/tap.
    vec3 inverseW = vec3(v.inverseRelativeViewProjection[0][3],
                         v.inverseRelativeViewProjection[1][3],v.inverseRelativeViewProjection[3][3]);
    float depthW = v.inverseRelativeViewProjection[2][3];
    int radius = v.sunDirection.w > 3.0 ? 1 : 2;
    // Fixed Gaussian factors: exp(-offset^2/(2*sigma^2)). Separable
    // constants avoid a transcendental instruction for every full-res tap.
    vec3 axisWeights = radius == 1 ? vec3(1.0,0.6065306597,0.0) :
                                    vec3(1.0,0.8007374029,0.4111122905);
    vec2 inverseSize = 1.0/vec2(sceneSize);
    float minimumW = max(1.0/v.parameters.x,0.000001);
    for (int y=-2;y<=2;++y) for (int x=-2;x<=2;++x) {
        if (abs(x)>radius || abs(y)>radius) continue;
        ivec2 samplePixel = clamp(source+ivec2(x,y),ivec2(0),sceneSize-1);
        vec2 guideUV = (vec2(samplePixel)+0.5)*inverseSize;
        float guideDepth = texelFetch(sceneDepth,samplePixel,0).r;
        float predictedDepth = depth+dot(gradient,vec2(samplePixel-source));
        float baseW = dot(inverseW,vec3(guideUV*2.0-1.0,1.0));
        // |1/a-1/b| <= max(epsilon,relative/b) is equivalent to
        // |b-a| <= max(epsilon*a*b,relative*a), for positive a,b.
        // Clamp reciprocal depth at the march limit before comparing. This
        // preserves the original endpoint gate without two divides per tap.
        float guideW = max(baseW+depthW*guideDepth,minimumW);
        float predictedW = max(baseW+depthW*predictedDepth,minimumW);
        if (abs(predictedW-guideW)<=max(0.0005*guideW*predictedW,0.0002*guideW)) {
            float weight = axisWeights[abs(x)]*axisWeights[abs(y)];
            vec4 tap = texelFetch(scattering,samplePixel,0);
            sum += tap.r*weight;
            validWeight += weight;
        }
    }
    // The center always matches and supplies positive weight. Explicit fallback
    // protects unusual matrices; it never substitutes background illumination.
    vec4 center = texelFetch(scattering,source,0);
    float peak = validWeight > 0.000001 ? sum/validWeight : center.r;
    vec3 color = max(v.sunColor.rgb,vec3(0.0));
    float sourcePeak = max(color.r,max(color.g,color.b));
    return sourcePeak > 0.0 ? color*(peak/sourcePeak) : vec3(0.0);
}
void main() {
    ivec2 sceneSize = textureSize(sceneDepth,0);
    ivec2 source = clamp(ivec2(TexCoord*vec2(sceneSize)),ivec2(0),sceneSize-1);
    vec2 uv = (vec2(source)+0.5)/vec2(sceneSize);
    float depth = texelFetch(sceneDepth,source,0).r;
    vec4 endpoint = v.inverseRelativeViewProjection*vec4(uv*2.0-1.0,depth,1.0);
    float eyeDepth = min(1.0/max(endpoint.w,0.000001),65000.0);
    if (v.sunColor.w>0.5 && v.sunColor.w<1.5) {
        outColor = vec4(vec3(clamp(eyeDepth/v.parameters.x,0.0,1.0)),1.0);
        return;
    }
    if (v.sunColor.w>1.5 && v.sunColor.w<2.5) {
        outColor = vec4(vec3(textureLod(shadowDepth,uv,0.0).r),1.0);
        return;
    }
    vec3 radiance = dot(v.sunColor.rgb,v.sunColor.rgb)>0.0 ?
                    filteredRadiance(source,sceneSize,depth) : vec3(0.0);
    radiance = max(radiance,vec3(0.0));
    float peakRadiance = max(radiance.r,max(radiance.g,radiance.b));
    vec3 displayed = radiance*volumeDisplayScale(peakRadiance);
    // Volume inspection displays the denoised full-resolution radiance only.
    if (v.sunColor.w>2.5) {
        outColor = vec4(displayed,1.0);
        return;
    }
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
