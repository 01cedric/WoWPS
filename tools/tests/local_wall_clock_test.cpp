#include "game/local_day_clock.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace wowee::game;
using Result = LocalWallClockFollower::Result;
static bool near(float a, float b) { return std::abs(a-b) < 0.0001f; }
int main() {
    LocalDayClock clock;
    LocalWallClockFollower follower;
    float wall = 6; bool valid = true; unsigned reads = 0;
    auto read = [&](float& out) { ++reads; out = wall; return valid; };
    assert(follower.poll(true, 100, 0, clock, read) == Result::Synchronized);
    assert(near(clock.hours(0),6));
    wall = 18;
    assert(follower.poll(true,100.9,0.9,clock,read)==Result::Idle && reads==1);
    assert(follower.poll(true,101,1,clock,read)==Result::Synchronized);
    assert(near(clock.hours(1),18));
    wall=2;
    assert(follower.poll(true,102,2,clock,read)==Result::Synchronized);
    assert(near(clock.hours(2),2));
    wall=23.9997222f;
    assert(follower.poll(true,103,3,clock,read)==Result::Synchronized);
    wall=0;
    assert(follower.poll(true,104,4,clock,read)==Result::Unchanged);
    wall=23.9f;
    assert(follower.poll(true,105,5,clock,read)==Result::Synchronized);
    assert(near(clock.hours(5),23.9f));
    // Guests/external connections cannot sample or replace authoritative time.
    const auto beforeReads=reads;
    wall=12;
    assert(follower.poll(false,110,10,clock,read)==Result::Idle && reads==beforeReads);
    assert(near(clock.hours(10),23.9f+5.0f/3600));
    // Resume/large time change polls immediately; simulation time is an input.
    const double simulationNow=11;
    assert(follower.poll(true,200,simulationNow,clock,read)==Result::Synchronized);
    assert(near(clock.hours(simulationNow),12) && simulationNow==11);
    valid=false;
    assert(follower.poll(true,201,12,clock,read)==Result::ReadFailed);
    assert(near(clock.hours(12),12+1.0f/3600));
    valid=true; wall=std::numeric_limits<float>::quiet_NaN();
    assert(follower.poll(true,202,13,clock,read)==Result::ReadFailed);
    wall=24;
    assert(follower.poll(true,203,14,clock,read)==Result::ReadFailed);
    wall=-1;
    assert(follower.poll(true,204,15,clock,read)==Result::ReadFailed);
    wall=8;
    assert(follower.poll(true,205,16,clock,read)==Result::Synchronized);
    // Monotonic input controls poll frequency even if simulation jumps wildly.
    assert(follower.poll(true,205.5,100000,clock,read)==Result::Idle);
    // First read failure must recover on the next poll.
    LocalWallClockFollower recovery;
    valid=false;
    assert(recovery.poll(true,0,0,clock,read)==Result::ReadFailed);
    valid=true;
    assert(recovery.poll(true,1,1,clock,read)==Result::Synchronized);
    std::cout << "PASS live local clock: polling, forward/backward edits, midnight, authority, resume, invalid/read failure, interpolation\n";
}
