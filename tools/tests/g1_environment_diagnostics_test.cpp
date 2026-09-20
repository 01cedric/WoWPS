#include "rendering/environment_weather.hpp"
#include "rendering/frame_timing_window.hpp"
#include "rendering/volumetric_status.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <limits>

using namespace wowee::rendering;
int main() {
    auto clear = resolveEnvironmentWeather(true, 0, 1.f, 3, 1.f);
    assert(clear.type == 0 && clear.intensity == 0 && !clear.usesOvercastLighting());
    for (unsigned type = 1; type <= 3; ++type) {
        auto server = resolveEnvironmentWeather(true, type, .8f, 0, 0.f);
        auto local = resolveEnvironmentWeather(false, 0, 0.f, type, .8f);
        assert(server.type == local.type && server.intensity == local.intensity);
        assert(server.usesOvercastLighting());
    }
    assert(resolveEnvironmentWeather(true, 99, 1.f, 0, 0.f).type == 0);
    assert(resolveEnvironmentWeather(false, 0, 0.f, 1, std::numeric_limits<float>::quiet_NaN()).type == 0);
    assert(resolveEnvironmentWeather(true, 1, 1.5f, 0, 0.f).intensity == 1.f);
    assert(!resolveEnvironmentWeather(true, 1, -.1f, 0, 0.f).usesOvercastLighting());
    std::puts("PASS one forecast: clear authority, local rain/snow/storm, finite clamping");

    FrameTimingWindow w;
    // Independent known order statistics on reversed input, not copied policy.
    for (int i=100; i>=1; --i) w.add(i/1000.0, 1., 2.);
    assert(w.frames == 100 && w.sampleCount == 100 && w.over40ms == 60);
    assert(w.over25ms == 75 && w.updateOver25ms == 0 && w.renderOver25ms == 0);
    assert(w.percentile(.50) == 50 && w.percentile(.95) == 95 && w.percentile(.99) == 99);
    assert(w.percentile(0) == 1 && w.percentile(1) == 100 && w.maxFrameMs == 100);
    assert(w.ready() && std::abs(w.fps() - 100./5.05) < 1e-6);
    w.add(0,1,1); w.add(std::numeric_limits<double>::quiet_NaN(),1,1);
    assert(w.frames == 100);
    w.reset(); assert(w.frames == 0 && w.sampleCount == 0 && w.percentile(.99) == 0);
    assert(w.over25ms == 0 && w.otherAndPacingMs == 0);
    w.add(.025, 10., 12.); // Exactly 40 FPS: not over budget, 3 ms outside these phases.
    w.add(.060, 26., 30.); // Both measured phases exceed the target independently.
    assert(w.over25ms == 1 && w.updateOver25ms == 1 && w.renderOver25ms == 1);
    assert(w.otherAndPacingMs == 7.);
    w.reset();
    for (size_t i=0;i<FrameTimingWindow::capacity+100;++i) w.add(.02, 3, 4);
    assert(w.sampleCount == FrameTimingWindow::capacity && w.frames == FrameTimingWindow::capacity+100);
    assert(w.percentile(.99) == 20);
    w.reset();
    using Clock=std::chrono::steady_clock;
    auto t=Clock::time_point{};
    w.addFrame(t,t+std::chrono::milliseconds(1),t+std::chrono::milliseconds(6),
               t+std::chrono::milliseconds(12),t+std::chrono::milliseconds(20));
    assert(w.updateMs==5 && w.renderMs==6 && w.percentile(.5)==20);
    std::puts("PASS exact bounded frame percentiles, pacing, saturation and reset");

    auto status=[](int q,bool world,bool camera,bool temporal,bool msaa,bool allowed,const char* reason,
                   bool failed,bool pipe,bool target,bool light) {
        return volumetricFrameStatus(q,world,camera,temporal,msaa,allowed,reason,failed,pipe,target,light);
    };
    assert(!std::strcmp(status(1,true,true,false,false,true,"ready",false,true,true,true),"ready"));
    assert(!std::strcmp(status(0,true,true,false,false,true,"ready",false,true,true,true),"off"));
    assert(!std::strcmp(status(1,false,true,false,false,true,"ready",false,true,true,true),"not-world"));
    assert(!std::strcmp(status(1,true,false,false,false,true,"ready",false,true,true,true),"no-camera"));
    assert(!std::strcmp(status(1,true,true,true,false,true,"ready",false,true,true,true),"temporal-upscaling"));
    assert(!std::strcmp(status(1,true,true,false,true,true,"ready",false,true,true,true),"multisampled-depth"));
    assert(!std::strcmp(status(1,true,true,false,false,false,"camera-indoors",false,true,true,true),"camera-indoors"));
    assert(!std::strcmp(status(1,true,true,false,false,true,"ready",true,false,true,true),"resource-failure"));
    assert(!std::strcmp(status(1,true,true,false,false,true,"ready",false,false,true,true),"pipeline-not-ready"));
    assert(!std::strcmp(status(1,true,true,false,false,true,"ready",false,true,false,true),"scene-target-not-ready"));
    assert(!std::strcmp(status(1,true,true,false,false,true,"ready",false,true,true,false),"no-key-light"));
    std::puts("PASS explicit ray execution/bypass reasons");
}
