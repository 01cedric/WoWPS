#include "game/local_bots.hpp"
#include "game/local_spell_import.hpp"
#include "game/local_talents.hpp"
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include "local_group_rewards_fixture.hpp"
#include <cassert>
#include <iostream>
using namespace wowee::game;
namespace net=wowee::net;
static uint32_t bits(float f){uint32_t u;std::memcpy(&u,&f,4);return u;}
static std::unique_ptr<wowee::pipeline::DBCFile> dbc(std::vector<std::vector<uint32_t>> rows){
 std::vector<uint8_t> data{'W','D','B','C'};auto u=[&](uint32_t v){for(int i=0;i<4;++i)data.push_back(uint8_t(v>>(i*8)));};
 u(rows.size());u(rows[0].size());u(rows[0].size()*4);u(9);for(auto& row:rows)for(auto v:row)u(v);
 for(char c:std::string("\0Fixture\0",9))data.push_back(c);auto result=std::make_unique<wowee::pipeline::DBCFile>();assert(result->load(data));return result;
}
static void importTest(){
 std::vector<std::vector<uint32_t>> spells,abilities;std::vector<uint32_t> skill(38);skill[0]=6;skill[1]=7;
 for(auto cls:{1,2,3,4,5,6,7,8,9,11}){std::vector<uint32_t> s(234),a(14);s[0]=900000+cls;s[136]=1;s[28]=1;s[39]=1;s[46]=1;s[68]=UINT32_MAX;s[71]=10;s[80]=10;s[86]=25;s[14]=UINT32_MAX;spells.push_back(s);a[0]=cls;a[1]=6;a[2]=s[0];a[4]=1u<<(cls-1);abilities.push_back(a);}
 auto passive=spells[0];passive[0]=910001;passive[4]=64;passive[71]=6;passive[86]=1;passive[95]=34;passive[80]=49;spells.push_back(passive);
 auto rejected=passive;rejected[0]=910002;rejected[95]=107;spells.push_back(rejected);
 std::vector<uint32_t> talent(23),blocked(23),tab(24),range(40);talent[0]=10;talent[1]=1;talent[4]=910001;blocked=talent;blocked[0]=11;blocked[4]=910002;tab[0]=1;tab[20]=1;range[0]=1;range[3]=range[4]=bits(30);
 auto sd=dbc(spells),ad=dbc(abilities),sk=dbc({skill}),td=dbc({talent,blocked}),tabs=dbc({tab}),ranges=dbc({range}),casts=dbc({{1,0,0,0}}),durations=dbc({{1,1000,0,0}});
 auto imported=importClientStarterSpells(sd.get(),ranges.get(),casts.get(),durations.get(),nullptr,ad.get(),sk.get(),td.get());
 detail::importClientTalents(imported,td.get(),tabs.get(),sd.get(),ranges.get(),casts.get(),durations.get(),nullptr,nullptr);
 for(auto cls:{1,2,3,4,5,6,7,8,9,11}){auto f=std::find_if(imported.spells.begin(),imported.spells.end(),[&](auto& s){return s.id==900000u+cls;});assert(f!=imported.spells.end() && f->allowableClasses==(1u<<(cls-1)) && f->unsupportedReason.empty());}
 auto p=std::find_if(imported.spells.begin(),imported.spells.end(),[](auto& s){return s.id==910001;});assert(p!=imported.spells.end() && p->talentId==10 && p->passiveHealth==50 && p->unsupportedReason.empty());
 p=std::find_if(imported.spells.begin(),imported.spells.end(),[](auto& s){return s.id==910002;});assert(p!=imported.spells.end() && !p->unsupportedReason.empty());
 std::cout<<"PASS DBC class/talent import: ten class masks, friendly target25, excluded forms at normal stance, genuine talent joins, passive health and blocked spell modifiers\n";
}
static sockaddr_in loopback(uint16_t p){sockaddr_in a{};initAddress(a);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(p);return a;}
int main(){
 importTest();
 LocalGameplay live;std::string catalogError;assert(live.loadContent("assets/local_realm/world.json",catalogError));
 auto city=rewardPlayer(77);city.x=-8867.9f;city.y=673.6f;city.z=97.9f;
 const auto& sites=localMailboxSites(live.content(),city);assert(!sites.empty());
 for(const auto& m:sites){city.x=m.x;city.y=m.y;city.z=m.z;assert(nearbyLocalMailbox(live.content(),city,m.guid));break;}
 std::cout<<"PASS shipped catalog mailbox lookup: Stormwind streamed inn spawns resolve to reachable local objects\n";
 auto c=rewardContent();c->quests.clear();LocalNpcDefinition inn;inn.id=51;inn.name="Inn";inn.npcFlags=kLocalNpcFlagInnkeeper;c->npcs.push_back(inn);c->spawns.push_back({1,51,0,0,0,0,0});
 auto p=rewardPlayer(1);p.level=80;p.quests.clear();const auto* m=nearbyLocalMailbox(*c,p);assert(m && m->x==3);auto mailbox=*m;
 p.x=8;assert(nearbyLocalMailbox(*c,p,mailbox.guid));p.x=8.01f;assert(!nearbyLocalMailbox(*c,p,mailbox.guid));p.x=0;p.instanceId=1;assert(!nearbyLocalMailbox(*c,p));p.instanceId=0;p.dead=true;assert(!nearbyLocalMailbox(*c,p));p.dead=false;
 std::cout<<"PASS mailbox: deterministic object identity, five-yard 3D range, wrong instance and death rejection\n";
 for(uint8_t rank=1;rank<=5;++rank){LocalSpellDefinition s;s.id=100+rank;s.name="Health talent";s.talentId=10;s.talentTab=1;s.talentRank=rank;s.passive=true;s.passiveHealth=rank*10;s.allowableClasses=1;c->spells.push_back(s);}
 LocalSpellDefinition active;active.id=201;active.name="Active talent";active.talentId=20;active.talentTab=1;active.talentRank=1;active.talentRow=1;active.talentPrerequisites[0]=10;active.talentPrerequisiteRanks[0]=4;active.allowableClasses=1;active.damage=20;active.range=30;c->spells.push_back(active);
 LocalGameplay game;game.useContent(c);std::string status;auto run=[&](LocalAction a,uint32_t id=0,uint32_t rank=0){return game.execute(p,{a,0,id,rank},{&p},status);};
 assert(!run(LocalAction::LearnTalent,20));p.level=9;assert(!run(LocalAction::LearnTalent,10));p.level=80;p.classId=2;assert(!run(LocalAction::LearnTalent,10));p.classId=1;p.attackTarget=1;assert(!run(LocalAction::LearnTalent,10));p.attackTarget=0;
 assert(!run(LocalAction::LearnTalent,10,2));for(uint32_t rank=0;rank<5;++rank)assert(run(LocalAction::LearnTalent,10,rank));assert(p.maxHealth==2125 && localTalentPointsAvailable(p)==66);assert(!run(LocalAction::LearnTalent,10,4));
 assert(run(LocalAction::LearnTalent,20) && std::find(p.knownSpells.begin(),p.knownSpells.end(),201)!=p.knownSpells.end());assert(run(LocalAction::ResetTalents));assert(p.talents.empty() && p.maxHealth==2075 && p.knownSpells==std::vector<uint32_t>{1});
 std::cout<<"PASS talent authority: level/class/combat, exact next rank, tiers/prerequisites, derived health, active spell grant, duplicate rejection and complete reset\n";
 char temp[]="/tmp/wowps-0186-XXXXXX";auto* dir=mkdtemp(temp);assert(dir);LocalRealm host,guest;auto& h=*host.impl_;auto& g=*guest.impl_;h.state=LocalRealmState::Hosting;g.state=LocalRealmState::Connected;h.realmId=g.realmId=123;h.directory=dir;h.self=p;g.self=p;g.self.guid=2;h.gameplay.useContent(c);g.gameplay.useContent(c);h.saved={{{1,11},h.self},{{2,22},g.self}};h.refreshPlayers();g.players=h.players;
 assert(h.openSocket(0) && g.openSocket(0));g.host=loopback(h.port);g.session=987;LocalRealm::Impl::Peer peer;peer.guid=2;peer.identity={2,22};peer.address=loopback(g.port);peer.session=g.session;peer.loading=false;h.peers.push_back(peer);auto& member=h.saved[1].player;
 auto receive=[&]{for(;;){std::array<uint8_t,MaxPacket+1> bytes{};auto n=::recvfrom(g.socket,reinterpret_cast<char*>(bytes.data()),bytes.size(),net::datagramFlags(),nullptr,nullptr);if(n<0){assert(net::isWouldBlock(net::lastError()));break;}Reader r(bytes.data(),n);assert(r.u32()==WireMagic && r.u8()==Version);auto type=Message(r.u8());assert(r.u16()==n);auto seq=r.u32();auto token=r.u64();g.handleClient(type,r,g.host,token,seq);}};
 auto settle=[&]{guest.update(.21f);h.receive();h.history(h.peers[0]);receive();assert(g.pendingCommands.empty());};const auto good=h.directory;
 h.directory+="/missing";assert(guest.learnTalent(10,0));settle();assert(member.talents.empty());h.directory=good;assert(guest.learnTalent(10,0));settle();assert(member.talents==g.self.talents && member.talents.size()==1);
 Writer replay;replay.u32(h.peers[0].lastCommand);replay.u8(uint8_t(LocalAction::LearnTalent));replay.u64(0);replay.u32(10);replay.u32(1);replay.u32(0);replay.u32(0);replay.u64(0);g.send(Message::Command,g.session,replay,g.host);h.receive();receive();assert(member.talents[0].second==1);
 LocalRealm::Impl loaded;loaded.gameplay.useContent(c);assert(loaded.parseSave(good+"/realm.wprs") && loaded.findSaved(2)->player.talents==member.talents);
 h.directory+="/missing";assert(guest.resetTalents());settle();assert(member.talents.size()==1);h.directory=good;assert(guest.resetTalents());settle();assert(member.talents.empty() && g.self.talents.empty());
 std::cout<<"PASS UDP/save17 talents: failed-save rollback, confirmed owner state, altered duplicate ignored, production reload and reset rollback\n";
 LocalMail mail;mail.recipient=2;mail.system=true;mail.senderName="Auction House";mail.subject="Won";mail.items[0]={117,2};assert(h.mailbox.append(mail));assert(h.mailReach(member,mailbox.guid));g.gameplay.useContent(c);
 assert(guest.mailAction(LocalAction::MailTakeItem,mailbox.guid,1,0));settle();assert(member.inventory.size()==1 && member.inventory[0].count==2);assert(guest.mailAction(LocalAction::MailTakeItem,mailbox.guid,1,0));settle();assert(member.inventory[0].count==2);
 std::cout<<"PASS mailbox UDP attachment collection: service object recognized on both peers, auction attachment credited once\n";
 for(uint32_t i=0;i<192;++i){LocalSpellDefinition s;s.id=1000+i;s.name="Spell";c->spells.push_back(s);}member.knownSpells.clear();for(uint32_t i=0;i<192;++i)member.knownSpells.push_back(1000+i);member.knownTaxiNodes.clear();for(uint32_t i=1;i<=512;++i)member.knownTaxiNodes.push_back(i);h.progress(h.peers[0]);receive();assert(g.self.knownSpells.size()==192 && g.self.knownTaxiNodes.size()==512);
 Writer legacy;writeProgress(legacy,p,16);Reader lr(legacy.bytes.data(),legacy.bytes.size());auto old=p;old.talents={{10,1}};assert(readProgress(lr,old,16) && lr.done() && old.talents.empty());
 std::cout<<"PASS progress bounds: 192 spells and 512 flight discoveries via chunk assembly; prior Save16 progress defaults to no talents\n";
 host.stop();guest.stop();std::filesystem::remove_all(dir);
}
