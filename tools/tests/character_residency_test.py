#!/usr/bin/env python3
"""Run the actual character retirement code against a deterministic GPU seam.

Both CharacterRenderer methods and the real deferred-cleanup helper are used.
The seam records explicit GPU destruction and signals the two frame fences
independently. It does not emulate Vulkan or PS4 memory allocation.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "src/rendering/character_renderer.cpp").read_text()


def between(start, end):
    return source[source.index(start):source.index(end, source.index(start))]


functions = between("void CharacterRenderer::destroyModelGPU(", "void CharacterRenderer::destroyInstanceBones(")
functions += between("void CharacterRenderer::reclaimUnusedResources(", "bool CharacterRenderer::attachWeaponEffect(")

fixture = r'''
#include "rendering/deferred_cleanup.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#define LOG_INFO(...) ((void)0)
using VkBuffer = uint64_t;
using VmaAllocation = uint64_t;
using VmaAllocator = int;
constexpr VkBuffer VK_NULL_HANDLE = 0;
static std::unordered_map<uint64_t, unsigned> buffersDestroyed, texturesDestroyed, textureOwnersDestroyed;
static void vmaDestroyBuffer(VmaAllocator allocator, VkBuffer buffer, VmaAllocation allocation) {
    assert(allocator == 2718 && allocation == buffer + 100000);
    ++buffersDestroyed[buffer];
}
struct VkTexture {
    uint64_t id;
    explicit VkTexture(uint64_t value) : id(value) {}
    ~VkTexture() { ++textureOwnersDestroyed[id]; } // Deliberately does not release GPU resources.
    void destroy(int device, VmaAllocator allocator) {
        assert(device == 314 && allocator == 2718);
        ++texturesDestroyed[id];
    }
};
struct FakeVkContext {
    std::vector<std::function<void()>> queues[2];
    size_t deferCalls = 0, throwOnDefer = 0;
    int getDevice() const { return 314; }
    VmaAllocator getAllocator() const { return 2718; }
    void deferAfterAllFrameFences(std::function<void()>&& fn) {
        if (++deferCalls == throwOnDefer) throw std::bad_alloc();
        wowee::rendering::enqueueAfterAllFences(queues, std::move(fn));
    }
    void finish(unsigned frame) {
        std::vector<std::function<void()>> completed;
        completed.swap(queues[frame]);
        for (auto& fn : completed) fn();
    }
    void finishBoth() { finish(0); finish(1); }
};
struct CharacterRenderer {
    struct M2ModelGPU {
        VkBuffer vertexBuffer = 0, indexBuffer = 0;
        VmaAllocation vertexAlloc = 0, indexAlloc = 0;
        std::shared_ptr<int> data;
        std::vector<VkTexture*> textureIds;
    };
    struct CharacterInstance {
        uint32_t modelId = 0;
        std::unordered_map<uint16_t, VkTexture*> groupTextureOverrides, textureSlotOverrides;
    };
    struct TextureCacheEntry {
        std::unique_ptr<VkTexture> texture, normalHeightMap;
        float heightMapVariance = 0;
        size_t approxBytes = 0;
        uint64_t lastUse = 0;
        bool hasAlpha = false, colorKeyBlack = false, normalMapPending = false;
    };
    FakeVkContext* vkCtx_;
    std::unordered_map<uint32_t, M2ModelGPU> models;
    std::unordered_map<uint32_t, CharacterInstance> instances;
    std::unordered_map<std::string, TextureCacheEntry> textureCache;
    std::unordered_map<VkTexture*, int> texturePropsByPtr_, normalMapByTexPtr_;
    std::unordered_map<std::string, VkTexture*> compositeCache_;
    size_t textureCacheBytes_ = 0;
    void destroyModelGPU(M2ModelGPU&, bool);
    void reclaimUnusedResources(const std::unordered_set<uint32_t>& = {});
    VkTexture* texture(const std::string& key, uint64_t id, uint64_t normalId = 0, bool pending = false) {
        TextureCacheEntry entry;
        entry.texture = std::make_unique<VkTexture>(id);
        if (normalId) entry.normalHeightMap = std::make_unique<VkTexture>(normalId);
        entry.approxBytes = normalId ? 150 : 100;
        entry.normalMapPending = pending;
        textureCacheBytes_ += entry.approxBytes;
        auto* result = entry.texture.get();
        texturePropsByPtr_[result] = 1;
        normalMapByTexPtr_[result] = 1;
        compositeCache_[key] = result;
        textureCache.emplace(key, std::move(entry));
        return result;
    }
    std::weak_ptr<int> model(uint32_t id, VkTexture* texture = nullptr) {
        M2ModelGPU model;
        model.vertexBuffer = uint64_t(id)*10 + 1; model.vertexAlloc = model.vertexBuffer + 100000;
        model.indexBuffer = uint64_t(id)*10 + 2; model.indexAlloc = model.indexBuffer + 100000;
        model.data = std::make_shared<int>(id);
        std::weak_ptr<int> weak = model.data;
        if (texture) model.textureIds.push_back(texture);
        models.emplace(id, std::move(model));
        return weak;
    }
};
'''

cases = r'''
int main() {
    FakeVkContext ctx;
    CharacterRenderer r{&ctx};
    auto* primary = r.texture("primary",1);
    auto* preparing = r.texture("preparing",2);
    auto* group = r.texture("group",3);
    auto* slot = r.texture("slot",4);
    auto* orphan = r.texture("orphan",5,6);
    auto* cacheOnly = r.texture("cache-only",7,8);
    r.texture("normal-pending",9,10,true);
    auto activeCpu = r.model(1,primary);
    auto preparingCpu = r.model(2,preparing);
    auto orphanCpu = r.model(3,orphan);
    CharacterRenderer::CharacterInstance instance;
    instance.modelId = 1; instance.groupTextureOverrides[4] = group; instance.textureSlotOverrides[7] = slot;
    r.instances.emplace(42,std::move(instance));
    r.reclaimUnusedResources({2});
    assert(r.models.size()==2 && r.models.count(1) && r.models.count(2));
    assert(orphanCpu.expired() && !activeCpu.expired() && !preparingCpu.expired());
    assert(r.textureCache.size()==5 && r.textureCacheBytes_==550);
    assert(r.textureCache.count("primary") && r.textureCache.count("preparing"));
    assert(r.textureCache.count("group") && r.textureCache.count("slot") && r.textureCache.count("normal-pending"));
    assert(!r.texturePropsByPtr_.count(orphan) && !r.normalMapByTexPtr_.count(orphan));
    assert(!r.texturePropsByPtr_.count(cacheOnly) && !r.normalMapByTexPtr_.count(cacheOnly));
    assert(!r.compositeCache_.count("orphan") && !r.compositeCache_.count("cache-only"));
    assert(buffersDestroyed.empty() && texturesDestroyed.empty() && textureOwnersDestroyed.empty());
    ctx.finish(1);
    assert(buffersDestroyed.empty() && texturesDestroyed.empty() && textureOwnersDestroyed.empty());
    ctx.finish(0);
    assert(buffersDestroyed[31]==1 && buffersDestroyed[32]==1);
    for (uint64_t id : {5,6,7,8}) {
        assert(texturesDestroyed[id]==1 && textureOwnersDestroyed[id]==1);
    }
    for (uint64_t id : {1,2,3,4,9,10}) assert(!texturesDestroyed.count(id));
    std::cout << "PASS character retirement: active/preparing models and primary/group/slot textures retained; CPU released before both GPU fences\n";

    r.instances.clear(); r.textureCache.at("normal-pending").normalMapPending = false;
    r.reclaimUnusedResources();
    assert(r.models.empty() && r.textureCache.empty() && r.textureCacheBytes_==0);
    assert(activeCpu.expired() && preparingCpu.expired());
    assert(r.texturePropsByPtr_.empty() && r.normalMapByTexPtr_.empty() && r.compositeCache_.empty());
    assert(!texturesDestroyed.count(9)); ctx.finishBoth();
    for (uint64_t id : {1,2,3,4,9,10}) assert(texturesDestroyed[id]==1 && textureOwnersDestroyed[id]==1);
    std::cout << "PASS character retirement: pending normal-map entries wait; orphan images use explicit GPU destroy\n";

    for (uint32_t id=100; id<200; ++id) {
        uint64_t textureId = uint64_t(id)*100;
        auto* texture = r.texture("travelling",textureId,textureId+1);
        auto cpu = r.model(id,texture);
        r.reclaimUnusedResources();
        assert(cpu.expired() && r.models.empty() && r.textureCache.empty() && r.textureCacheBytes_==0);
        assert(!texturesDestroyed.count(textureId)); ctx.finish(0);
        assert(!texturesDestroyed.count(textureId)); ctx.finish(1);
        assert(texturesDestroyed[textureId]==1 && texturesDestroyed[textureId+1]==1);
        assert(textureOwnersDestroyed[textureId]==1 && textureOwnersDestroyed[textureId+1]==1);
        assert(buffersDestroyed[uint64_t(id)*10+1]==1 && buffersDestroyed[uint64_t(id)*10+2]==1);
        assert(ctx.queues[0].empty() && ctx.queues[1].empty());
    }
    std::cout << "PASS character retirement: 100 cycles return CPU, GPU, texture accounting and fence queues to baseline\n";

    // Failure registering texture retirement must leave ownership and indexes unchanged.
    auto* failureTexture = r.texture("defer-failure",30001,30002);
    ctx.throwOnDefer = ctx.deferCalls+1;
    try { r.reclaimUnusedResources(); assert(false); } catch (const std::bad_alloc&) {}
    assert(r.textureCache.at("defer-failure").texture.get()==failureTexture && r.textureCacheBytes_==150);
    assert(r.texturePropsByPtr_.count(failureTexture) && r.normalMapByTexPtr_.count(failureTexture));
    assert(r.compositeCache_.at("defer-failure")==failureTexture);
    assert(!texturesDestroyed.count(30001) && !textureOwnersDestroyed.count(30001));
    assert(ctx.queues[0].empty() && ctx.queues[1].empty());
    ctx.throwOnDefer = 0; r.reclaimUnusedResources(); ctx.finishBoth();
    assert(texturesDestroyed[30001]==1 && texturesDestroyed[30002]==1 && r.textureCacheBytes_==0);

    auto failedCpu = r.model(300);
    ctx.throwOnDefer = ctx.deferCalls+1;
    try { r.reclaimUnusedResources(); assert(false); } catch (const std::bad_alloc&) {}
    assert(r.models.count(300) && !failedCpu.expired());
    assert(r.models.at(300).vertexBuffer==3001 && r.models.at(300).indexBuffer==3002);
    assert(!buffersDestroyed.count(3001) && ctx.queues[0].empty() && ctx.queues[1].empty());
    ctx.throwOnDefer = 0; r.reclaimUnusedResources();
    assert(failedCpu.expired()); ctx.finishBoth();
    assert(buffersDestroyed[3001]==1 && buffersDestroyed[3002]==1);

    // A partial pass may retire the model, then fail before transferring the texture.
    auto* partialTexture = r.texture("partial",40001,40002);
    auto partialCpu = r.model(400,partialTexture);
    ctx.throwOnDefer = ctx.deferCalls+2;
    try { r.reclaimUnusedResources(); assert(false); } catch (const std::bad_alloc&) {}
    assert(!r.models.count(400) && partialCpu.expired());
    assert(r.textureCache.at("partial").texture.get()==partialTexture && r.textureCacheBytes_==150);
    assert(r.compositeCache_.at("partial")==partialTexture);
    assert(!texturesDestroyed.count(40001) && !textureOwnersDestroyed.count(40001));
    ctx.finishBoth(); assert(buffersDestroyed[4001]==1 && buffersDestroyed[4002]==1);
    assert(!texturesDestroyed.count(40001));
    ctx.throwOnDefer = 0; r.reclaimUnusedResources(); ctx.finishBoth();
    assert(texturesDestroyed[40001]==1 && texturesDestroyed[40002]==1);
    assert(r.models.empty() && r.textureCache.empty() && r.textureCacheBytes_==0);
    std::cout << "PASS character retirement: throwing defer preserves model/texture ownership; partial retirement safely retries\n";
}
'''

with tempfile.TemporaryDirectory(prefix="wowps-character-residency-") as tmp:
    unit = Path(tmp) / "residency.cpp"
    binary = Path(tmp) / "residency"
    unit.write_text(fixture + functions + cases)
    command = shlex.split(os.environ.get("CXX", "g++"))
    command += ["-std=c++20", "-O1", "-g", "-Wall", "-Wextra", "-Wno-missing-field-initializers", "-I", str(ROOT / "include")]
    if os.environ.get("SANITIZE") == "1":
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command + [str(unit), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
