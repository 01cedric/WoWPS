// Narrow LAN codec regression, against whatever version lan::GameplayVersion
// advertises (82 at the the implementation checkpoint); the static_assert below is what ties
// the two together, so this line is not a second pin that can go stale on its
// own. Includes the real codec; unrelated realm operations are removed by
// linker GC. This is not a UDP or PS4 acceptance test.
#include "../../src/game/local_realm.cpp"
#include <cassert>
#include <iostream>
using namespace wowee::game;

static LocalRealmPlayer owner() {
    LocalRealmPlayer p; p.guid=101; p.classId=8;
    p.positionRevision=p.meleeViewPositionRevision=7;
    return p;
}
static LocalMeleeView observation(bool healing, uint64_t target=101) {
    LocalMeleeView v; v.serial=1; v.spell=healing?2050:133;
    v.amount=150; v.source=101; v.target=target;
    v.outcome=LocalMeleeOutcome::Critical; v.healing=healing;
    return v;
}
static std::vector<uint8_t> encode(const LocalRealmPlayer& p) {
    Writer w; writeCast(w,p); return w.bytes;
}
static bool decode(const std::vector<uint8_t>& bytes, LocalRealmPlayer& p) {
    Reader r(bytes.data(),bytes.size()); return readCast(r,p)&&r.done();
}
static void reject(LocalMeleeView v) {
    auto p=owner();p.meleeViews.back()=v;
    auto loaded=owner();assert(!decode(encode(p),loaded));
}
static void equalView(const LocalMeleeView& a,const LocalMeleeView& b) {
    assert(a.serial==b.serial&&a.spell==b.spell&&a.amount==b.amount&&a.blocked==b.blocked);
    assert(a.source==b.source&&a.target==b.target&&a.outcome==b.outcome&&a.offHand==b.offHand&&a.healing==b.healing);
}
int main() {
    // writeCast/readCast travels only on the LAN wire (welcome and history);
    // it is never written into a save file, so a SaveVersion pin here said
    // nothing about this codec. The literal LAN pin this line used to carry
    // (static_assert(Version==74)) recorded only which checkpoint last edited
    // the file and broke the build on every unrelated protocol bump, so assert
    // the relation that stays true across bumps instead: the realm codec must
    // speak exactly the version LAN discovery advertises, or a peer accepted by
    // lan::Reply::compatible() would go on to decode a different layout.
    static_assert(Version==lan::GameplayVersion);
    // The layout is what this file really pins: every byte offset used below
    // is derived from this payload size, so a shape change has to fail here
    // and be re-derived rather than silently shifting the corruption probes.
    // the implementation (LAN 84) appended a 4-byte `resisted` to each of the four views,
    // AFTER the outcome/offHand/healing bytes, so 206 became 222 and every
    // probe offset below (66 + 32..34, the first view's trailing bytes) still
    // names the byte it did.
    static_assert(CastWireBytes==222);
    auto p=owner();p.meleeViews[0]=observation(true);
    p.meleeViews[1]=observation(true,202);p.meleeViews[1].serial=2;
    p.meleeViews[2]=observation(true);p.meleeViews[2].serial=3;p.meleeViews[2].source=202;
    p.meleeViews[2].outcome=LocalMeleeOutcome::Hit;p.meleeViews[2].amount=0;
    p.meleeViews[3]=observation(false,303);p.meleeViews[3].serial=4;p.meleeViews[3].offHand=true;
    const auto bytes=encode(p);assert(bytes.size()==CastWireBytes);
    auto loaded=owner();assert(decode(bytes,loaded));
    assert(loaded.meleeSerial==4&&loaded.meleeViewPositionRevision==p.positionRevision);
    for(size_t i=0;i<p.meleeViews.size();++i)equalView(p.meleeViews[i],loaded.meleeViews[i]);
    std::cout<<"PASS LAN"<<int(Version)<<" real codec: critical self/friendly heals, incoming heal, "
               "zero effective heal and damage roundtrip; exact "<<CastWireBytes<<" bytes\n";

    reject(observation(false));
    auto bad=observation(true);bad.spell=0;reject(bad);
    bad=observation(true);bad.offHand=true;reject(bad);
    bad=observation(true);bad.blocked=1;reject(bad);
    for(auto outcome:{LocalMeleeOutcome::Miss,LocalMeleeOutcome::Dodge,LocalMeleeOutcome::Parry,LocalMeleeOutcome::Block,LocalMeleeOutcome::Glancing,LocalMeleeOutcome::Crushing}) {
        bad=observation(true);bad.outcome=outcome;bad.amount=0;reject(bad);
    }
    bad=observation(true);bad.source=0;reject(bad);
    bad=observation(true);bad.target=0;reject(bad);
    bad=observation(true,202);bad.source=303;reject(bad);
    bad=observation(true);bad.amount=1000001;reject(bad);
    bad=observation(true);bad.serial=0;reject(bad);
    auto corrupt=bytes;corrupt[66+34]=2;loaded=owner();assert(!decode(corrupt,loaded));
    corrupt=bytes;corrupt[66+33]=2;loaded=owner();assert(!decode(corrupt,loaded));
    corrupt=bytes;corrupt[66+32]=255;loaded=owner();assert(!decode(corrupt,loaded));
    std::cout<<"PASS malformed observations: boolean bytes, self damage, heal spell/hand/block/outcome constraints, owner identity, empty slot and amount bounds\n";

    for(size_t n=0;n<bytes.size();++n) {
        std::vector<uint8_t> shortBytes(bytes.begin(),bytes.begin()+n);
        loaded=owner();assert(!decode(shortBytes,loaded));
    }
    auto hole=p;hole.meleeViews[1]={};loaded=owner();assert(!decode(encode(hole),loaded));
    auto duplicate=p;duplicate.meleeViews[2].serial=2;loaded=owner();assert(!decode(encode(duplicate),loaded));
    ++p.positionRevision;loaded=owner();assert(decode(encode(p),loaded));assert(loaded.meleeSerial==0);
    for(const auto& v:loaded.meleeViews)equalView(v,LocalMeleeView{});
    std::cout<<"PASS bounded window: all "<<bytes.size()<<" truncation points rejected, "
               "hole/duplicate serial rejected, stale position window cleared\n";
    std::cout<<"PASS compile-time compatibility: the realm codec speaks exactly the advertised "
               "LAN"<<int(lan::GameplayVersion)<<" and the cast payload is still "<<CastWireBytes
             <<" bytes; no full LAN or PS4 acceptance claimed\n";
}
