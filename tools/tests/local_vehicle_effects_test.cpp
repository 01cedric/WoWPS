#include "game/local_gameplay.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace wowee::game;

struct Scenario {
    LocalGameplay game;
    LocalRealmPlayer player;
    std::vector<LocalRealmPlayer*> players;
    uint64_t hull=0;
    std::string result;

    explicit Scenario(const char* path) {
        assert(game.loadContent(path,result));
        player.guid=1;player.name="Driver";game.initializePlayer(player,true);
        players={&player};game.tick(0,players);hull=npc(50).guid;
        assert(game.execute(player,{LocalAction::EnterVehicle,hull,0},players,result));
    }
    LocalRealmNpc& npc(uint32_t entry) {
        for(const auto& row:game.npcs())if(row.entry==entry)return const_cast<LocalRealmNpc&>(row);
        std::abort();
    }
    const LocalRealmNpc* actor(uint32_t id) const {
        for(const auto& row:game.npcs())if(row.scriptActorId==id)return &row;
        return nullptr;
    }
    bool use(uint32_t slot,uint64_t target=0) {
        LocalRealmCommand command{LocalAction::VehicleAbility,target,slot};command.serviceNpcGuid=hull;
        return game.execute(player,command,players,result);
    }
    void advance(float seconds) {
        while(seconds>0) {const float step=std::min(seconds,.25f);game.tick(step,players);seconds-=step;}
    }
    void advanceOffline(float seconds) {
        while(seconds>0) {const float step=std::min(seconds,.25f);game.tick(step,{});seconds-=step;}
    }
};

