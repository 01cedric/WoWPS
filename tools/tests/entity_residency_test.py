#!/usr/bin/env python3
"""Exercise the actual spawner retirement code with a deterministic renderer seam.

This does not emulate GPU fences. The renderer contract is tested separately;
here the important boundary is protecting queued users and invalidating handles
after retirement, including a partial cleanup interrupted by allocation failure.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "src/core/entity_spawner.cpp").read_text()


def between(start, end):
    return source[source.index(start):source.index(end, source.index(start))]


functions = between("void EntitySpawner::reclaimUnusedPresentationAssets()", "namespace {\n/// What one display")
functions += between("size_t predecodedEntryBytes(", "} // namespace\n\nvoid EntitySpawner::storePredecodedSkins")
functions += between("void EntitySpawner::storePredecodedSkins(", "void EntitySpawner::syncCreatureStealthVisuals()")

fixture = r'''
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#define LOG_INFO(...) ((void)0)
namespace pipeline {
struct BLPImage { std::vector<uint8_t> data; std::vector<std::vector<uint8_t>> mipmaps; };
}
struct FakeCharacterRenderer {
    std::unordered_set<uint32_t> models, active;
    bool failAfterRetirement = false;
    unsigned calls = 0;
    const void* getModelData(uint32_t id) const { return models.count(id) ? this : nullptr; }
    void reclaimUnusedResources(const std::unordered_set<uint32_t>& preparing) {
        ++calls;
        for (auto it = models.begin(); it != models.end();) {
            if (active.count(*it) || preparing.count(*it)) ++it;
            else it = models.erase(it);
        }
        if (failAfterRetirement) throw std::bad_alloc();
    }
};
struct FakeRenderer {
    FakeCharacterRenderer characters;
    FakeCharacterRenderer* getCharacterRenderer() const {
        return const_cast<FakeCharacterRenderer*>(&characters);
    }
};
struct EntitySpawner {
    struct Spawn { uint32_t displayId; };
    struct Composite { uint32_t modelId; };
    struct Player { uint8_t raceId, genderId; };
    struct AssetCache {
        std::mutex mutex;
        std::unordered_map<std::string, std::weak_ptr<int>> models;
    };
    FakeRenderer* renderer_;
    std::chrono::steady_clock::time_point lastPresentationReclaimAt_{};
    std::unordered_set<uint32_t> preparingCharacterModels_;
    std::unordered_map<uint32_t, uint32_t> displayIdModelCache_, playerModelCache_;
    std::unordered_map<uint32_t, bool> modelIdIsWolfLike_;
    std::unordered_map<uint32_t, int> playerTextureSlotsByModelId_;
    std::unordered_set<uint32_t> displayIdTexturesApplied_;
    std::vector<Spawn> pendingCreatureSpawns_;
    std::vector<Composite> asyncNpcCompositeLoads_;
    std::vector<Player> pendingPlayerSpawns_;
    uint32_t pendingMountDisplayId_ = 0;
    std::unordered_map<uint64_t, uint32_t> pendingRemotePlayerMounts_;
    std::unordered_map<std::string, uint32_t> attachmentModelIds_;
    std::unordered_map<std::string, std::shared_ptr<int>> attachmentModelData_;
    std::shared_ptr<AssetCache> creatureAssetCache_ = std::make_shared<AssetCache>();
    std::unordered_map<uint32_t, std::unordered_map<std::string, pipeline::BLPImage>> displayIdPredecodedTextures_;
    std::deque<uint32_t> predecodedSkinOrder_;
    size_t predecodedSkinBytes_ = 0;
    void reclaimUnusedPresentationAssets();
    uint32_t cachedCreatureModelId(uint32_t);
    void storePredecodedSkins(uint32_t, std::unordered_map<std::string, pipeline::BLPImage>);
    void releasePredecodedSkins(uint32_t);
    size_t trimPredecodedSkins(size_t);
    void sweep() { lastPresentationReclaimAt_ = {}; reclaimUnusedPresentationAssets(); }
};
'''

cases = r'''
static std::unordered_map<std::string, pipeline::BLPImage> skin(size_t bytes = 64) {
    pipeline::BLPImage b; b.data.resize(bytes); return {{"skin", std::move(b)}};
}
int main() {
    FakeRenderer renderer;
    EntitySpawner s{&renderer};
    auto& r = renderer.characters;
    r.models = {1,2,3,4,5,6,7,8,9}; r.active = {1,8};
    s.displayIdModelCache_ = {{50,1},{51,2},{52,3},{56,6},{57,7}};
    s.displayIdTexturesApplied_ = {50,51,52,56,57};
    s.modelIdIsWolfLike_[3] = true;
    s.pendingCreatureSpawns_.push_back({51});
    s.asyncNpcCompositeLoads_.push_back({4});
    s.playerModelCache_[1 << 8] = 5; s.playerTextureSlotsByModelId_[5] = 1;
    s.pendingPlayerSpawns_.push_back({1,0});
    s.pendingMountDisplayId_ = 56; s.pendingRemotePlayerMounts_[9000] = 57;
    s.attachmentModelIds_ = {{"helmet|red",8},{"helmetExtra|blue",9}};
    s.attachmentModelData_ = {{"helmet",std::make_shared<int>(8)},
                              {"helmetExtra",std::make_shared<int>(9)}, {"missing",nullptr}};
    s.creatureAssetCache_->models["expired"] = std::make_shared<int>(1);
    auto workerOwned = std::make_shared<int>(2);
    s.creatureAssetCache_->models["worker"] = workerOwned;
    s.storePredecodedSkins(52, skin());
    s.sweep();
    assert((r.models == std::unordered_set<uint32_t>{1,2,4,5,6,7,8}));
    assert(!s.displayIdModelCache_.count(52) && !s.displayIdTexturesApplied_.count(52));
    assert(!s.modelIdIsWolfLike_.count(3));
    assert(s.predecodedSkinBytes_ == 0 && s.predecodedSkinOrder_.empty());
    assert(s.attachmentModelData_.count("helmet") && !s.attachmentModelData_.count("helmetExtra"));
    assert(s.attachmentModelData_.count("missing") && !s.attachmentModelIds_.count("helmetExtra|blue"));
    assert(s.creatureAssetCache_->models.size() == 1 && s.creatureAssetCache_->models.count("worker"));
    s.reclaimUnusedPresentationAssets(); assert(r.calls == 1); // Maintenance is throttled.
    s.pendingCreatureSpawns_.clear(); s.asyncNpcCompositeLoads_.clear(); s.pendingPlayerSpawns_.clear();
    s.pendingMountDisplayId_ = 0; s.pendingRemotePlayerMounts_.clear(); s.sweep();
    assert((r.models == std::unordered_set<uint32_t>{1,8}));
    assert(s.displayIdModelCache_.size() == 1 && s.playerModelCache_.empty());
    assert(s.playerTextureSlotsByModelId_.empty());
    std::cout << "PASS entity residency: active + pending spawns, skins, players and mounts retained; stale handles retired\n";

    // Many area changes: parsed attachments and display ids cannot accumulate.
    for (uint32_t i=100; i<200; ++i) {
        r.models.insert(i); s.displayIdModelCache_[i]=i; s.displayIdTexturesApplied_.insert(i);
        auto path="old-area-"+std::to_string(i);
        s.attachmentModelIds_[path+"|texture"] = i;
        s.attachmentModelData_[path] = std::make_shared<int>(i);
        s.storePredecodedSkins(i,skin()); s.sweep();
        assert(r.models.size()==2 && s.displayIdModelCache_.size()==1);
        assert(s.attachmentModelData_.size()==2 && s.attachmentModelIds_.size()==1);
        assert(s.predecodedSkinBytes_==0 && s.predecodedSkinOrder_.empty());
    }
    std::cout << "PASS entity residency: 100 area turnovers retain a bounded presentation cache\n";

    // Small consumes must also bound the age index while a different skin stays resident.
    s.storePredecodedSkins(400, skin());
    for (uint32_t i=500; i<1500; ++i) {
        s.storePredecodedSkins(i, skin()); s.releasePredecodedSkins(i);
        assert(s.predecodedSkinOrder_.size()==1 && s.displayIdPredecodedTextures_.size()==1);
    }
    assert(s.trimPredecodedSkins(0)==64 && s.predecodedSkinOrder_.empty());

    // A renderer-side pressure cleanup must not strand cached display ids.
    r.models.erase(1); s.storePredecodedSkins(50,skin());
    assert(s.cachedCreatureModelId(50)==0 && s.displayIdModelCache_.empty());
    assert(s.displayIdTexturesApplied_.empty() && s.predecodedSkinBytes_==0);
    r.models.insert(999); s.displayIdModelCache_[999]=999;
    s.displayIdTexturesApplied_.insert(999); s.storePredecodedSkins(999,skin());
    r.failAfterRetirement = true;
    s.sweep(); // A small bookkeeping OOM after retirement cannot escape into the frame loop.
    assert(!s.displayIdModelCache_.count(999) && !s.displayIdTexturesApplied_.count(999));
    assert(s.predecodedSkinBytes_==0);
    std::cout << "PASS entity residency: bounded skin age records, external eviction and partial-OOM reconciliation\n";
}
'''

with tempfile.TemporaryDirectory(prefix="wowps-entity-residency-") as tmp:
    unit = Path(tmp) / "residency.cpp"
    binary = Path(tmp) / "residency"
    unit.write_text(fixture + functions + cases)
    command = shlex.split(os.environ.get("CXX", "clang++-18"))
    command += ["-std=c++20", "-O1", "-g", "-Wall", "-Wextra", "-Wno-missing-field-initializers"]
    if os.environ.get("SANITIZE") == "1":
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command + [str(unit), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
