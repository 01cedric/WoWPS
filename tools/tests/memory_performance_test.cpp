#include "pipeline/byte_lru_cache.hpp"
#include "addons/interface_source.hpp"
#include "addons/lua_memory_budget.hpp"
#include "core/snapshot_writer.hpp"
#include "addons/local_framexml_lua.hpp"
#include "rendering/ps4_world_budget.hpp"
#include "rendering/shadow_ranges.hpp"
#include "rendering/shadow_instances.hpp"
#include "ui/unit_portrait.hpp"
#include "rendering/portrait_camera.hpp"
#include "ui/texture_content_bounds.hpp"
#include <set>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include <cassert>
#include <future>
#include <iostream>
#include <limits>

using wowee::pipeline::ByteLruCache;
static auto bytes(size_t n, uint8_t value) {
    return std::make_shared<const ByteLruCache::Bytes>(n, value);
}
static void testCache() {
    ByteLruCache cache;
    assert(cache.put("hot", bytes(4, 1), 8));
    assert(cache.put("cold", bytes(4, 2), 8));
    auto retained = cache.get("hot");
    assert(cache.put("new", bytes(4, 3), 8));
    assert(!cache.get("cold") && cache.get("hot"));
    assert(cache.bytes() == 8);
    assert(!cache.put("oversize", bytes(16, 4), 8));
    assert(!cache.put("hot", bytes(4, 5), 8));
    assert((*cache.get("hot"))[0] == 1);
    assert(cache.trim(4) == 4 && cache.get("hot"));
    assert(cache.trim(0) == 4 && cache.bytes() == 0);
    assert((*retained)[0] == 1); // reader survives concurrent-style eviction
    for (int i = 0; i < 10000; ++i) {
        cache.put(std::to_string(i), bytes(32, 7), 256);
        assert(cache.bytes() <= 256);
    }
    cache.clear(); assert(cache.bytes() == 0);
    std::cout << "PASS cache: recency, eviction, budget, duplicate, reader lifetime\n";
}
static void testSources() {
    wowee::addons::InterfaceSource source;
    unsigned reads = 0;
    source.setArchive([&](const std::string&) {
        ++reads;
        return reads == 1 ? std::vector<uint8_t>{} : std::vector<uint8_t>{'o','k'};
    }, [](const std::string&) { return true; }, {});
    assert(!source.read("mpq/Interface/FrameXML/test.lua"));
    assert(source.read("mpq/Interface/FrameXML/test.lua").value() == "ok");
    assert(source.read("mpq/interface/framexml/TEST.lua").value() == "ok");
    assert(reads == 2);
    assert(source.releaseSourceCache() == 2);
    assert(source.read("mpq/Interface/FrameXML/test.lua").value() == "ok");
    assert(source.read("mpq/Interface/FrameXML/test.lua").value() == "ok");
    assert(reads == 4); // cache remains suspended after memory pressure
    std::cout << "PASS interface: failed read retry, normalized hit, pressure release\n";
}
static void testLuaBudget() {
    using Budget = wowee::addons::LuaMemoryBudget;
    Budget small; small.limit = 128;
    auto* p = static_cast<uint8_t*>(Budget::allocate(&small, nullptr, 12345, 100));
    assert(p && small.used == 100); p[0] = 42;
    assert(!Budget::allocate(&small, p, 100, 129) && p[0] == 42 && small.used == 100);
    assert(!Budget::allocate(&small, nullptr, 0, std::numeric_limits<size_t>::max()));
    p = static_cast<uint8_t*>(Budget::allocate(&small, p, 100, 50));
    assert(p && p[0] == 42 && small.used == 50);
    Budget::allocate(&small, p, 50, 0); assert(small.used == 0 && small.peak == 100);
    Budget vm; vm.limit = 512 * 1024;
    lua_State* L = lua_newstate(&Budget::allocate, &vm);
    assert(L); luaL_openlibs(L);
    assert(luaL_dostring(L, "return string.rep('x', 1000)") == 0);
    lua_settop(L, 0);
    assert(luaL_loadstring(L, "return string.rep('x', 2000000)") == 0);
    assert(lua_pcall(L, 0, LUA_MULTRET, 0) == LUA_ERRMEM);
    lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT, 0);
    assert(vm.failures > 0 && vm.used <= vm.limit);
    assert(luaL_dostring(L, "return 1+2") == 0 && lua_tonumber(L, -1) == 3);
    lua_close(L); assert(vm.used == 0);
    std::cout << "PASS Lua: limit, overflow, failed realloc preservation, real VM recovery/free\n";
}
static void testAutosave() {
    std::promise<void> started, release;
    auto gate = release.get_future().share();
    std::vector<unsigned> written;
    {
        wowee::core::SnapshotWriter writer([&](const std::string&, const std::vector<uint8_t>& data) {
            if (data[0] == 1) { started.set_value(); gate.wait(); }
            written.push_back(data[0]);
            return data[0] != 4;
        });
        writer.submit("test", {1}); started.get_future().wait();
        writer.submit("test", {2}); writer.submit("test", {3});
        release.set_value(); writer.flush();
        assert((written == std::vector<unsigned>{1, 3}));
        assert(!writer.takeFailure());
        writer.submit("test", {4}); writer.flush();
        assert(writer.takeFailure() && !writer.takeFailure());
        writer.submit("test", {5}); // destructor must drain, not discard
    }
    assert((written == std::vector<unsigned>{1, 3, 4, 5}));
    std::cout << "PASS autosave: bounded coalescing, flush ordering, errors, shutdown drain\n";
}

