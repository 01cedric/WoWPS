// CPU geometry regression: actual culling helpers, no claim of GPU pixels.
#include "rendering/shadow_caster_bounds.hpp"
#include "rendering/visibility_bounds.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cassert>
#include <cstdio>
using namespace glm;
using namespace wowee::rendering;
int main() {
    unsigned checks=0;
    for (float altitude : {15.f,30.f,45.f,70.f}) for(int azimuth=0;azimuth<360;azimuth+=15) {
        const float a=radians(altitude), b=radians(float(azimuth));
        // Travel direction FROM sun/moon; both use the same downward key-light convention.
        const vec3 travel(cos(a)*cos(b),cos(a)*sin(b),-sin(a));
        const vec3 center(1200,-3400,100), up(0,0,1);
        mat4 projection=ortho(-70.f,70.f,-70.f,70.f,1.f,1950.f);
        projection[1][1]*=-1;
        const mat4 matrix=projection*lookAt(center-travel*900.f,center,up);
        Frustum frustum;frustum.extractFromMatrix(matrix);
        for(float motion : {-25.f,0.f,25.f}) for(float height : {1.f,2.f,20.f,80.f,160.f}) {
            const vec3 ground=center+vec3(motion,0,0);
            const vec3 caster=ground-travel*(height/-travel.z);
            const vec4 g=matrix*vec4(ground,1), c=matrix*vec4(caster,1);
            assert(length(vec2(g)-vec2(c))<.00003f);
            assert(c.z>=0 && c.z<g.z && g.z<=1);
            // WMO moving/elevated AABB and character padded skinned bounds must
            // remain eligible although elevated casters can be far off camera.
            assert(shadowIntersectsWorldBounds(matrix,caster-vec3(1),caster+vec3(1)));
            assert(modelBoundsInFrustum(frustum,translate(mat4(1),caster),vec3(-1),vec3(1),1.f));
            ++checks;
        }
        // Unrelated volume remains rejectable rather than disabling culling.
        const vec3 right=normalize(cross(travel,up));
        const vec3 outside=center+right*150.f;
        assert(!shadowIntersectsWorldBounds(matrix,outside-vec3(1),outside+vec3(1)));
    }
    printf("PASS %u moving/elevated ground-projection cases: matching light XY, nearer caster depth, WMO and character culling; 96 outside-volume rejections\n",checks);
}
