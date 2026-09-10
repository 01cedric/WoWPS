#include "game/local_bots.hpp"
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include "game/local_spell_import.hpp"
#include "local_group_rewards_fixture.hpp"
#include <cassert>
#include <iostream>
using namespace wowee::game;
namespace net=wowee::net;
static uint32_t count(const LocalRealmPlayer& p,uint32_t id){uint32_t n=0;for(auto s:p.inventory)if(s.itemId==id)n+=s.count;return n;}
static std::shared_ptr<LocalWorldContent> fixture(){
    auto c=rewardContent();c->quests.clear();c->spells.clear();
    for(uint32_t id:{118,119,5655}){LocalItemDefinition i;i.id=id;i.name="Fixture item";i.stack=20;c->items.push_back(i);}
    LocalSpellDefinition mount;mount.id=6648;mount.name="Horse";mount.clientSpell=true;mount.baseLevel=20;mount.mountDisplayId=2404;mount.mountSpeedPercent=60;mount.range=0;mount.allowableClasses=0x5ff;c->spells.push_back(mount);
    LocalSpellDefinition rank;rank.id=900;rank.name="Class rank";rank.clientSpell=true;rank.baseLevel=1;rank.allowableClasses=1;rank.heal=1;c->spells.insert(c->spells.begin(),rank);
    LocalRecipe recipe;recipe.spellId=300;recipe.skillId=164;recipe.name="Tool-assisted recipe";recipe.createdItemId=118;recipe.createdCount=1;recipe.reagents={{117,2}};recipe.tools={119,0};recipe.trivialLow=50;recipe.trivialHigh=100;recipe.access={{1,1,0,0}};c->recipes={recipe};return c;
}
static LocalRealmPlayer player(uint64_t id){auto p=rewardPlayer(id);p.quests.clear();p.knownSpells.clear();p.level=80;p.ridingSkill=75;p.money=1000000;p.professions={{164,25,75,0}};p.knownRecipes={300};p.inventory={{117,6},{119,1},{5655,1}};return p;}
static LocalRealmNpc trainer(){auto n=rewardNpc(0xf13000000000000aULL);n.hostile=false;n.x=2;n.classTrainer=true;n.trainerClass=1;n.professionTrainer=true;n.trainerSkill=164;return n;}
static void authority(){
    auto c=fixture();LocalGameplay game;game.useContent(c);auto p=player(1);std::vector<LocalRealmPlayer*> ps{&p};std::string result;
    game.setRemoteNpcs({trainer()});
    auto original=p;assert(game.execute(p,{LocalAction::CraftItem,3,300},ps,result));
    assert(count(p,117)==0 && count(p,118)==3 && count(p,119)==1 && p.professions[0].current==28);
    p=original;p.inventory.erase(p.inventory.begin()+1);assert(!game.execute(p,{LocalAction::CraftItem,1,300},ps,result));assert(count(p,117)==6);
    p=original;p.race=2;assert(!game.execute(p,{LocalAction::CraftItem,1,300},ps,result));assert(game.craftableRecipes(p).empty());
    p=original;p.classId=2;assert(!game.execute(p,{LocalAction::CraftItem,1,300},ps,result));assert(game.trainableRecipes(p,trainer().guid).empty());
    p=original;c->recipes[0].access={{0,0,1,0}};assert(!game.execute(p,{LocalAction::CraftItem,1,300},ps,result));
    c->recipes[0].access={{0,0,0,1}};assert(!game.execute(p,{LocalAction::CraftItem,1,300},ps,result));
    c->recipes[0].access.push_back({1,1,0,0});assert(game.execute(p,{LocalAction::CraftItem,1,300},ps,result));
    p=original;c->recipes[0].unsupportedReason="Additional recipe effects are not implemented";
    assert(!game.execute(p,{LocalAction::CraftItem,1,300},ps,result));assert(game.validatePlayer(p,result));
    c->recipes[0].unsupportedReason.clear();c->recipes[0].tools={117,117};
    assert(localRecipeReagentCount(c->recipes[0],p,117)==5);
    assert(!game.execute(p,{LocalAction::CraftItem,3,300},ps,result));assert(count(p,117)==6 && !count(p,118));
    assert(game.execute(p,{LocalAction::CraftItem,2,300},ps,result));assert(count(p,117)==2 && count(p,118)==2);
    p=original;c->recipes[0].tools={119,0};p.inventory[0].count=5;
    assert(!game.execute(p,{LocalAction::CraftItem,3,300},ps,result));assert(count(p,117)==5 && !count(p,118) && p.professions[0].current==25);
    p=original;p.inventory.clear();for(uint32_t i=0;i<24;++i)p.inventory.push_back({i==0?117u:i==1?119u:5655u,20});
    assert(!game.execute(p,{LocalAction::CraftItem,1,300},ps,result));assert(count(p,117)==20);
    auto duplicate=c->recipes;duplicate[0].reagents.push_back({117,1});assert(!game.setRecipes(duplicate,result));
    LocalGameplay g1,g2;g1.useContent(fixture());g2.useContent(fixture());auto recipes=c->recipes;assert(g1.setRecipes(recipes,result));recipes[0].tools={117,0};assert(g2.setRecipes(recipes,result));assert(g1.content().fingerprint!=g2.content().fingerprint);
    p=original;p.knownRecipes.clear();LocalRealmCommand learn{LocalAction::LearnRecipe,0,300};learn.serviceNpcGuid=trainer().guid;
    for(int mode=0;mode<4;++mode){p=original;p.knownRecipes.clear();if(mode==0)p.attackTarget=99;if(mode==1)p.castingSpellId=900;if(mode==2)p.flight.active=true;if(mode==3)p.transportEntry=1;assert(!game.execute(p,learn,ps,result));assert(p.money==original.money && p.knownRecipes.empty());}
    auto wrong=trainer();wrong.guid++;wrong.trainerSkill=171;game.setRemoteNpcs({trainer(),wrong});p=original;p.knownRecipes.clear();learn.serviceNpcGuid=wrong.guid;assert(!game.execute(p,learn,ps,result));learn.serviceNpcGuid=trainer().guid;assert(game.execute(p,learn,ps,result));assert(p.knownRecipes==std::vector<uint32_t>{300} && p.money<original.money);
    p=original;p.ridingSkill=75;p.knownSpells={6648};p.level=19;assert(!game.execute(p,{LocalAction::CastSpell,p.guid,6648},ps,result));p.level=20;
    auto distant=trainer();distant.targetGuid=p.guid;distant.mapId=530;game.setRemoteNpcs({distant});assert(game.execute(p,{LocalAction::CastSpell,p.guid,6648},ps,result));assert(p.mountSpellId==6648);
    assert(game.execute(p,{LocalAction::Dismount},ps,result));c->spells[1].castTimeMs=1000;assert(game.execute(p,{LocalAction::CastSpell,p.guid,6648},ps,result));p.level=19;for(int i=0;i<4;++i)game.tick(.25f,ps);assert(!p.mountSpellId && p.castStatus==LocalCastStatus::Failed);
    std::cout<<"PASS progression authority: recipe access/tools, retained tools, batch rollback, duplicate rejection, fingerprint, selected trainer/state gates, mount level/completion and same-world combat\n";
}
static std::unique_ptr<wowee::pipeline::DBCFile> dbc(const std::vector<std::vector<uint32_t>>& rows,const std::string& strings=std::string("\0Fixture\0",9)){
    std::vector<uint8_t> bytes;auto u=[&](uint32_t v){for(int i=0;i<4;++i)bytes.push_back(uint8_t(v>>(8*i)));};u(0x43424457);u(rows.size());u(rows[0].size());u(rows[0].size()*4);u(strings.size());for(auto& row:rows)for(auto v:row)u(v);bytes.insert(bytes.end(),strings.begin(),strings.end());auto d=std::make_unique<wowee::pipeline::DBCFile>();assert(d->load(bytes));return d;
}
static void importer(){
    std::vector<uint32_t> row(234);row[0]=300;row[71]=24;row[107]=118;row[80]=0;row[52]=row[53]=117;row[60]=2;row[61]=3;row[50]=119;row[136]=1;
    std::vector<uint32_t> skill(38);skill[0]=164;skill[1]=11;
    std::vector<uint32_t> ability(14);ability[0]=1;ability[1]=164;ability[2]=300;ability[3]=1;ability[4]=1;ability[10]=100;ability[11]=50;
    auto lines=dbc({skill}),r=dbc({std::vector<uint32_t>(40)}),casts=dbc({{1,0,0,0}}),durations=dbc({{1,0,0,0}});
    auto run=[&](const std::vector<std::vector<uint32_t>>& abilities){auto spells=dbc({row}),a=dbc(abilities);auto result=importClientStarterSpells(spells.get(),r.get(),casts.get(),durations.get(),nullptr,a.get(),lines.get());assert(result.recipes.size()==1);return result.recipes[0];};
    auto recipe=run({ability});assert(recipe.reagents.size()==1 && recipe.reagents[0].count==5 && recipe.tools[0]==119 && recipe.access[0].races==1 && recipe.access[0].classes==1 && recipe.unsupportedReason.empty());
    auto second=ability;second[0]=2;second[3]=2;second[5]=4;second[6]=8;recipe=run({ability,second});assert(recipe.access.size()==2 && recipe.access[1].excludedRaces==4 && recipe.access[1].excludedClasses==8 && recipe.unsupportedReason.empty());
    second[7]=20;assert(!run({ability,second}).unsupportedReason.empty());
    row[61]=0;assert(!run({ability}).unsupportedReason.empty());row[61]=3;
    row[72]=6;assert(!run({ability}).unsupportedReason.empty());row[72]=24;row[108]=119;assert(!run({ability}).unsupportedReason.empty());row[72]=0;
    row[80]=0x7fffffff;assert(!run({ability}).unsupportedReason.empty());
    std::cout<<"PASS progression importer: actual DBC recipe join, duplicate reagent aggregation, exact tools, alternative/exclusion masks, ambiguous/malformed/mixed-effect and overflow guards\n";
}
static sockaddr_in loopback(uint16_t port){sockaddr_in a{};initAddress(a);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(port);return a;}
static void lanSave(){
    char temp[]="/tmp/wowps-progression-0173-XXXXXX";auto* dir=mkdtemp(temp);assert(dir);
    LocalRealm host,guest;auto& h=*host.impl_;auto& g=*guest.impl_;auto c=fixture();h.gameplay.useContent(c);g.gameplay.useContent(c);
    h.state=LocalRealmState::Hosting;g.state=LocalRealmState::Connected;h.realmId=g.realmId=123;h.directory=dir;h.self=player(1);g.self=player(2);h.saved={{{1,11},h.self},{{2,22},g.self}};
    assert(h.openSocket(0) && g.openSocket(0));g.host=loopback(h.port);g.session=987;
    LocalRealm::Impl::Peer peer;peer.guid=2;peer.identity={2,22};peer.address=loopback(g.port);peer.session=g.session;peer.loading=false;h.peers.push_back(peer);
    h.gameplay.setRemoteNpcs({trainer()});g.gameplay.setRemoteNpcs({trainer()});h.refreshPlayers();g.players=h.players;auto& member=h.saved[1].player;const auto goodDirectory=h.directory;
    auto receive=[&]{for(;;){std::array<uint8_t,MaxPacket+1> b{};auto n=::recvfrom(g.socket,reinterpret_cast<char*>(b.data()),b.size(),net::datagramFlags(),nullptr,nullptr);if(n<0){assert(net::isWouldBlock(net::lastError()));break;}Reader r(b.data(),n);assert(r.u32()==WireMagic && r.u8()==Version);auto type=Message(r.u8());assert(r.u16()==n);auto seq=r.u32();auto token=r.u64();g.handleClient(type,r,g.host,token,seq);}};
    auto command=[&](LocalRealmCommand cmd){assert(guest.command(cmd));guest.update(.21f);h.receive();h.history(h.peers[0]);receive();assert(g.pendingCommands.empty());};
    auto replay=[&](LocalRealmCommand cmd){Writer w;w.u32(h.peers[0].lastCommand);w.u8(uint8_t(cmd.action));w.u64(cmd.target);w.u32(cmd.id);w.u32(0);w.u32(0);w.u32(0);w.u64(cmd.serviceNpcGuid);g.send(Message::Command,g.session,w,g.host);h.receive();receive();};
    LocalRealmCommand mount{LocalAction::UseItem,0,5655};h.directory+="/missing/mount";command(mount);assert(count(member,5655)==1 && member.knownSpells.empty() && count(g.self,5655)==1);
    assert(!host.command(mount) && count(h.self,5655)==1 && h.self.knownSpells.empty());h.directory=goodDirectory;command(mount);assert(!count(member,5655) && member.knownSpells==std::vector<uint32_t>{6648} && g.self.knownSpells==member.knownSpells);replay(mount);assert(member.knownSpells.size()==1);assert(host.command(mount));
    LocalRealmCommand craft{LocalAction::CraftItem,2,300};h.directory+="/missing/craft";command(craft);assert(count(member,117)==6 && !count(member,118) && member.professions[0].current==25);assert(!host.command(craft));h.directory=goodDirectory;command(craft);assert(count(member,117)==2 && count(member,118)==2 && count(member,119)==1 && member.professions[0].current==27);assert(g.self.inventory==member.inventory);replay(craft);assert(count(member,118)==2);assert(host.command(craft));
    LocalRealmCommand train{LocalAction::LearnSpell,0,900};train.serviceNpcGuid=trainer().guid;const auto money=member.money;h.directory+="/missing/train";command(train);assert(member.money==money && member.knownSpells.size()==1);h.directory=goodDirectory;command(train);assert(member.money<money && member.knownSpells.size()==2);replay(train);assert(member.knownSpells.size()==2);assert(host.command(train));
    LocalRealm::Impl loaded;loaded.gameplay.useContent(c);assert(loaded.parseSave(h.directory+"/realm.wprs"));for(uint64_t id:{1,2}){auto* saved=loaded.findSaved(id);assert(saved && saved->player.knownSpells==std::vector<uint32_t>({6648,900}) && count(saved->player,118)==2 && count(saved->player,119)==1 && !count(saved->player,5655) && saved->player.professions[0].current==27);}
    host.stop();guest.stop();std::filesystem::remove_all(dir);
    std::cout<<"PASS progression LAN/save: host and guest atomic mount learning, crafting/tools/skill gain and class training, failed writes, replay and save12 reload\n";
}
int main(){importer();authority();lanSave();}