static void testTerrainTravel() {
    using namespace wowee::rendering::ps4budget;
    // ADT axes are swapped relative to render X/Y. Test both independently.
    assert(tileDistanceSquared(32, 31, 100.f, -100.f) == 0.f);
    assert(tileDistanceSquared(31, 32, 100.f, -100.f) > 0.f);
    assert(tileInRange(31, 31, -.1f, -.1f, 420.f, false)); // diagonal seam
    assert(!tileInRange(35, 32, -266.f, -266.f, 533.f, true));
    // 64-unit hysteresis: already loaded tiles survive small reversals but
    // do not remain resident after a long journey or a camera cut.
    assert(!tileInRange(32, 32, 500.f, -100.f, 420.f, false));
    assert(tileInRange(32, 32, 500.f, -100.f, 420.f, true));
    assert(!tileInRange(32, 32, 600.f, -100.f, 420.f, true));
    std::set<std::pair<int,int>> retained;
    for (int step=0; step<2000; ++step) {
        const float x=-15000.f+step*15.f, y=-100.f;
        for (auto it=retained.begin(); it!=retained.end();) {
            if (!tileInRange(it->first,it->second,x,y,420.f,true)) it=retained.erase(it);
            else ++it;
        }
        const int cx=int(std::floor(32.f-y/(1600.f/3.f)));
        const int cy=int(std::floor(32.f-x/(1600.f/3.f)));
        for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy)
            if(cx+dx>=0 && cx+dx<64 && cy+dy>=0 && cy+dy<64 &&
               tileInRange(cx+dx,cy+dy,x,y,420.f,false)) retained.emplace(cx+dx,cy+dy);
        assert(retained.size()<=10);
    }
    std::cout << "PASS terrain: ADT axes, diagonal seam, hysteresis, bounded 30km journey\n";
}
static void testPortraitFraming() {
    wowee::ui::PortraitModel orc;
    orc.boundMaxZ=2.6f;orc.headZ=1.5f;orc.headForward=.3f;
    const auto face=wowee::ui::portraitFraming(orc);
    assert(face.source==wowee::ui::PortraitFraming::Source::HeadAttachment);
    assert(face.focusZ>orc.headZ && face.focusZ<2.f && face.distance>0.f);
    auto upright = orc; upright.headForward = 0;
    const auto centered = wowee::ui::portraitFraming(upright);
    assert(std::abs((face.distance - orc.headForward) - centered.distance) < .0001f);
    orc.hasPortraitCamera=true;orc.cameraTargetZ=1.65f;orc.cameraDistance=1.2f;orc.cameraFovRadians=.6f;
    const auto authored=wowee::ui::portraitFraming(orc);
    assert(authored.focusZ==1.65f && authored.source==wowee::ui::PortraitFraming::Source::PortraitCamera);
    // Two offset/pitched authored cameras must retain their optical relation
    // to the model, independently of race orientation and world translation.
    for(const glm::vec3 target : {glm::vec3(.4f,.1f,1.8f),glm::vec3(-.3f,0,1.9f)}) {
        const auto eye=target+glm::vec3(.8f,.2f,.15f);
        for(float yaw : {0.f,90.f,180.f,270.f}) {
            const glm::vec3 origin(10,20,30);
            const auto pose=wowee::rendering::placePortraitCamera(eye,target,origin,yaw);
            const auto inv=glm::inverse(glm::translate(glm::mat4(1),origin)*glm::rotate(glm::mat4(1),glm::radians(yaw),glm::vec3(0,0,1)));
            assert(glm::length(glm::vec3(inv*glm::vec4(pose.eye,1))-eye)<.0001f);
            assert(glm::length(glm::vec3(inv*glm::vec4(pose.target,1))-target)<.0001f);
            const auto view=glm::lookAt(pose.eye,pose.target,glm::vec3(0,0,1));
            const auto at=view*glm::vec4(pose.target,1);assert(std::abs(at.x)<.0001f&&std::abs(at.y)<.0001f);
        }
    }
    std::cout << "PASS portraits: hunched head attachment and authored camera\n";
}
static void testLocalInterface() {
    lua_State* L=luaL_newstate();assert(L);luaL_openlibs(L);
    const auto run=[&](const char* code) {
        if(luaL_dostring(L,code)!=0){std::cerr<<lua_tostring(L,-1)<<'\n';std::abort();}
    };
    run(R"lua(
      function __WoWPSLocalCommand() return true end
      UnitName=function()return 'Player' end
      CURRENT_MAP_QUESTS=nil
      function WatchFrame_Update() watchUpdates=(watchUpdates or 0)+1 end
      WatchFrame={};BACKPACK_HEIGHT=240
      ContainerFrame_GenerateFrame=nil;WatchFrame_GetCurrentMapQuests=nil
      function updateContainerFrameAnchors() anchorUpdates=(anchorUpdates or 0)+1 end
      local methods={}
      function methods:GetName()return self.name end
      function methods:SetHeight(v)self.height=v end
      function methods:SetWidth(v)self.width=v end
      function methods:SetTexture(v)self.texture=v end
      function methods:SetTexCoord(...)self.uv={...} end
      function methods:ClearAllPoints()self.point=nil end
      function methods:SetPoint(...)self.point={...} end
      function methods:Show()self.shown=true end
      function methods:Hide()self.shown=false end
      local function widget(name)local w=setmetatable({name=name},{__index=methods});_G[name]=w;return w end
      function methods:CreateTexture(name)return widget(name) end
      testBag=widget('ContainerFrame1')
      for _,suffix in ipairs({'BackgroundTop','BackgroundMiddle1','BackgroundMiddle2','BackgroundBottom','MoneyFrame','Item1'})do widget('ContainerFrame1'..suffix)end
      __WoWPSLocal={name='Orc',log={11},quests={[11]={id=11,title='Trial',level=1,active=true,
        complete=false,objectives={{text='Boar',done=0,count=4,type='monster'}}}},bags={{id=9,count=2,equip=0}},spells={}}
    )lua");
    run(wowee::addons::kLocalFrameXmlLua);
    run(R"lua(
      -- Match real startup: retail globals appear AFTER the local API bridge.
      CURRENT_MAP_QUESTS={}
      function WatchFrame_GetCurrentMapQuests() CURRENT_MAP_QUESTS={} end
      function ContainerFrame_GenerateFrame(frame,size,id)
        bagCalls=(bagCalls or 0)+1;frame.size=size;frame:SetHeight(240)
      end
      assert(WoWPS_InstallLocalFrameXmlHooks())
      local installedBag=ContainerFrame_GenerateFrame
      assert(WoWPS_InstallLocalFrameXmlHooks() and ContainerFrame_GenerateFrame==installedBag)
      WatchFrame_GetCurrentMapQuests();assert(CURRENT_MAP_QUESTS[11]==1)
      local title,level,tag,group,header,collapsed,complete,daily,id=GetQuestLogTitle(1)
      assert(title=='Trial' and group==0 and header==false and complete==nil and id==11)
      assert(GetNumQuestWatches()==1 and GetQuestIndexForWatch(1)==1 and GetQuestWatchIndex(1)==1)
      assert(CURRENT_MAP_QUESTS[11]==1)
      local t,kind,done=GetQuestLogLeaderBoard(1,1);assert(t=='Boar: 0 / 4' and not done)
      __WoWPSLocal.quests[11].objectives[1].done=4;__WoWPSLocal.quests[11].complete=true
      WoWPS_RefreshLocalQuestTracking()
      assert(select(7,GetQuestLogTitle(1))==1 and select(3,GetQuestLogLeaderBoard(1,1))==true)
      assert(GetQuestLogCompletionText(1):find('quest giver'))
      RemoveQuestWatch(1);WoWPS_RefreshLocalQuestTracking();assert(GetNumQuestWatches()==0)
      AddQuestWatch(1);assert(GetNumQuestWatches()==1 and watchUpdates==2)
      __WoWPSLocal.log={};WoWPS_RefreshLocalQuestTracking();assert(GetNumQuestWatches()==0 and not CURRENT_MAP_QUESTS[11])
      __WoWPSLocal.log={11};WoWPS_RefreshLocalQuestTracking();assert(GetNumQuestWatches()==1)
      ContainerFrame_GenerateFrame(testBag,24,0)
      assert(testBag.height==322 and ContainerFrame1Item1.point[5]==-290 and bagCalls==1)
      assert(ContainerFrame1BackgroundTop.height==48 and ContainerFrame1BackgroundBottom.height==44)
      assert(#testBag.wowpsBackpackRows==6 and testBag.wowpsBackpackRows[1].height==41 and anchorUpdates==1)
      assert(ContainerFrame1MoneyFrame.point[5]==-298)
      assert(GetContainerNumSlots(0)==24 and GetContainerNumFreeSlots(0)==23)
      assert(GetContainerItemID(0,1)==9 and select(2,GetContainerItemInfo(0,1))==2)
      -- Online mode is delegated, and does not get the local backpack resize.
      __WoWPSLocal=nil;ContainerFrame_GenerateFrame(testBag,16,0);assert(testBag.height==240)
      assert(not testBag.wowpsBackpackRows[1].shown and ContainerFrame1MoneyFrame.point[5]==-216)
    )lua");
    if (const char* retail=std::getenv("WOWPS_RETAIL_FRAME_XML_DIR")) {
        lua_pushstring(L,retail);lua_setglobal(L,"retailRoot");
        const char* fixture=std::getenv("WOWPS_RETAIL_TEST");assert(fixture);
        if(luaL_dofile(L,fixture)!=0){std::cerr<<lua_tostring(L,-1)<<'\n';std::abort();}
    }
    lua_close(L);
    std::cout<<"PASS local Lua 5.1: quest ABI, automatic/manual watch, progress, completion, abandon/reaccept, 24-slot backpack and online delegation\n";
}
static void testTextureContent() {
    using namespace wowee::ui;
    std::vector<uint8_t> rgba(32*64*4,0);
    assert(!textureContentBounds(rgba,32,64).valid);
    for(unsigned y=26;y<59;++y)for(unsigned x=2;x<30;++x)rgba[(y*32+x)*4+3]=255;
    const auto bounds=textureContentBounds(rgba,32,64);
    assert(bounds.valid && bounds.left==2.f/32 && bounds.right==30.f/32);
    assert(bounds.top==26.f/64 && bounds.bottom==59.f/64);
    float a,b;assert(contentAxis(0,1,bounds.top,bounds.bottom,a,b) && a==26.f/64 && b==59.f/64);
    assert(contentAxis(1,0,bounds.top,bounds.bottom,a,b) && a==5.f/64 && b==38.f/64);
    assert(contentAxis(.5f,1,bounds.top,bounds.bottom,a,b) && a==0 && b==54.f/64);
    assert(!contentAxis(0,.1f,bounds.top,bounds.bottom,a,b));
    assert(!textureContentBounds(rgba,UINT32_MAX,UINT32_MAX).valid);
    std::cout<<"PASS UI focus: transparent padding, cropped/reversed UVs, empty/malformed images\n";
}
static void testShadowRanges() {
    using namespace wowee::rendering;
    const auto triangles=[](const auto& ranges) {
        std::set<uint64_t> result;
        for(auto r:ranges)for(uint64_t i=r.firstIndex;i+2<uint64_t(r.firstIndex)+r.indexCount;i+=3)result.insert(i);
        return result;
    };
    for(uint32_t seed=1;seed<1000;++seed) {
        std::vector<ShadowRange> ranges;
        uint32_t x=seed;
        for(int i=0;i<80;++i){x=x*1664525u+1013904223u;ranges.push_back({(x%160)*3,((x>>16)%15)*3});}
        const auto before=triangles(ranges);const auto count=ranges.size();
        coalesceShadowRanges(ranges);assert(triangles(ranges)==before && ranges.size()<=count);
    }
    std::vector<ShadowRange> gaps{{0,6},{9,3},{1,3},{UINT32_MAX-5,6}};
    const auto before=triangles(gaps);coalesceShadowRanges(gaps);assert(triangles(gaps)==before);
    std::cout<<"PASS shadow batching: triangle coverage, overlaps, gaps, alignment, overflow\n";
}
static void testShadowInstances() {
    using namespace wowee::rendering;
    struct Caster { uint32_t model, id; };
    // Membership must survive grouping exactly, including repeated models and
    // sparse ranges: the draw expansion is (model, instance ID, triangle).
    for (uint32_t seed = 1; seed < 500; ++seed) {
        std::vector<Caster> casters;
        for (uint32_t i = 0; i < seed % 193 + 1; ++i)
            casters.push_back({(i * 17u + seed * 19u) % 23u, i});
        std::sort(casters.begin(), casters.end(), [](auto a, auto b) { return a.model < b.model; });
        std::vector<uint32_t> seen(casters.size());
        for (size_t begin = 0; begin < casters.size();) {
            size_t end = shadowInstanceGroupEnd(begin, casters.size(), [&](size_t i) { return casters[i].model; });
            assert(end > begin && end <= casters.size());
            for (size_t i = begin; i < end; ++i) {
                assert(casters[i].model == casters[begin].model);
                assert(++seen[casters[i].id] == 1);
            }
            if (end < casters.size()) assert(casters[end].model != casters[begin].model);
            begin = end;
        }
        for (auto count : seen) assert(count == 1);
    }
    assert(shadowInstanceStorageFits(32768,32768));
    assert(!shadowInstanceStorageFits(32769,32768));
    assert(!shadowInstanceStorageFits(0,32768));
    assert(!shadowInstanceStorageFits(SIZE_MAX,32768));
    bool inspectedEmpty=false;
    assert(shadowInstanceGroupEnd(0,0,[&](size_t){inspectedEmpty=true;return 0;})==0);
    assert(!inspectedEmpty);
    std::cout << "PASS shadow instancing: exact caster membership, complete model groups, bounded storage and empty/overflow fallback\n";
}
int main() { testCache(); testSources(); testLuaBudget(); testAutosave(); testTerrainTravel(); testPortraitFraming(); testLocalInterface(); testTextureContent(); testShadowRanges(); testShadowInstances(); }