int main(int argc,char** argv) {
    assert(argc==2);

    // A cast publishes progress but reserves no power/cooldown. Its elemental
    // AoE resolves once, in deterministic target order, at completion.
    {
        Scenario s(argv[1]);
        auto bystander=s.player;bystander.guid=2;bystander.name="Bystander";bystander.vehicleGuid=0;bystander.vehicleId=0;
        bystander.vehicleSeat=0;bystander.vehicleControl=false;bystander.x=10;bystander.health=bystander.maxHealth=777;
        s.players.push_back(&bystander);assert(s.use(0,s.npc(51).guid));
        assert(s.game.vehicleCasts().size()==1 && s.npc(50).vehiclePower==100 && !s.npc(50).vehicleCooldownMs[0]);
        const auto cast=s.game.vehicleCasts().front();
        assert(validLocalVehicleCastView(cast,s.game.content()) && cast.totalMs==1000 && cast.remainingMs==1000 && cast.slot==0);
        LocalGameplay remote;remote.useContent(s.game.sharedContent());remote.setRemoteVehicleCasts({cast});
        assert(remote.vehicleCasts().size()==1);
        auto invalid=cast;invalid.remainingMs=0;remote.setRemoteVehicleCasts({invalid});
        assert(remote.vehicleCasts().size()==1);remote.setRemoteVehicleCasts({cast,cast});assert(remote.vehicleCasts().size()==1);
        s.advance(.5f);assert(s.game.vehicleCasts().size()==1 && s.game.vehicleCasts()[0].remainingMs==500);
        s.advance(.5f);assert(s.game.vehicleCasts().empty());
        assert(s.npc(50).vehiclePower==70 && s.npc(50).vehicleCooldownMs[0]==2000);
        assert(s.npc(51).health==900 && s.npc(52).health==900 && s.npc(53).health==1000 && s.npc(56).health==1000);
        assert(s.npc(54).dead && s.actor(7201));
        assert(s.game.scriptDialogues().size()==1 && s.game.scriptDialogues()[0].text=="Vehicle area kill committed.");
        assert(bystander.health==777);
        std::vector<uint64_t> fireTargets;for(const auto& event:s.game.combatEvents())if(event.spell==9201){
            assert(event.schoolMask==4 && event.attackType==LocalCombatAttackType::Magic);fireTargets.push_back(event.target);
        }
        assert(fireTargets.size()==3 && std::is_sorted(fireTargets.begin(),fireTargets.end()));
    }
    std::cout<<"PASS cast progress, atomic remote view, completion resource commit and direct AoE npcKill dialogue/spawn\n";

    // Physical AoE keeps armor reduction, while a no-power elemental ability
    // spends no energy and bypasses armor.
    {
        Scenario s(argv[1]);assert(s.use(1,s.npc(51).guid));
        const auto physical=1000-s.npc(51).health;
        assert(physical>0 && physical<100 && s.npc(52).health==1000-physical && s.npc(53).health==1000);
        assert(s.npc(50).vehiclePower==90);
    }
    {
        Scenario s(argv[1]);assert(s.use(2,s.npc(51).guid));
        assert(s.npc(51).health==950 && s.npc(50).vehiclePower==100);
        const auto& event=s.game.combatEvents().back();
        assert(event.spell==9203 && event.schoolMask==64 && event.attackType==LocalCombatAttackType::Magic);
    }
    std::cout<<"PASS physical armor, elemental school events and authored none/energy power types\n";

    // Queue admission is part of the whole direct AoE preflight. A lethal
    // secondary target cannot turn a full reward queue into partially damaged
    // primary targets plus spent energy/cooldown.
    {
        Scenario s(argv[1]);auto saturated=s.game.scriptActionCheckpoint();
        for(size_t i=0;i<LocalGameplay::MaxPendingScriptKills;++i)
            saturated.pendingKills.push_back({1000+i,uint32_t(1000+i),0,1});
        s.game.restoreScriptActionCheckpoint(std::move(saturated));
        const auto primary=s.npc(51),nearby=s.npc(52),victim=s.npc(54),hull=s.npc(50);
        const auto events=s.game.combatEvents().size();
        assert(!s.use(1,s.npc(51).guid));
        assert(s.npc(51).health==primary.health && s.npc(52).health==nearby.health &&
               s.npc(54).health==victim.health && !s.npc(54).dead);
        assert(s.npc(50).vehiclePower==hull.vehiclePower && !s.npc(50).vehicleCooldownMs[1] &&
               !s.npc(50).vehicleGlobalCooldownMs && s.game.combatEvents().size()==events);
        assert(s.game.pendingScriptKills().size()==LocalGameplay::MaxPendingScriptKills);
        auto available=s.game.scriptActionCheckpoint();available.pendingKills.clear();
        s.game.restoreScriptActionCheckpoint(std::move(available));
        assert(s.use(1,s.npc(51).guid));
        assert(s.npc(51).health<primary.health && s.npc(52).health<nearby.health && s.npc(54).dead &&
               s.npc(50).vehiclePower==hull.vehiclePower-10 && s.npc(50).vehicleCooldownMs[1]==1000);
    }
    std::cout<<"PASS full reward queue rejects direct vehicle AoE before damage, power or cooldown commit\n";

    // Cancel paths never spend the completion resource or begin cooldowns.
    {
        Scenario s(argv[1]);const auto health=s.npc(51).health;assert(s.use(0,s.npc(51).guid));
        assert(s.game.execute(s.player,{LocalAction::CancelCast},s.players,s.result));
        s.advance(1.f);assert(s.game.vehicleCasts().empty() && s.npc(50).vehiclePower==100 && s.npc(51).health==health);
    }
    {
        Scenario s(argv[1]);assert(s.use(0,s.npc(51).guid));
        assert(s.game.moveVehicle(s.player,0,.25f,0,0,0,1));
        assert(s.game.vehicleCasts().empty() && s.npc(50).vehiclePower==100);
    }
    {
        Scenario s(argv[1]);assert(s.use(0,s.npc(51).guid));
        assert(s.game.execute(s.player,{LocalAction::SwitchVehicleSeat,s.hull,1},s.players,s.result));
        assert(s.game.vehicleCasts().empty() && s.npc(50).vehiclePower==100);
    }
    {
        Scenario s(argv[1]);assert(s.use(0,s.npc(51).guid));
        assert(s.game.execute(s.player,{LocalAction::ExitVehicle},s.players,s.result));
        assert(s.game.vehicleCasts().empty() && s.npc(50).vehiclePower==100);
    }
    {
        Scenario s(argv[1]);assert(s.use(0,s.npc(51).guid));s.player.dead=true;s.player.health=0;
        s.advance(.1f);assert(s.game.vehicleCasts().empty() && s.npc(50).vehiclePower==100);
    }
    {
        Scenario s(argv[1]);assert(s.use(0,s.npc(51).guid));
        s.advanceOffline(.1f);assert(s.game.vehicleCasts().empty() && s.npc(50).vehiclePower==100);
    }
    {
        Scenario s(argv[1]);assert(s.use(0,s.npc(51).guid));s.npc(51).health=0;s.npc(51).dead=true;
        s.advance(1.f);assert(s.game.vehicleCasts().empty() && s.npc(50).vehiclePower==100);
    }
    {
        Scenario s(argv[1]);assert(s.use(0,s.npc(51).guid));s.npc(50).vehiclePower=29;
        s.advance(1.f);assert(s.game.vehicleCasts().empty() && s.npc(50).vehiclePower==29 && !s.npc(50).vehicleCooldownMs[0] && s.npc(51).health==1000);
    }
    {
        Scenario s(argv[1]);assert(s.use(0,s.npc(51).guid));assert(!s.use(1,s.npc(51).guid));
        assert(s.game.vehicleCasts().size()==1 && s.npc(50).vehiclePower==100);
        assert(s.game.execute(s.player,{LocalAction::StopAttack},s.players,s.result));
        assert(s.game.vehicleCasts().empty() && s.npc(50).vehiclePower==100 && !s.npc(50).vehicleCooldownMs[0]);
    }
    {
        Scenario s(argv[1]);assert(s.use(0,s.npc(51).guid));
        s.npc(50).controls.emplace_back();s.npc(50).controls.back().kind=uint8_t(LocalNpcControlKind::Stun);
        s.npc(50).controls.back().casterGuid=s.player.guid;s.npc(50).controls.back().casterRevision=s.player.positionRevision;
        s.npc(50).controls.back().remainingMs=500;s.advance(.1f);
        assert(s.game.vehicleCasts().empty() && s.npc(50).vehiclePower==100 && !s.npc(50).vehicleCooldownMs[0]);
    }
    {
        Scenario s(argv[1]);assert(s.use(0,s.npc(51).guid));s.npc(50).dead=true;s.npc(50).health=0;s.advance(.1f);
        assert(s.game.vehicleCasts().empty() && s.npc(50).vehiclePower==100 && !s.npc(50).vehicleCooldownMs[0]);
    }
    {
        Scenario s(argv[1]);assert(s.use(0,s.npc(51).guid));++s.npc(50).combatEpoch;s.advance(.1f);
        assert(s.game.vehicleCasts().empty() && s.npc(50).vehiclePower==100 && !s.npc(50).vehicleCooldownMs[0]);
    }
    {
        Scenario s(argv[1]);assert(s.use(0,s.npc(51).guid));s.player.phaseMask=2;s.advance(.1f);
        assert(s.game.vehicleCasts().empty() && s.npc(50).vehiclePower==100 && !s.npc(50).vehicleCooldownMs[0]);
    }
    {
        Scenario s(argv[1]);assert(s.use(0,s.npc(51).guid));s.npc(51).x=40;s.advance(1.f);
        assert(s.game.vehicleCasts().empty() && s.npc(50).vehiclePower==100 && !s.npc(50).vehicleCooldownMs[0] && s.npc(51).health==1000);
    }
    std::cout<<"PASS all cast cancellation paths, hull-wide pending limit and completion range/resource revalidation\n";

    // An ability may explicitly survive hull movement. Timed repairs use the
    // same completion commit and never heal an occupant.
    {
        Scenario s(argv[1]);assert(s.use(5,s.npc(51).guid));assert(s.game.moveVehicle(s.player,0,.25f,0,0,0,1));
        assert(s.game.vehicleCasts().size()==1);s.advance(.5f);
        assert(s.game.vehicleCasts().empty() && s.npc(51).health==980 && s.npc(50).vehiclePower==95);
    }
    {
        Scenario s(argv[1]);s.npc(50).health=800;const auto riderHealth=s.player.health;
        assert(s.use(4,s.hull));assert(s.npc(50).health==800 && s.npc(50).vehiclePower==100);
        s.advance(.5f);assert(s.npc(50).health==860 && s.npc(50).vehiclePower==80 && s.player.health==riderHealth);
    }
    std::cout<<"PASS authored movement policy and timed repair completion\n";

    // Projectile casts snapshot their confirmed aim. The newly launched shot
    // does not consume simulation time from before its cast completed.
    {
        Scenario s(argv[1]);assert(s.use(3));
        LocalRealmCommand aim{LocalAction::VehicleAim,s.hull,s.player.vehicleSeat};aim.serviceNpcGuid=s.hull;aim.vehicleAimYaw=1;aim.vehicleAimPitch=0;
        assert(s.game.execute(s.player,aim,s.players,s.result));
        s.advance(.5f);assert(s.game.vehicleCasts().empty() && s.game.vehicleProjectiles().size()==1);
        const auto& shot=s.game.vehicleProjectiles()[0];
        assert(std::abs(shot.x)<.001f && std::abs(shot.y)<.001f && std::abs(shot.vx-20)<.001f && std::abs(shot.vy)<.001f);
        s.advance(.5f);assert(s.game.vehicleProjectiles().empty());
        assert(s.npc(51).health==920 && s.npc(52).health==920 && s.npc(53).health==1000 &&
            s.npc(56).health==920 && s.npc(50).vehiclePower==85);
        assert(s.npc(54).dead && s.actor(7201));
        assert(s.game.scriptDialogues().size()==1 && s.game.scriptDialogues()[0].text=="Vehicle area kill committed.");
        unsigned frostHits=0;for(const auto& event:s.game.combatEvents())if(event.spell==9204){assert(event.schoolMask==16);++frostHits;}
        assert(frostHits==4);
    }
    std::cout<<"PASS cast-time projectile aim snapshot, launch timing and projectile AoE npcKill dialogue/spawn\n";

    // Every new content dimension is strict; malformed profiles never enter
    // the authority and therefore cannot produce peer-dependent effects.
    char temp[]="/tmp/wowps-vehicle-effects-XXXXXX";assert(mkdtemp(temp));
    const auto original=nlohmann::json::parse(std::ifstream(argv[1]));const auto path=std::string(temp)+"/world.json";
    for(int scenario=0;scenario<10;++scenario) {
        auto j=original;auto& first=j["vehicleKits"][0]["abilities"][0];auto& repair=j["vehicleKits"][0]["abilities"][4];
        if(scenario==0)first["castTimeMs"]=10001;
        if(scenario==1)first["areaRadius"]=41;
        if(scenario==2)first["schoolMask"]=0;
        if(scenario==3)first["schoolMask"]=3;
        if(scenario==4)first["schoolMask"]=128;
        if(scenario==5){first["powerType"]=0;first["powerCost"]=1;}
        if(scenario==6)first["powerType"]=2;
        if(scenario==7)repair["areaRadius"]=1;
        if(scenario==8)repair["schoolMask"]=4;
        if(scenario==9)first["interruptOnMove"]="yes";
        std::ofstream(path)<<j.dump();LocalGameplay rejected;std::string error;assert(!rejected.loadContent(path,error));
    }
    std::filesystem::remove_all(temp);
    std::cout<<"PASS cast, area, school, power and interruption profile bounds\n";
}
