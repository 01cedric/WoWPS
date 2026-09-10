#include "game/local_world_catalog.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <queue>
#include <stdexcept>
#include <tuple>

namespace wowee::game {
namespace {
using Json = nlohmann::json;
uint32_t u32(const unsigned char* p) { return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24; }
uint64_t u64(const unsigned char* p) { return u32(p) | uint64_t(u32(p+4))<<32; }
int32_t i32(const unsigned char* p) { const uint32_t v=u32(p); int32_t result; std::memcpy(&result,&v,4);return result; }
float f32(const unsigned char* p) { const uint32_t v=u32(p);float result;std::memcpy(&result,&v,4);return result; }
uint32_t number(const Json& j,const char* key,uint32_t fallback=0,uint32_t maximum=1000000000) {
    if(!j.contains(key))return fallback;
    const auto& v=j.at(key);
    if(!v.is_number_integer() || v.get<int64_t>()<0 || v.get<uint64_t>()>maximum)throw std::runtime_error(std::string("Catalog number: ")+key);
    return v.get<uint32_t>();
}
float real(const Json& j,const char* key,float fallback=0,float maximum=200000) {
    if(!j.contains(key))return fallback;
    if(!j.at(key).is_number())throw std::runtime_error(std::string("Catalog float: ")+key);
    const float v=j.at(key).get<float>();
    if(!std::isfinite(v)||std::abs(v)>maximum)throw std::runtime_error(std::string("Catalog coordinate: ")+key);
    return v;
}
std::string label(const Json& j,const char* key,size_t max,bool empty=false) {
    if(!j.contains(key)||!j.at(key).is_string())throw std::runtime_error(std::string("Catalog string: ")+key);
    auto v=j.at(key).get<std::string>();if((!empty&&v.empty())||v.size()>max||v.find('\0')!=std::string::npos)throw std::runtime_error(std::string("Catalog string length: ")+key);return v;
}
const Json& array(const Json& j,const char* key,size_t max) {
    const auto& v=j.at(key);if(!v.is_array()||v.size()>max)throw std::runtime_error(std::string("Catalog array: ")+key);return v;
}
struct File {
    static constexpr size_t PageBytes = 4096, Ways = 4;
    struct Page {
        uint64_t number = UINT64_MAX, age = 0;
        std::array<unsigned char, PageBytes> data{};
    };
    mutable std::ifstream stream;
    uint64_t bytes=0;
    std::string path;
    size_t cachePages = 16;
    mutable std::vector<Page> pages;
    mutable uint64_t clock = 0, diskReads = 0, cacheHits = 0;
    void open(const std::string& p) {
        path=p;stream.open(p,std::ios::binary);if(!stream)throw std::runtime_error("Cannot open catalog file: "+p);
        stream.seekg(0,std::ios::end);const auto end=stream.tellg();
        if(end<0||uint64_t(end)>512ULL*1024*1024)throw std::runtime_error("Invalid catalog file size: "+p);
        bytes=uint64_t(end);
    }
    void diskRead(uint64_t offset, void* output, size_t count) const {
        stream.clear();stream.seekg(std::streamoff(offset),std::ios::beg);
        stream.read(static_cast<char*>(output),std::streamsize(count));
        if(!stream)throw std::runtime_error("Catalog read failed: "+path);
        ++diskReads;
    }
    void read(uint64_t offset,void* output,size_t count) const {
        if(offset>bytes||count>bytes-offset)throw std::runtime_error("Truncated catalog file: "+path);
        if (!count) return;
        // Large one-off manifest reads must not evict the hot random-access
        // pages. Page data is bounded to 768 KiB per catalog, plus small metadata.
        if (count > 32768) { diskRead(offset, output, count); return; }
        if (pages.empty()) pages.resize(cachePages);
        auto* dst = static_cast<unsigned char*>(output);
        while (count) {
            const uint64_t pageNumber = offset / PageBytes;
            const size_t begin = size_t(pageNumber % (pages.size() / Ways)) * Ways;
            Page* hit = nullptr; Page* victim = &pages[begin];
            for (size_t i=begin; i<begin+Ways; ++i) {
                auto& page=pages[i];
                if (page.number == pageNumber) {hit=&page;break;}
                if (page.age < victim->age) victim=&page;
            }
            if (hit) ++cacheHits;
            else {
                hit=victim;
                const uint64_t pageOffset=pageNumber*PageBytes;
                // Publish the key only after a successful read.
                hit->number=UINT64_MAX;
                diskRead(pageOffset,hit->data.data(),size_t(std::min<uint64_t>(PageBytes,bytes-pageOffset)));
                hit->number=pageNumber;
            }
            hit->age=++clock;
            const size_t within=size_t(offset % PageBytes);
            const size_t take=std::min(count,PageBytes-within);
            std::memcpy(dst,hit->data.data()+within,take);
            dst+=take;offset+=take;count-=take;
        }
    }
};
struct Records : File {
    uint32_t count=0,width=0;
    void open(const std::string& path,uint32_t expectedWidth) {
        File::open(path);std::array<unsigned char,16> header{};read(0,header.data(),header.size());
        if(std::memcmp(header.data(),"WPCAT01\0",8))throw std::runtime_error("Catalog header/version: "+path);
        count=u32(header.data()+8);width=u32(header.data()+12);
        if(count>1000000||width!=expectedWidth||16+uint64_t(count)*width>bytes)throw std::runtime_error("Catalog index bounds: "+path);
    }
    bool get(uint32_t id,Json& result) const {
        uint32_t lo=0,hi=count;std::array<unsigned char,16> row{};
        while(lo<hi){const uint32_t mid=lo+(hi-lo)/2;read(16+uint64_t(mid)*16,row.data(),16);
            if(u32(row.data())<id)lo=mid+1;else hi=mid;}
        if(lo>=count)return false;
        read(16+uint64_t(lo)*16,row.data(),16);if(u32(row.data())!=id)return false;
        const uint64_t offset=u64(row.data()+4);const uint32_t size=u32(row.data()+12);
        if(offset<16+uint64_t(count)*16||!size||size>LocalWorldCatalog::MaxRecordBytes)throw std::runtime_error("Catalog record bounds: "+path);
        std::string text(size,'\0');read(offset,text.data(),size);result=Json::parse(text);return true;
    }
};
LocalItemStack stack(const Json& j) {
    LocalItemStack s;s.itemId=number(j,"itemId",0,UINT32_MAX);s.count=uint16_t(number(j,"count",1,65535));
    if(!s.itemId||!s.count)throw std::runtime_error("Catalog zero item stack");
    return s;
}
}
struct LocalWorldCatalog::Impl {
    Records cells,npcs,items,quests,contacts;
    File spawns;
    uint32_t fingerprint=0;
    bool loaded=false;
    std::vector<LocalCatalogStart> starts;
    std::vector<LocalCatalogMap> maps;
    std::vector<LocalCatalogDestination> destinations;
    mutable size_t lastRead=0;
    bool cell(uint32_t map,int32_t x,int32_t y,uint64_t& offset,uint32_t& count) const {
        const auto wanted=std::make_tuple(map,x,y);uint32_t lo=0,hi=cells.count;
        std::array<unsigned char,24> row{};
        while(lo<hi){uint32_t mid=lo+(hi-lo)/2;cells.read(16+uint64_t(mid)*24,row.data(),24);
            const auto key=std::make_tuple(u32(row.data()),i32(row.data()+4),i32(row.data()+8));
            if(key<wanted)lo=mid+1;else hi=mid;}
        if(lo>=cells.count)return false;
        cells.read(16+uint64_t(lo)*24,row.data(),24);
        if(std::make_tuple(u32(row.data()),i32(row.data()+4),i32(row.data()+8))!=wanted)return false;
        offset=u64(row.data()+12);count=u32(row.data()+20);
        // A malformed cell can never cause unbounded work or heap allocation.
        if(count>65536||offset%28||offset>spawns.bytes||uint64_t(count)*28>spawns.bytes-offset)throw std::runtime_error("Catalog cell range");
        return true;
    }
};
LocalWorldCatalog::LocalWorldCatalog():impl_(std::make_unique<Impl>()){}
LocalWorldCatalog::~LocalWorldCatalog()=default;
bool LocalWorldCatalog::load(const std::string& directory,std::string& error) {
    error.clear();
    try {
        auto next=std::make_unique<Impl>();File manifest;manifest.open(directory+"/manifest.json");
        if(!manifest.bytes||manifest.bytes>2*1024*1024)throw std::runtime_error("Catalog manifest size");
        std::string text(size_t(manifest.bytes),'\0');manifest.read(0,text.data(),text.size());const auto j=Json::parse(text);
        if(number(j,"schemaVersion")!=1||number(j,"cellSize")!=256)throw std::runtime_error("Catalog manifest version");
        next->fingerprint=number(j,"fingerprint",0,UINT32_MAX);if(!next->fingerprint)throw std::runtime_error("Catalog fingerprint missing");
        next->cells.cachePages=64;next->spawns.cachePages=64;
        next->cells.open(directory+"/cells.idx",24);next->spawns.open(directory+"/spawns.pack");
        if(next->spawns.bytes%28)throw std::runtime_error("Catalog spawn alignment");
        next->npcs.open(directory+"/npcs.pack",16);next->items.open(directory+"/items.pack",16);
        next->quests.open(directory+"/quests.pack",16);next->contacts.open(directory+"/contacts.pack",16);
        // Size metadata catches partial package copies without reading the entire world.
        for(const File* f:{static_cast<File*>(&next->cells),&next->spawns,static_cast<File*>(&next->npcs),static_cast<File*>(&next->items),static_cast<File*>(&next->quests),static_cast<File*>(&next->contacts)}) {
            const auto name=f->path.substr(f->path.find_last_of('/')+1);
            if(!j.at("files").contains(name)||j.at("files").at(name).at("bytes").get<uint64_t>()!=f->bytes)throw std::runtime_error("Catalog file size differs from manifest: "+name);
        }
        for(const auto& s:array(j,"starts",128)) {
            LocalCatalogStart r;r.race=uint8_t(number(s,"race",0,11));r.classId=uint8_t(number(s,"classId",0,11));r.level=uint8_t(number(s,"level",1,80));r.mapId=number(s,"mapId",0,65535);
            if(!r.race||!r.classId||!r.level)throw std::runtime_error("Catalog invalid profile");
            r.x=real(s,"x");r.y=real(s,"y");r.z=real(s,"z");r.orientation=real(s,"orientation",0,100);
            next->starts.push_back(r);
        }
        for(const auto& m:array(j,"maps",4096)) {
            LocalCatalogMap r;r.id=number(m,"id",0,65535);r.spawnCount=number(m,"spawnCount");r.instanceMap=m.value("instanceMap",false);r.upstreamScript=label(m,"upstreamScript",128,true);next->maps.push_back(std::move(r));
        }
        for(const auto& d:array(j,"destinations",4096)) {
            LocalCatalogDestination r;r.id=number(d,"id",0,UINT32_MAX);r.mapId=number(d,"mapId",0,65535);r.name=label(d,"name",128,true);
            r.x=real(d,"x");r.y=real(d,"y");r.z=real(d,"z");r.orientation=real(d,"orientation",0,100);r.instanceMap=d.value("instanceMap",false);next->destinations.push_back(std::move(r));
        }
        next->loaded=true;impl_=std::move(next);return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool LocalWorldCatalog::query(uint32_t mapId,float x,float y,float radius,size_t limit,std::vector<LocalNpcSpawn>& result,std::string& error) const {
    return queryImpl(mapId,x,y,0,false,radius,limit,result,error);
}
bool LocalWorldCatalog::query3D(uint32_t mapId,float x,float y,float z,float radius,size_t limit,std::vector<LocalNpcSpawn>& result,std::string& error) const {
    return queryImpl(mapId,x,y,z,true,radius,limit,result,error);
}
bool LocalWorldCatalog::queryImpl(uint32_t mapId,float x,float y,float z,bool useHeight,float radius,size_t limit,std::vector<LocalNpcSpawn>& result,std::string& error) const {
    error.clear();result.clear();impl_->lastRead=0;
    try {
        if(!impl_->loaded)throw std::runtime_error("World catalog not loaded");
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||!std::isfinite(radius)||std::abs(x)>200000||std::abs(y)>200000||std::abs(z)>200000||radius<0||radius>MaxRadius||limit>MaxResults)throw std::runtime_error("Invalid catalog query bounds");
        if(!limit)return true;
        struct Candidate {float distance;LocalNpcSpawn spawn;bool operator<(const Candidate& b)const{return distance<b.distance||(distance==b.distance&&spawn.id<b.spawn.id);}};
        std::priority_queue<Candidate> nearest;
        const int minX=int(std::floor((x-radius)/CellSize)),maxX=int(std::floor((x+radius)/CellSize));
        const int minY=int(std::floor((y-radius)/CellSize)),maxY=int(std::floor((y+radius)/CellSize));
        std::array<unsigned char,28*64> block{};
        for(int cy=minY;cy<=maxY;++cy)for(int cx=minX;cx<=maxX;++cx) {
            uint64_t offset=0;uint32_t count=0;if(!impl_->cell(mapId,cx,cy,offset,count))continue;
            for(uint32_t at=0;at<count;) {
                const uint32_t n=std::min(uint32_t(64),count-at);impl_->spawns.read(offset+uint64_t(at)*28,block.data(),size_t(n)*28);at+=n;impl_->lastRead+=n;
                for(uint32_t i=0;i<n;++i){const auto* row=block.data()+i*28;LocalNpcSpawn s;
                    s.id=u32(row);s.entry=u32(row+4);s.mapId=u32(row+8);s.x=f32(row+12);s.y=f32(row+16);s.z=f32(row+20);s.orientation=f32(row+24);
                    if(!s.id||!s.entry||s.mapId!=mapId||!std::isfinite(s.x)||!std::isfinite(s.y)||!std::isfinite(s.z)||!std::isfinite(s.orientation)||std::abs(s.x)>200000||std::abs(s.y)>200000||std::abs(s.z)>200000||int(std::floor(s.x/CellSize))!=cx||int(std::floor(s.y/CellSize))!=cy)throw std::runtime_error("Invalid catalog spawn record");
                    const float distance=(s.x-x)*(s.x-x)+(s.y-y)*(s.y-y)+(useHeight?(s.z-z)*(s.z-z):0.0f);if(distance>radius*radius)continue;
                    Candidate c{distance,s};if(nearest.size()<limit)nearest.push(c);else if(c<nearest.top()){nearest.pop();nearest.push(c);}
                }
            }
        }
        result.resize(nearest.size());for(size_t i=result.size();i>0;--i){result[i-1]=nearest.top().spawn;nearest.pop();}return true;
    }catch(const std::exception& e){result.clear();error=e.what();return false;}
}
bool LocalWorldCatalog::npc(uint32_t id,LocalNpcDefinition& result,std::string& error) const {
    error.clear();try {
        if(!impl_->loaded)throw std::runtime_error("World catalog not loaded");
        Json j;if(!impl_->npcs.get(id,j))return false;LocalNpcDefinition n;
        n.id=number(j,"id",0,UINT32_MAX);if(n.id!=id)throw std::runtime_error("Catalog NPC ID mismatch");n.name=label(j,"name",96);n.displayId=number(j,"displayId",0,UINT32_MAX);n.level=uint8_t(number(j,"level",1,83));
        n.health=number(j,"health",40);n.damage=number(j,"damage",4);n.armor=number(j,"armor");n.xp=number(j,"xp");n.money=number(j,"money");n.faction=number(j,"faction",0,UINT32_MAX);n.unitFlags=number(j,"unitFlags",0,UINT32_MAX);
        if(j.contains("gossipText"))n.gossipText=label(j,"gossipText",4096,true);
        if(j.contains("subname"))n.subname=label(j,"subname",128,true);
        n.hostile=j.value("hostile",false);n.questGiver=j.value("questGiver",false);n.respawnSeconds=real(j,"respawnSeconds",30,86400);n.aggroRadius=real(j,"aggroRadius",0,100);
        // creature_template.npcflag and npc_vendor, if this catalog carries
        // them. The shipped one does not, and localEffectiveNpcFlags() then
        // falls back to the transcription that ships beside the code; a catalog
        // that fills either in overrides that fallback for this creature.
        n.npcFlags=number(j,"npcFlags",0,UINT32_MAX);
        n.trainerSkill=uint16_t(number(j,"trainerSkill",0,65535));n.trainerClass=uint8_t(number(j,"trainerClass",0,11));
        if(j.contains("vendor")){
            for(const auto& id:array(j,"vendor",256)){
                if(!id.is_number_integer()||id.get<int64_t>()<1||id.get<uint64_t>()>UINT32_MAX)throw std::runtime_error("Invalid catalog vendor item");
                n.vendorItems.push_back(id.get<uint32_t>());
            }
        }
        if(!n.health||!n.level||!n.displayId||n.respawnSeconds<0||n.aggroRadius<0)throw std::runtime_error("Invalid catalog NPC values");
        for(const auto& s:array(j,"loot",8))n.loot.push_back(stack(s));
        result=std::move(n);return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool LocalWorldCatalog::item(uint32_t id,LocalItemDefinition& result,std::string& error) const {
    error.clear();try {
        if(!impl_->loaded)throw std::runtime_error("World catalog not loaded");
        Json j;if(!impl_->items.get(id,j))return false;LocalItemDefinition n;n.id=number(j,"id",0,UINT32_MAX);if(n.id!=id)throw std::runtime_error("Catalog item ID mismatch");
        n.name=label(j,"name",96);n.displayId=number(j,"displayId",0,UINT32_MAX);n.stack=uint16_t(number(j,"stack",1,1000));n.slot=uint8_t(number(j,"slot",0,4));n.inventoryType=uint8_t(number(j,"inventoryType",0,255));
        n.maxHealth=number(j,"maxHealth");n.attack=number(j,"attack");n.armor=number(j,"armor");n.heal=number(j,"heal");n.mana=number(j,"mana");n.value=number(j,"value");if(!n.stack)throw std::runtime_error("Zero catalog stack");result=std::move(n);return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool LocalWorldCatalog::quest(uint32_t id,LocalQuestDefinition& result,std::string& error) const {
    error.clear();try {
        if(!impl_->loaded)throw std::runtime_error("World catalog not loaded");
        Json j;if(!impl_->quests.get(id,j))return false;LocalQuestDefinition q;q.id=number(j,"id",0,UINT32_MAX);if(q.id!=id)throw std::runtime_error("Catalog quest ID mismatch");
        q.title=label(j,"title",96);q.description=label(j,"description",1024,true);q.giverEntry=number(j,"giverEntry",0,UINT32_MAX);q.turnInEntry=number(j,"turnInEntry",0,UINT32_MAX);q.prerequisite=number(j,"prerequisite",0,UINT32_MAX);q.minLevel=uint8_t(number(j,"minLevel",1,80));
        q.allowableRaces=number(j,"allowableRaces",0,UINT32_MAX);q.allowableClasses=number(j,"allowableClasses",0,UINT32_MAX);q.xp=number(j,"xp");q.money=number(j,"money");q.rewardItem=number(j,"rewardItem",0,UINT32_MAX);q.rewardCount=uint16_t(number(j,"rewardCount",0,65535));
        if(j.contains("additionalRewards"))for(const auto& r:array(j,"additionalRewards",3))q.additionalRewards.push_back(stack(r));
        if(j.contains("rewardChoices"))for(const auto& r:array(j,"rewardChoices",6))q.rewardChoices.push_back(stack(r));
        if(!validLocalQuestRewards(q))throw std::runtime_error("Invalid catalog quest reward bundle");
        for(const auto& o:array(j,"objectives",4)){LocalQuestObjective d;const auto t=label(o,"type",16);if(t=="kill")d.type=LocalQuestObjective::Type::Kill;else if(t=="collect")d.type=LocalQuestObjective::Type::Collect;else if(t=="talk")d.type=LocalQuestObjective::Type::Talk;else throw std::runtime_error("Unsupported catalog objective");d.entry=number(o,"entry",0,UINT32_MAX);d.count=uint16_t(number(o,"count",1,65535));if(!d.entry||!d.count)throw std::runtime_error("Zero catalog objective");q.objectives.push_back(d);}
        if(!q.giverEntry||!q.turnInEntry||!q.minLevel||q.objectives.empty())throw std::runtime_error("Incomplete catalog quest");
        result=std::move(q);return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool LocalWorldCatalog::questsForNpc(uint32_t id,std::vector<LocalQuestDefinition>& result,std::string& error) const {
    result.clear();error.clear();try{
        if(!impl_->loaded)throw std::runtime_error("World catalog not loaded");
        Json j;if(!impl_->contacts.get(id,j))return true;
        if(!j.is_array()||j.size()>512)throw std::runtime_error("Catalog quest contact bound");
        for(const auto& qid:j){if(!qid.is_number_unsigned()&&!qid.is_number_integer())throw std::runtime_error("Invalid catalog contact ID");const auto idValue=qid.get<int64_t>();if(idValue<1||uint64_t(idValue)>UINT32_MAX)throw std::runtime_error("Invalid catalog contact ID");LocalQuestDefinition q;if(!quest(uint32_t(idValue),q,error))throw std::runtime_error(error.empty()?"Missing catalog contact quest":error);result.push_back(std::move(q));}return true;
    }catch(const std::exception& e){result.clear();error=e.what();return false;}
}
uint32_t LocalWorldCatalog::fingerprint()const{return impl_->fingerprint;}
const std::vector<LocalCatalogStart>& LocalWorldCatalog::starts()const{return impl_->starts;}
const std::vector<LocalCatalogMap>& LocalWorldCatalog::maps()const{return impl_->maps;}
const std::vector<LocalCatalogDestination>& LocalWorldCatalog::destinations()const{return impl_->destinations;}
size_t LocalWorldCatalog::lastQueryRecordsRead()const{return impl_->lastRead;}
LocalCatalogIoStats LocalWorldCatalog::ioStats() const {
    LocalCatalogIoStats result;
    for (const File* f : {static_cast<const File*>(&impl_->cells), static_cast<const File*>(&impl_->spawns),
         static_cast<const File*>(&impl_->npcs), static_cast<const File*>(&impl_->items),
         static_cast<const File*>(&impl_->quests), static_cast<const File*>(&impl_->contacts)}) {
        result.diskReads += f->diskReads; result.cacheHits += f->cacheHits;
        result.cacheBytes += f->pages.size()*sizeof(File::Page);
    }
    return result;
}

}
