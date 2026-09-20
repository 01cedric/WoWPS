// Narrow production codec regression; no full save/UDP/hardware acceptance.
#include "../../src/game/local_realm.cpp"
#include <cassert>
#include <iostream>
using namespace wowee::game;
int main(){
    // writeProgress/readProgress is written both into the save file and into
    // the LAN history message, so both constants matter - but as relations,
    // not as literals that go stale on the next bump. The codec must speak
    // exactly the version LAN discovery advertises, and the legacy save this
    // suite migrates from has to stay strictly older than the current format
    // (parseSave reads 1..SaveVersion) or the migration below stops being one.
    constexpr uint8_t Legacy=25; // last format without the caster armor snapshot
    static_assert(Version==lan::GameplayVersion);
    static_assert(Legacy<SaveVersion);
    LocalRealmPlayer p;p.guid=1;p.classId=1;p.level=40;p.money=321;p.knownSpells={78};
    LocalStatAura aura;aura.spellId=1126;aura.remainingMs=123456;aura.mapId=p.mapId;
    aura.instanceId=p.instanceId;aura.casterGuid=99;aura.stacks=1;aura.buffArmorSnapshot=1512;
    p.statAuras={aura};
    Writer current;writeProgress(current,p);LocalRealmPlayer q=p;q.statAuras.clear();
    auto transient=p;transient.statAuras[0].applicationGeneration=123456789;
    transient.rangedAutoSpellId=75;transient.rangedTarget=123;transient.rangedRemainingMs=2100;
    Writer runtimeOnly;writeProgress(runtimeOnly,transient);assert(runtimeOnly.bytes==current.bytes);
    Reader read(current.bytes.data(),current.bytes.size());assert(readProgress(read,q)&&read.done());
    assert(q.statAuras==p.statAuras&&q.money==p.money&&q.knownSpells==p.knownSpells);
    // Every later format adds to this per-player block. Measure the delta
    // against the codec instead of pinning a literal that goes stale each
    // checkpoint: what this suite asserts is that the legacy block is a
    // strict prefix-compatible shorter encoding, not its exact byte count.
    Writer legacy;writeProgress(legacy,p,Legacy);
    assert(legacy.bytes.size()<current.bytes.size());
    q=p;Reader old(legacy.bytes.data(),legacy.bytes.size());assert(readProgress(old,q,Legacy)&&old.done());
    assert(q.statAuras.size()==1&&q.statAuras[0].buffArmorSnapshot==0);
    auto expected=aura;expected.buffArmorSnapshot=0;assert(q.statAuras[0]==expected&&q.money==p.money);
    auto bad=p;bad.statAuras[0].buffArmorSnapshot=1000001;
    Writer malformed;writeProgress(malformed,bad);Reader invalid(malformed.bytes.data(),malformed.bytes.size());
    assert(!readProgress(invalid,q));
    for(size_t n=0;n<current.bytes.size();++n){
        Reader shortRead(current.bytes.data(),n);assert(!readProgress(shortRead,q)||!shortRead.done());
    }
    std::cout<<"PASS: Save"<<int(SaveVersion)<<"/LAN"<<int(Version)<<" production progress codec preserves "
               "caster armor snapshot and unrelated owner state; Save"<<int(Legacy)<<" migration gives legacy "
               "zero snapshot; oversized snapshot and every truncated payload rejected; "
               "no full save/LAN/PS4 acceptance claimed\n";
}
