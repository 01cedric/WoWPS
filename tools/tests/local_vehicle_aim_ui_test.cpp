#include "ui/local_vehicle_aim.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using wowee::ui::LocalVehicleAimInput;
int main() {
    LocalVehicleAimInput s;
    s.observe(1,10,0,0,.2f,10);
    assert(!s.ready(10) && s.settled());
    s.edit(0,.2f,-1.4f,1.4f); assert(!s.ready(10));
    s.edit(.5f,.3f,-1.4f,1.4f); assert(s.ready(10));
    s.submitted(10,true);assert(!s.ready(11) && !s.settled());
    s.edit(.7f,.4f,-1.4f,1.4f);assert(!s.ready(11));
    s.observe(1,10,0,0,.2f,10.1);assert(!s.ready(10.1));
    s.observe(1,10,0,.5f,.3f,10.2);assert(!s.ready(10.2));
    assert(s.ready(10.25) && LocalVehicleAimInput::same(s.yaw,.7f));
    s.submitted(10.25,true);s.observe(1,10,0,.7f,.4f,10.3);assert(s.settled());
    s.edit(8,100,-1.4f,1.4f);assert(std::abs(s.yaw)<3.141593f && s.pitch==1.4f);
    s.submitted(10.6,true);s.edit(1,-1,-1.4f,1.4f);
    s.observe(1,10,0,.7f,.4f,12.61);assert(s.settled() && LocalVehicleAimInput::same(s.yaw,.7f));
    s.edit(1,1,-1.4f,1.4f);s.submitted(13,true);
    s.observe(1,10,1,-.3f,-.2f,13.1);assert(s.settled() && s.seat==1 && s.yaw==-.3f);
    s.edit(1,1,-1.4f,1.4f);s.submitted(13.4,true);
    s.observe(1,0,0,0,0,13.5);assert(!s.ready(14) && s.settled());
    s.observe(2,10,1,.2f,.3f,14);assert(s.owner==2 && s.yaw==.2f);
    s.edit(std::numeric_limits<float>::quiet_NaN(),0,-1.4f,1.4f);assert(s.settled());
    s.edit(.4f,.5f,-1.4f,1.4f);s.submitted(14,false);
    assert(!s.ready(14.1) && s.ready(14.25));
    // LAN98's 12-bit angle encoding must acknowledge the sent aim even
    // at its worst half-step error; otherwise a guest stalls until timeout.
    s.submitted(14.25,true);
    const auto quantized=[](float v,float limit){return float(std::lround(v/limit*2047.f))*(limit/2047.f);};
    s.observe(2,10,1,quantized(.4f,3.14159265358979323846f),quantized(.5f,1.4f),14.3);
    assert(s.settled());
    assert(LocalVehicleAimInput::same(0,3.14159265358979323846f/4094.f));
    assert(!LocalVehicleAimInput::same(0,.002f));
    std::cout << "PASS vehicle aim UI: idle suppression, coalescing, confirmed aim, rate limit, bounds, timeout, identity reset, rejected submission, LAN98 quantization\n";
}
