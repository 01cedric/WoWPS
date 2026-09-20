// Narrow production NPC wire codec regression; full LAN/hardware acceptance is P35.
#include "../../src/game/local_realm.cpp"
#include <cassert>
#include <iostream>
using namespace wowee::game;

int main() {
    // writeNpc/readNpc rides the LAN wire only and is never written into a
    // save, so a SaveVersion pin here covered nothing; and a literal LAN pin
    // recorded only which checkpoint last edited the file, failing the build
    // on every unrelated bump. The relation worth asserting survives bumps:
    // the realm codec speaks exactly the version LAN discovery advertises, so
    // a peer accepted by lan::Reply::compatible() decodes this layout. The
    // layout itself is pinned below, against the codec's own per-aura size.
    static_assert(Version==lan::GameplayVersion);
    LocalWorldContent c;LocalNpcDefinition definition;definition.id=50;definition.name="Codec NPC";
    definition.displayId=100;c.npcs.push_back(definition);
    LocalSpellDefinition storm;storm.id=17364;storm.durationMs=12000;storm.stormstrikeProfile=1;c.spells.push_back(storm);
    LocalSpellDefinition snare;snare.id=500;snare.durationMs=6000;snare.snarePercent=20;c.spells.push_back(snare);
    LocalSpellDefinition periodic;periodic.id=501;periodic.durationMs=12000;periodic.periodicDamage=3;c.spells.push_back(periodic);
    std::sort(c.spells.begin(),c.spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    LocalRealmNpc n;n.guid=0xf130000000000001ULL;n.entry=50;n.level=20;n.health=80;n.maxHealth=100;
    n.playerThreat.viewerGuid=41;n.snares={{500,3000,42,20,7}};n.damageAuras={{501,5000,12000,43,1}};
    n.stormstrikeAuras={{17364,12000,41,4,11},{17364,1,42,1,99}};
    auto roundtrip=[&](const LocalRealmNpc& source) {
        Writer wire;writeNpc(wire,source);Reader r(wire.bytes.data(),wire.bytes.size());auto copy=readNpc(r,c);
        assert(r.valid&&r.done());assert(copy.stormstrikeAuras.size()==source.stormstrikeAuras.size());
        for(size_t i=0;i<copy.stormstrikeAuras.size();++i) {
            const auto& a=source.stormstrikeAuras[i];const auto& b=copy.stormstrikeAuras[i];
            assert(a.spellId==b.spellId&&a.casterGuid==b.casterGuid&&a.remainingMs==b.remainingMs&&a.charges==b.charges);
            assert(b.casterRevision==0); // authority lifecycle revision never crosses the wire
        }
        assert(copy.guid==source.guid&&copy.health==source.health&&copy.maxHealth==source.maxHealth);
        assert(copy.playerThreat.viewerGuid==source.playerThreat.viewerGuid&&copy.name==definition.name);
        assert(copy.snares.size()==1&&copy.snares[0].spellId==500&&copy.snares[0].remainingMs==3000&&copy.snares[0].percent==20);
        assert(copy.damageAuras==source.damageAuras);return wire.bytes;
    };
    const auto wire=roundtrip(n);auto zeroRevision=n;for(auto& a:zeroRevision.stormstrikeAuras)a.casterRevision=0;
    Writer zero;writeNpc(zero,zeroRevision);assert(zero.bytes==wire);
    size_t rejected=0;
    auto reject=[&](const LocalRealmNpc& bad) {
        assert(!validLocalNpcStormstrikeAuras(bad,c));Writer bytes;writeNpc(bytes,bad);
        Reader r(bytes.bytes.data(),bytes.bytes.size());(void)readNpc(r,c);assert(!r.valid);++rejected;
    };
    auto bad=n;bad.stormstrikeAuras[1].casterGuid=bad.stormstrikeAuras[0].casterGuid;reject(bad);
    bad=n;bad.stormstrikeAuras[0].casterGuid=0;reject(bad);
    bad=n;bad.stormstrikeAuras[0].spellId=500;reject(bad);
    bad=n;bad.stormstrikeAuras[0].spellId=999999;reject(bad);
    for(uint8_t charges:{uint8_t(0),uint8_t(5),uint8_t(255)}) {bad=n;bad.stormstrikeAuras[0].charges=charges;reject(bad);}
    for(uint32_t duration:{0u,12001u,0xffffffffu}) {bad=n;bad.stormstrikeAuras[0].remainingMs=duration;reject(bad);}
    bad=n;bad.dead=true;bad.health=0;bad.snares.clear();bad.damageAuras.clear();reject(bad);
    auto maximum=n;maximum.stormstrikeAuras.clear();
    for(unsigned i=0;i<kLocalMaxNpcStormstrikeAuras;++i)maximum.stormstrikeAuras.push_back({17364,12000-i,100+i,uint8_t(1+i%4),500+i});
    const auto maxWire=roundtrip(maximum);bad=maximum;bad.stormstrikeAuras.push_back({17364,12000,999,4,1});reject(bad);
    auto empty=n;empty.stormstrikeAuras.clear();const auto emptyWire=roundtrip(empty);
    assert(maxWire.size()==emptyWire.size()+17*kLocalMaxNpcStormstrikeAuras);
    for(size_t size=0;size<maxWire.size();++size) {
        Reader r(maxWire.data(),size);(void)readNpc(r,c);assert(!r.valid);
    }
    // Definitions that no longer support the exact received aura reject it.
    for(unsigned mutation=0;mutation<3;++mutation) {
        auto altered=c;if(mutation==0)altered.spells.back().stormstrikeProfile=2;
        if(mutation==1)altered.spells.back().durationMs=12001;
        if(mutation==2)altered.spells.back().unsupportedReason="Unsupported fixture";
        Reader r(wire.data(),wire.size());(void)readNpc(r,altered);assert(!r.valid);++rejected;
    }
    auto trailing=wire;trailing.push_back(0);Reader extra(trailing.data(),trailing.size());(void)readNpc(extra,c);
    assert(extra.valid&&!extra.done());
    std::cout<<"PASS LAN"<<int(Version)<<" Stormstrike NPC codec: two and eight casters, 1..4 charges, "
               "1..12000ms, authority revision omitted, unrelated NPC/snare/DoT/threat preserved; "
             <<rejected<<" invalid states and "<<maxWire.size()<<" truncated payload lengths rejected; no full LAN/PS4 acceptance claimed\n";
}
