// Include the codec implementation to exercise actual progress/save readers,
// including version-8 migration; unrelated realm functions are link-GC'd.
#include "../../src/game/local_realm.cpp"
#include "game/local_spell_import.hpp"
#include <cassert>
#include <iostream>

using namespace wowee::game;
using wowee::pipeline::DBCFile;
static std::unique_ptr<DBCFile> dbc(std::vector<uint32_t> fields) {
    std::vector<uint8_t> data{'W','D','B','C'};
    auto u32=[&](uint32_t v){for(unsigned i=0;i<4;++i)data.push_back(uint8_t(v>>(8*i)));};
    u32(1);u32(static_cast<uint32_t>(fields.size()));u32(static_cast<uint32_t>(fields.size()*4));u32(1);
    for(auto v:fields)u32(v);data.push_back(0);
    auto out=std::make_unique<DBCFile>();assert(out->load(data));return out;
}
static uint32_t floatBits(float f) {uint32_t v;std::memcpy(&v,&f,4);return v;}
static void testCosts() {
    std::vector<uint32_t> spell(234);spell[0]=45477;spell[28]=1;spell[39]=1;
    spell[41]=5;spell[46]=1;spell[68]=UINT32_MAX;spell[71]=2;spell[80]=10;spell[86]=6;spell[226]=77;
    std::vector<uint32_t> range(40);range[0]=1;range[3]=range[4]=floatBits(20);
    auto spells=dbc(spell), ranges=dbc(range), casts=dbc({1,0,0,0}), durations=dbc({1,0,0,0});
    auto costs=dbc({77,1,0,1,100});
    auto imported=importClientStarterSpells(spells.get(),ranges.get(),casts.get(),durations.get(),nullptr,nullptr,nullptr,nullptr,costs.get());
    auto get=[&]() -> const LocalSpellDefinition& {for(auto& s:imported.spells)if(s.id==45477)return s;std::abort();};
    assert(get().unsupportedReason.empty());
    assert((get().runeCost==LocalRuneCost{1,0,1}) && get().runicPowerGain==10 && get().resourceType==5);
    imported=importClientStarterSpells(spells.get(),ranges.get(),casts.get(),durations.get());
    assert(get().unsupportedReason.find("SpellRuneCost")!=std::string::npos);
    auto bad=dbc({77,3,0,1,100});
    imported=importClientStarterSpells(spells.get(),ranges.get(),casts.get(),durations.get(),nullptr,nullptr,nullptr,nullptr,bad.get());
    assert(!get().unsupportedReason.empty());
    spell[71]=6;spell[95]=22;spells=dbc(spell);
    imported=importClientStarterSpells(spells.get(),ranges.get(),casts.get(),durations.get(),nullptr,nullptr,nullptr,nullptr,costs.get());
    assert(get().unsupportedReason.find("Unsupported effect")!=std::string::npos);
    std::cout<<"PASS rune DBC import: authored costs/power gain, missing/malformed rejection, unsupported effects remain blocked\n";
}
static void testLifecycle() {
    LocalRuneCooldowns runes{};
    const LocalRuneCost cost{1,0,1};
    auto first=selectLocalRunes(6,runes,cost);assert(first && *first==0x11);
    consumeLocalRunes(runes,*first);assert(runes[0]==10000 && runes[4]==10000);
    auto second=selectLocalRunes(6,runes,cost);assert(second && *second==0x22);
    consumeLocalRunes(runes,*second);const auto before=runes;
    assert(!selectLocalRunes(6,runes,cost));assert(runes==before);
    assert(advanceLocalRunes(runes,9999));assert(!selectLocalRunes(6,runes,cost));
    assert(advanceLocalRunes(runes,1));assert(selectLocalRunes(6,runes,cost));
    assert(!advanceLocalRunes(runes,0));assert(!selectLocalRunes(1,runes,cost));
    assert(!selectLocalRunes(6,runes,{3,0,0}));
    runes.fill(10000);assert(advanceLocalRunes(runes,UINT32_MAX));
    for(auto cd:runes)assert(!cd);
    runes[3]=10001;assert(!validLocalRunes(runes));
    std::cout<<"PASS rune lifecycle: atomic selection, independent paired recharge, exhaustion, class/cost bounds and elapsed overflow\n";
}
static void testCodec() {
    LocalRealmPlayer player;player.classId=6;player.resourceType=LocalResourceType::RunicPower;
    player.mana=15;player.money=456;player.knownSpells={45477};player.inventory={{117,3}};
    player.runeCooldownMs={10000,9500,0,2500,1,0};
    Writer wire;writeProgress(wire,player,9);
    LocalRealmPlayer loaded;Reader read(wire.bytes.data(),wire.bytes.size());
    assert(readProgress(read,loaded,9) && read.done());
    assert(loaded.runeCooldownMs==player.runeCooldownMs && loaded.money==456 && loaded.inventory[0].count==3);
    auto old=wire.bytes;old.resize(old.size()-12);Reader version8(old.data(),old.size());
    loaded.runeCooldownMs.fill(9000);
    assert(readProgress(version8,loaded,8) && version8.done());
    for(auto cd:loaded.runeCooldownMs)assert(cd==0);
    assert(loaded.money==456 && loaded.knownSpells==player.knownSpells);
    auto corrupt=wire.bytes;corrupt[corrupt.size()-12]=0xff;corrupt[corrupt.size()-11]=0xff;
    Reader invalid(corrupt.data(),corrupt.size());assert(!readProgress(invalid,loaded,9));
    auto truncated=wire.bytes;truncated.pop_back();Reader shortRead(truncated.data(),truncated.size());
    assert(!readProgress(shortRead,loaded,9));
    static_assert(HeaderSize+10+ProgressChunkBytes<=MaxPacket);
    player.name="SixteenLettersAa";player.mountSpellId=6648;
    Writer cast;writeCast(cast,player);assert(cast.bytes.size()==CastWireBytes);
    LocalRealmPlayer mounted;Reader castRead(cast.bytes.data(),cast.bytes.size());
    assert(readCast(castRead,mounted) && castRead.done() && mounted.mountSpellId==6648);
    Writer welcome;welcome.u64(1);welcome.u64(2);welcome.u64(player.guid);welcome.u8(100);welcome.u8(1);
    writePlayer(welcome,player);writeNetworkVitals(welcome,player);writeAppearance(welcome,player);
    writeCast(welcome,player);welcome.u8(1);
    assert(HeaderSize+welcome.bytes.size()==WelcomeWireBytes && WelcomeWireBytes<=MaxPacket);
    std::cout<<"PASS rune codec: actual save9/owner progress roundtrip, save8 migration, malformed/truncated rejection and MTU bound\n";
}
int main(){testCosts();testLifecycle();testCodec();}
