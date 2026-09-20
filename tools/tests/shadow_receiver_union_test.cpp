#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "rendering/shadow_receiver_hull.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cassert>
#include <chrono>
#include <iostream>
#include <random>
using namespace wowee::rendering;
static bool inClip(glm::vec3 p,const glm::mat4& clip) {
    const auto q=clip*glm::vec4(p,1);
    return q.w>0 && std::abs(q.x)<=q.w && std::abs(q.y)<=q.w && q.z>=0 && q.z<=q.w;
}
int main() {
    std::mt19937 rng(276);
    std::uniform_real_distribution<float> unit(-1,1), pos(0,1);
    unsigned covered=0,excluded=0;
    const auto start=std::chrono::steady_clock::now();
    for(unsigned scene=0;scene<40;++scene) {
        const glm::vec3 eye(12000+1000*unit(rng),-8800+1000*unit(rng),120+100*unit(rng));
        const auto forward=glm::normalize(glm::vec3(unit(rng),unit(rng),unit(rng)*0.4f));
        const auto lightDir=glm::normalize(glm::vec3(unit(rng),unit(rng),-0.2f-std::abs(unit(rng))));
        const auto camera=glm::perspective(glm::radians(50.f),16.f/9.f,0.1f,30000.f)*glm::lookAt(eye,eye+forward,glm::vec3(0,0,1));
        const auto center=eye+forward*250.f;
        const auto light=glm::ortho(-500.f,500.f,-500.f,500.f,1.f,3250.f)*glm::lookAt(center-lightDir*1500.f,center,glm::vec3(0,0,1));
        ShadowReceiverHull main;
        main.buildClipped(camera,light,5.f);
        assert(main.size()>=3);
        for(unsigned i=0;i<5000;++i) {
            const glm::vec3 receiver=eye+forward*(pos(rng)*1600.f)+glm::vec3(unit(rng),unit(rng),unit(rng))*600.f;
            if(inClip(receiver,camera) && inClip(receiver,light)) {
                const auto caster=receiver+lightDir*unit(rng)*5000.f;
                assert(main.intersects(caster,0));
                assert(main.intersectsBounds(caster-glm::vec3(1),caster+glm::vec3(1)));
                ++covered;
            }
            excluded+=!main.intersects(center+glm::vec3(unit(rng),unit(rng),unit(rng))*500.f,0);
        }
    }
    assert(covered>1000 && excluded>1000);
    // Two separated receiver boxes: preserve both, exclude the gap. Include
    // reflected receivers without changing all queries to unconditional true.
    const auto light=glm::ortho(-100.f,100.f,-100.f,100.f,1.f,1000.f)*
        glm::lookAt(glm::vec3(0,0,500),glm::vec3(0),glm::vec3(0,1,0));
    const auto left=glm::ortho(-5.f,5.f,-5.f,5.f,1.f,100.f)*
        glm::lookAt(glm::vec3(-40,0,50),glm::vec3(-40,0,0),glm::vec3(0,1,0));
    const auto right=glm::ortho(-5.f,5.f,-5.f,5.f,1.f,100.f)*
        glm::lookAt(glm::vec3(40,0,50),glm::vec3(40,0,0),glm::vec3(0,1,0));
    ShadowReceiverHull main,reflection;
    main.buildClipped(left,light);reflection.buildClipped(right,light);
    assert(!main.intersects(glm::vec3(40,0,0),0));
    main.includeAdditional(&reflection);
    assert(main.intersects(glm::vec3(40,0,0),0));
    assert(main.intersects(glm::vec3(-40,0,0),0));
    assert(!main.intersects(glm::vec3(0),0));
    assert(main.intersectsBounds(glm::vec3(39,-1,-1),glm::vec3(41,1,1)));
    auto model=glm::translate(glm::mat4(1),glm::vec3(40,0,0));
    assert(main.intersectsTransformedBounds(glm::vec3(-1),glm::vec3(1),model));
    // Exercise the same oblique-plane transform used by planar water.
    // Its far plane is not the original camera far plane; do not approximate
    // the reflected receivers by mirroring the eight main-camera corners.
    unsigned reflectedCovered=0;
    for (unsigned i=0;i<20;++i) {
        const float pitch=-0.8f+0.08f*i;
        const glm::vec3 reflectedEye(0,0,-20);
        const auto reflectedView=glm::lookAt(reflectedEye,
            reflectedEye+glm::normalize(glm::vec3(0,1,pitch)),glm::vec3(0,0,1));
        auto projection=glm::perspective(glm::radians(50.f),16.f/9.f,0.1f,30000.f);
        projection[1][1]*=-1.f;
        const auto clipPlane=glm::transpose(glm::inverse(reflectedView))*glm::vec4(0,0,1,0);
        const glm::vec4 q((glm::sign(clipPlane.x)+projection[2][0])/projection[0][0],
            (glm::sign(clipPlane.y)+projection[2][1])/projection[1][1],-1,
            (1+projection[2][2])/projection[3][2]);
        const auto c=clipPlane*(2.f/glm::dot(clipPlane,q));
        projection[0][2]=c.x;projection[1][2]=c.y;projection[2][2]=c.z+1;projection[3][2]=c.w;
        const auto reflectedClip=projection*reflectedView;
        reflection.buildClipped(reflectedClip,light,5);
        for (unsigned j=0;j<10000;++j) {
            const glm::vec3 receiver(unit(rng)*100,pos(rng)*100,pos(rng)*100);
            if(inClip(receiver,reflectedClip)&&inClip(receiver,light)) {
                assert(reflection.intersects(receiver+glm::vec3(0,0,1000),0));
                ++reflectedCovered;
            }
        }
    }
    assert(reflectedCovered>100);
    // Empty finite intersection is distinct from an invalid matrix. Rebuild
    // must also remove the previous additional hull.
    const auto outside=glm::ortho(-5.f,5.f,-5.f,5.f,1.f,100.f)*
        glm::lookAt(glm::vec3(400,0,50),glm::vec3(400,0,0),glm::vec3(0,1,0));
    main.buildClipped(outside,light);assert(!main.intersects(glm::vec3(0),1000));
    main.buildClipped(glm::mat4(0),light);assert(main.intersects(glm::vec3(0),0));
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"PASS clipped+union receivers="<<covered<<" reflected receivers="<<reflectedCovered<<" caster exclusions="<<excluded<<" cpuMs="<<ms<<"\n";
}
