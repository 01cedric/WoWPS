// Reuse the production-import/catalog/runtime scaffold, not the old suite's
// historical fixed population assertions. No original DBC data is embedded.
#include "local_binary_resist_fixture.hpp"

int main(int argc,char** argv) {
    assert(argc==3);
    ClientTables tables;tables.load(argv[1]);gTables=&tables;
    gImported=tables.import();loadCatalog(argv[2]);
    const uint32_t shocks[]={8056,8058,10472,10473,25464,49235,49236};
    unsigned admitted=0,pairs=0;
    for(auto id:shocks) {
        const auto& d=real(id);assert(castable(d)&&d.sourceBinary&&d.snarePercent==50);
        ++admitted;
        for(auto entry:gHostile)if(localResistanceForMask(gNpcs.at(entry).resistances,d.schoolMask)>0)++pairs;
    }
    assert(admitted==7&&pairs==966);
    for(auto id:{116u,133u,853u})assert(!real(id).sourceBinary);
    unsigned miss=0,resist=0,hit=0;
    for(unsigned r=1;r<=10000;++r)switch(localMagicHitOutcome(400,.75f,r)) {
        case LocalMeleeOutcome::Miss:++miss;break;
        case LocalMeleeOutcome::Resist:++resist;break;
        case LocalMeleeOutcome::Hit:++hit;break;
        default:assert(false);
    }
    assert(miss==399&&resist==7500&&hit==2101);
    assert(localAverageResist(0,20,80,true)==0.f);
    assert(localAverageResist(0,20,80,false)>0.f);
    assert(!localBinaryResistApplies(1,true,false,true));
    assert(!localBinaryResistApplies(2,true,false,true));
    assert(!localBinaryResistApplies(16,false,false,true));
    assert(!localBinaryResistApplies(16,true,true,true));
    assert(!localPartialResistApplies(16,true,false,true));
    assert(localPartialResistApplies(16,true,false,false));

    // Select a genuine shipped frost-resistant creature that is not immune to
    // the reviewed spell. Alter health/aggression only via the shared scaffold.
    LocalNpcDefinition target;bool found=false;
    for(auto entry:gHostile) {
        const auto& candidate=gNpcs.at(entry);
        if(candidate.resistances[3]>=100 &&
           !localNpcImmuneToSpell(candidate,real(shocks[0]),false)&&
           !localNpcStrippedEffects(candidate,real(shocks[0]))) {
            target=candidate;found=true;break;
        }
    }
    assert(found);
    unsigned totalResists=0,totalHits=0;
    for(auto id:shocks) {
        World world;buildWorld(world,{id},{{7,LocalResourceType::Mana,80}},target);
        auto& player=world.casters[0];
        const auto initial=*findNpc(world.game,world.npcGuid);
        unsigned resisted=0,landed=0;
        for(unsigned trial=0;trial<300;++trial) {
            world.game.setRemoteNpcs({initial}); // start without a previous snare
            auto r=castOnce(world,player,id);
            assert(r.executed&&r.event&&r.manaPaid>0);
            assert(player.globalCooldownMs>0);
            assert(!player.cooldowns.empty()||!player.categoryCooldowns.empty());
            const auto* npc=findNpc(world.game,world.npcGuid);assert(npc);
            if(r.event->outcome==LocalMeleeOutcome::Resist) {
                ++resisted;assert(!r.event->effective&&npc->health==initial.health&&npc->snares.empty());
                assert(localProcEventHitMask(*r.event)==LocalProcHitFullResist);
                assert(r.event->source==player.guid&&r.event->target==world.npcGuid&&r.event->spell==id);
                assert(r.result=="Resisted");
                Writer wire;writeCast(wire,player);
                LocalRealmPlayer copy=player;copy.meleeViews={};
                Reader reader(wire.bytes.data(),wire.bytes.size());
                assert(readCast(reader,copy)&&reader.done());
                assert(copy.meleeViews.back().outcome==LocalMeleeOutcome::Resist);
                assert(copy.meleeViews.back().spell==id&&!copy.meleeViews.back().amount);
            } else if(r.event->outcome!=LocalMeleeOutcome::Miss) {
                ++landed;assert(r.event->effective>0&&!r.event->resisted&&!npc->snares.empty());
            }
        }
        assert(resisted>10&&landed>10);
        totalResists+=resisted;totalHits+=landed;
    }
    std::cout<<"PASS binary resistance: 7 imported ranks, 966 source pairs; exact one-roll bands; "
             <<totalResists<<" resisted and "<<totalHits
             <<" landed runtime casts; mana/cooldown, no damage/snare, proc mask and codec\n";
    return gFailures?1:0;
}
