// Focused production progress codec and absorb snapshot regression.
// Complete save, live LAN and console acceptance remain in P35.
#include "../../src/game/local_realm.cpp"
#include <cassert>
#include <iostream>
using namespace wowee::game;

int main() {
    // writeProgress/readProgress is written both into the save file and into
    // the LAN history message, so both constants matter - but as relations,
    // not as literals that go stale on the next bump. The codec must speak
    // exactly the version LAN discovery advertises, and the legacy saves this
    // suite migrates from have to stay strictly older than the current format
    // (parseSave reads 1..SaveVersion) or those migrations stop being ones.
    constexpr uint8_t OldestLegacy=25,NewestLegacy=26; // pre-absorb, pre-reflect-snapshot
    static_assert(Version==lan::GameplayVersion);
    static_assert(OldestLegacy<NewestLegacy&&NewestLegacy<SaveVersion);
    LocalRealmPlayer p;p.guid=17;p.classId=8;p.level=40;p.money=321;
    p.knownSpells={543};p.health=p.maxHealth=1000;
    LocalStatAura ward;ward.spellId=543;ward.remainingMs=30000;
    ward.mapId=p.mapId;ward.instanceId=p.instanceId;ward.casterGuid=p.guid;
    ward.absorbRemaining=500;ward.reflectChanceBasisPointsSnapshot=3000;
    p.statAuras={ward};
    auto roundtrip=[&](const LocalRealmPlayer& source) {
        Writer w;writeProgress(w,source);LocalRealmPlayer copy;
        Reader r(w.bytes.data(),w.bytes.size());assert(readProgress(r,copy)&&r.done());
        assert(copy.statAuras==source.statAuras&&copy.money==source.money&&copy.knownSpells==source.knownSpells);
        return w.bytes;
    };
    const auto current=roundtrip(p);
    auto noAuras=p;noAuras.statAuras.clear();
    assert(current.size()==roundtrip(noAuras).size()+49);
    for(uint16_t chance:{uint16_t(0),uint16_t(1),uint16_t(10000)}) {
        auto boundary=p;boundary.statAuras[0].reflectChanceBasisPointsSnapshot=chance;roundtrip(boundary);
    }
    for(uint32_t id:{543u,8457u,8458u,10223u,10225u,27128u,43010u,
                    6143u,8461u,8462u,10177u,28609u,32796u,43012u}) {
        auto rank=p;rank.statAuras[0].spellId=id;roundtrip(rank);
    }
    size_t previousLegacySize=0;
    for(uint8_t version:{OldestLegacy,NewestLegacy}) {
        Writer legacy;writeProgress(legacy,p,version);
        assert(!previousLegacySize||legacy.bytes.size()>previousLegacySize);
        previousLegacySize=legacy.bytes.size();
        // Later formats keep adding to this block; the invariant is that an
        // older encoding is strictly shorter and that the oldest is shorter
        // still, not any particular literal delta.
        assert(legacy.bytes.size()<current.size());
        auto copy=p;Reader r(legacy.bytes.data(),legacy.bytes.size());
        assert(readProgress(r,copy,version)&&r.done());
        auto expected=ward;expected.reflectChanceBasisPointsSnapshot=0;
        assert(copy.statAuras.size()==1&&copy.statAuras[0]==expected);
        assert(copy.money==p.money&&copy.knownSpells==p.knownSpells);
    }
    auto transient=p;transient.statAuras[0].applicationGeneration=999;
    Writer runtimeOnly;writeProgress(runtimeOnly,transient);assert(runtimeOnly.bytes==current);
    auto reject=[&](const LocalRealmPlayer& bad) {
        assert(!validLocalStatAuras(bad));Writer w;writeProgress(w,bad);
        Reader r(w.bytes.data(),w.bytes.size());LocalRealmPlayer copy;
        assert(!readProgress(r,copy));
    };
    for(uint16_t chance:{uint16_t(10001),uint16_t(65535)}) {
        auto bad=p;bad.statAuras[0].reflectChanceBasisPointsSnapshot=chance;reject(bad);
    }
    for(uint32_t id:{1126u,11094u,13043u,999999u}) {
        auto bad=p;bad.statAuras[0].spellId=id;reject(bad);
        bad.statAuras[0].reflectChanceBasisPointsSnapshot=0;roundtrip(bad);
    }
    auto bad=p;bad.statAuras[0].absorbRemaining=1000001;reject(bad);
    auto bounded=p;bounded.statAuras[0].absorbRemaining=1000000;roundtrip(bounded);
    for(size_t n=0;n<current.size();++n) {
        Reader r(current.data(),n);LocalRealmPlayer copy;
        assert(!readProgress(r,copy)||!r.done());
    }
    // Owner damage observations admit the new Reflect outcome, but no value
    // above its enum boundary. Identity and ordinary cast fields still validate.
    auto reflected=p;reflected.meleeViews.back()={1,133,75,0,p.guid,999,LocalMeleeOutcome::Reflect,false,false};
    Writer reflectedWire;writeCast(reflectedWire,reflected);LocalRealmPlayer reflectedCopy=p;
    Reader reflectedRead(reflectedWire.bytes.data(),reflectedWire.bytes.size());
    assert(readCast(reflectedRead,reflectedCopy)&&reflectedRead.done());
    assert(reflectedCopy.meleeViews.back().outcome==LocalMeleeOutcome::Reflect);
    assert(reflectedCopy.meleeViews.back().amount==75);
    for(uint8_t value:{uint8_t(9),uint8_t(255)}) {
        reflected.meleeViews.back().outcome=LocalMeleeOutcome(value);
        Writer malformedView;writeCast(malformedView,reflected);
        Reader malformedRead(malformedView.bytes.data(),malformedView.bytes.size());
        assert(!readCast(malformedRead,reflectedCopy));
    }
    // Ward base 165 can have a larger, valid caster snapshot. Its remaining
    // pool survives serialization and is depleted directly by incoming damage.
    LocalWorldContent content;LocalSpellDefinition definition;definition.id=543;
    definition.buffAbsorb=165;definition.absorbSchoolMask=4;definition.wardProfile=1;
    content.spells.push_back(definition);
    assert(localAbsorbDamage(p,content,200,4)==0);
    assert(p.statAuras.size()==1&&p.statAuras[0].absorbRemaining==300);
    assert(localAbsorbDamage(p,content,1,16)==1&&p.statAuras[0].absorbRemaining==300);
    assert(localAbsorbDamage(p,content,350,4)==50&&p.statAuras.empty());
    // Ordinary shields retain their prior base-points clamp.
    content.spells[0].wardProfile=0;p.statAuras={ward};
    assert(localAbsorbDamage(p,content,200,4)==35&&p.statAuras.empty());
    std::cout<<"PASS Save"<<int(SaveVersion)<<"/LAN"<<int(Version)<<" Ward snapshots: all 14 ranks, "
               "0..10000 chance, Save"<<int(OldestLegacy)<<"/"<<int(NewestLegacy)<<" migration, transient identity "
               "omitted, malformed/truncated payload rejection and scaled absorb pool preserved; "
               "full save/LAN/PS4 acceptance remains P35\n";
}
