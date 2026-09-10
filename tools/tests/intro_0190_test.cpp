#include "core/character_intro.hpp"
#include "core/intro_stream_warmup.hpp"
#include <cassert>
#include <iostream>
int main(){
 wowee::pipeline::CharacterIntroPlan plan;plan.durationMs=79333;plan.mapId=530;plan.sequenceId=162;
 wowee::pipeline::CharacterIntroShot shot;shot.durationMs=79333;shot.cameraId=243;shot.camera.fov=1.2f;shot.camera.positionBase={0,0,20};shot.camera.targetBase={100,0,20};plan.shots.push_back(shot);
 wowee::core::CharacterIntro intro;assert(intro.start(plan));intro.advance(23,true);assert(intro.active()&&intro.frame()->sequenceTimeMs==23000);
 intro.advance(45,false);assert(intro.active()&&intro.frame()->sequenceTimeMs==23000&&intro.paused());
 intro.advance(.25f,true);assert(intro.frame()->sequenceTimeMs==23250);intro.advance(56,true);assert(intro.active());intro.advance(.083f,true);if(intro.active())intro.advance(.001f,true);assert(intro.finished()&&!intro.failed());
 std::cout<<"PASS cinematic duration: 79-second sequence survives a 45-second streaming pause and resumes without skipping\n";
 wowee::core::IntroStreamWarmup w;assert(!w.ready(0,false,true,20));assert(!w.ready(0,true,false,.016));assert(!w.ready(0,true,false,.016));assert(w.ready(0,true,false,.016));
 assert(!w.ready(0,false,true,1));assert(!w.ready(0,true,true,1));assert(!w.ready(0,true,true,1));assert(w.ready(0,true,true,1));
 std::cout<<"PASS cinematic warmup: same-shot tile stalls reset readiness and resume after uploads or bounded grace\n";
}
