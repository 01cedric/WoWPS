"""Run the production intro-return branch with deterministic scene completion."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/core/application_character_intro.cpp").read_text()
start = source.index("    if (introReturning_) {", source.index("void Application::updateCharacterIntro"))
end = source.index("\n    if (!characterIntro_", start)
branch = source[start:end]
code = r'''
#include "core/intro_stream_warmup.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#define LOG_INFO(...) ((void)0)
#define LOG_WARNING(...) ((void)0)
constexpr float kStreamTimeoutSeconds = 60.0f;
constexpr size_t kNoShot = std::numeric_limits<size_t>::max();
struct Position { float x,y,z; };
struct Camera { Position position{}; void setPosition(Position p) { position=p; } };
struct Terrain {
    bool spawnReady=false, cameraReady=false, finalizing=false;
    std::vector<bool> priorities;
    bool isTileSceneReadyAt(float x,float) const { return x==1 ? spawnReady : cameraReady; }
    bool hasFinalizationWork() const { return finalizing; }
};
bool requestPosition(Terrain& t, Position, bool priority=false) {
    t.priorities.push_back(priority); return true;
}
struct Realm { int completions=0; bool completeIntro() { ++completions; return true; } };
struct Player { unsigned mapId=0; };
struct ReturnHarness {
    Camera cameraState; Terrain terrainState; Realm realm; Player playerState;
    Realm* localRealm_=&realm;
    bool introReturning_=true, introCompleteOnReturn_=true, localRealmWmoOnly_=false;
    bool released=false, logoutToLoginPending_=false;
    Position introSpawnPosition_{1,0,0}, introSavedCameraPosition_{2,0,2};
    float introWaitSeconds_=0;
    std::string disconnectNotice_;
    wowee::core::IntroStreamWarmup introWarmup_;
    void stopCharacterIntro(bool completed) {
        assert(!completed); released=true; introReturning_=false; introWarmup_.reset();
    }
    void update(float elapsed) {
        auto* camera=&cameraState; auto* terrain=&terrainState;
        [[maybe_unused]] auto* player=&playerState;
''' + branch + r'''
    }
};
int main() {
    ReturnHarness crypt;
    // ADT presence is insufficient: keep controls locked until both scenes
    // finish, and actively request object repair at both positions.
    crypt.terrainState.cameraReady=true;
    crypt.update(.016f);
    assert(!crypt.released && crypt.realm.completions==0);
    assert(crypt.terrainState.priorities.size()==2);
    for (bool priority:crypt.terrainState.priorities) assert(priority);
    crypt.terrainState.spawnReady=true;
    crypt.update(.016f); crypt.update(.016f);
    assert(!crypt.released);
    crypt.terrainState.cameraReady=false; crypt.update(.016f);
    crypt.terrainState.cameraReady=true;
    crypt.update(.016f); crypt.update(.016f); assert(!crypt.released);
    crypt.update(.016f);
    assert(crypt.released && crypt.realm.completions==1 && !crypt.logoutToLoginPending_);
    std::cout << "PASS spawn return waits for both object scenes, repairs priority tiles and resets warmup on scene loss\n";

    ReturnHarness uploads;
    uploads.terrainState.spawnReady=uploads.terrainState.cameraReady=true;
    uploads.terrainState.finalizing=true;
    uploads.update(1); uploads.update(1); assert(!uploads.released);
    uploads.update(1); assert(uploads.released && uploads.realm.completions==1);
    std::cout << "PASS completed spawn scene has bounded GPU-upload grace\n";

    ReturnHarness timeout;
    timeout.update(59); assert(!timeout.released);
    timeout.update(1);
    assert(timeout.released && timeout.logoutToLoginPending_);
    assert(!timeout.disconnectNotice_.empty() && timeout.realm.completions==0);
    ReturnHarness interrupted;
    interrupted.introCompleteOnReturn_=false;
    interrupted.terrainState.spawnReady=interrupted.terrainState.cameraReady=true;
    for(int i=0;i<3;++i) interrupted.update(.016f);
    assert(interrupted.released && interrupted.realm.completions==0);
    std::cout << "PASS failed or interrupted return never persists cinematic completion; timeout schedules safe logout\n";
}
'''
with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    (directory / "test.cpp").write_text(code)
    subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(root / "include"), str(directory / "test.cpp"),
                    "-o", str(directory / "test")], check=True)
    subprocess.run([str(directory / "test")], check=True)
