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

static void statAuraTest(){
 auto c=std::make_shared<LocalWorldContent>();
 LocalSpellDefinition d;d.id=70001;d.name="Armor and health";d.clientSpell=true;d.buffHealth=100;d.buffArmor=40;d.durationMs=1000;d.range=30;d.mana=3;d.supercededBySpell=70002;
 c->spells.push_back(d);d.id=70002;d.buffHealth=200;d.buffArmor=80;d.supercededBySpell=0;c->spells.push_back(d);
 LocalGameplay g;g.useContent(c);LocalRealmPlayer p;p.guid=1;p.classId=1;p.race=1;p.level=1;p.health=p.maxHealth=p.mana=p.maxMana=100;p.knownSpells={70001,70002};p.gameplayInitialized=true;
 std::string msg;auto cast=[&](uint32_t id,uint64_t target=0){return g.execute(p,{LocalAction::CastSpell,target,id},{&p},msg);};
 assert(!cast(70001,2)&&p.mana==100);assert(cast(70001)&&p.maxHealth==200&&p.health==100&&p.mana==97&&p.statAuras.size()==1);
 assert(cast(70001)&&p.statAuras.size()==1&&p.maxHealth==200);assert(cast(70002)&&p.statAuras.size()==1&&p.maxHealth==300);
 auto before=p.mana;assert(!cast(70001)&&p.mana==before);assert(localStatAuraBonus(p,*c,true)==80);
 for(int i=0;i<10;++i)g.tick(.1f,{&p});assert(p.statAuras.empty()&&p.maxHealth==100);
 std::cout<<"PASS stat aura authority: self targeting, no free healing, refresh, rank upgrade/downgrade, armor and expiry\n";
 assert(cast(70001));assert(g.execute(p,{LocalAction::CancelStatAura,0,70001},{&p},msg)&&p.statAuras.empty()&&p.maxHealth==100);assert(!g.execute(p,{LocalAction::CancelStatAura,0,70001},{&p},msg));
 assert(cast(70001));p.dead=true;g.tick(.1f,{&p});assert(p.statAuras.empty());p.dead=false;p.health=100;
 assert(cast(70001));p.mapId=1;g.tick(.1f,{&p});assert(p.statAuras.empty()&&p.maxHealth==100);
 p.mapId=0;assert(cast(70001));p.instanceId=1;g.tick(.1f,{&p});assert(p.statAuras.empty());p.instanceId=0;
 std::cout<<"PASS stat aura cleanup: death, map change and instance change remove derived bonuses\n";
 for(uint32_t i=0;i<16;++i)p.statAuras.push_back({80000+i,1000,0,0});before=p.mana;assert(!cast(70001)&&p.mana==before&&p.statAuras.size()==16);p.statAuras.clear();
 assert(cast(70001));Writer w;writeProgress(w,p);Reader r(w.bytes.data(),w.bytes.size());LocalRealmPlayer q=p;q.statAuras.clear();assert(readProgress(r,q)&&r.done()&&q.statAuras==p.statAuras);
 Writer old;writeProgress(old,p,17);Reader ro(old.bytes.data(),old.bytes.size());assert(readProgress(ro,q,17)&&ro.done()&&q.statAuras.empty());
 p.statAuras.push_back(p.statAuras[0]);assert(!validLocalStatAuras(p));p.statAuras.pop_back();p.statAuras[0].remainingMs=3600001;assert(!validLocalStatAuras(p));
 std::cout<<"PASS stat aura bounds/save18: full-list rejection before cost, progress roundtrip, save17 migration and malformed state rejection\n";
 auto dc=rewardContent();dc->spells.clear();for(uint32_t i=0;i<10;++i){LocalSpellDefinition dot;dot.id=60000+i;dot.name="Dot";dot.clientSpell=true;dot.periodicDamage=1;dot.periodicIntervalMs=100;dot.durationMs=1000;dot.range=30;dot.mana=1;dc->spells.push_back(dot);}
 dc->spells[0].supercededBySpell=60009;LocalGameplay dots;dots.useContent(dc);auto caster=rewardPlayer(1);caster.mana=caster.maxMana=100;caster.knownSpells.clear();for(auto& x:dc->spells)caster.knownSpells.push_back(x.id);auto enemy=rewardNpc();enemy.health=enemy.maxHealth=10000;dots.setRemoteNpcs({enemy});
 auto dot=[&](uint32_t id){return dots.execute(caster,{LocalAction::CastSpell,enemy.guid,id},{&caster},msg);};
 for(uint32_t i=0;i<8;++i)assert(dot(60000+i));before=caster.mana;assert(!dot(60008)&&caster.mana==before);assert(dot(60009));before=caster.mana;assert(!dot(60000)&&caster.mana==before);assert(dot(60009));
 std::cout<<"PASS damage auras: eight-effect target bound rejects before spending, higher-rank replacement and refresh at capacity\n";
 std::vector<uint32_t> row(234),range(40);row[0]=990001;row[28]=1;row[40]=1;row[46]=1;row[68]=UINT32_MAX;row[71]=6;row[86]=1;row[80]=39;row[95]=22;row[110]=1;range[0]=1;range[3]=range[4]=bits(30);
 auto sd=dbc({row}),ranges=dbc({range}),casts=dbc({{1,0,0,0}}),durations=dbc({{1,1800000,0,0}});
 detail::ClientSpellTables tables;tables.spells=sd.get();tables.ranges=ranges.get();tables.casts=casts.get();tables.durations=durations.get();
 detail::ClientSpellTables::buildIndex(ranges.get(),tables.rangeIndex);detail::ClientSpellTables::buildIndex(casts.get(),tables.castIndex);detail::ClientSpellTables::buildIndex(durations.get(),tables.durationIndex);
 LocalSpellDefinition decoded;assert(detail::decodeClientSpell(tables,0,decoded)&&decoded.buffArmor==40&&decoded.durationMs==1800000);
 row[35]=2;sd=dbc({row});tables.spells=sd.get();decoded={};assert(!detail::decodeClientSpell(tables,0,decoded));
 row[35]=0;row[31]=1;sd=dbc({row});tables.spells=sd.get();decoded={};assert(!detail::decodeClientSpell(tables,0,decoded));
 row[31]=0;row[86]=21;sd=dbc({row});tables.spells=sd.get();decoded={};assert(detail::decodeClientSpell(tables,0,decoded) && !decoded.buffSelfOnly);
 std::cout<<"PASS aura DBC decoder: fixed self armor and long duration; proc and stacked variants blocked; friendly targets supported\n";
}

static void shieldTest(){
 auto c=rewardContent();c->spells.clear();LocalSpellDefinition d;d.id=72001;d.name="Shield";d.clientSpell=true;d.buffAbsorb=50;d.absorbSchoolMask=1;d.buffSelfOnly=false;d.durationMs=10000;d.range=30;d.mana=5;c->spells.push_back(d);
 LocalGameplay game;game.useContent(c);auto caster=rewardPlayer(1),friendPlayer=rewardPlayer(2);caster.mana=caster.maxMana=100;caster.knownSpells={d.id};std::vector<LocalRealmPlayer*> group{&caster,&friendPlayer};std::string msg;
 auto cast=[&](){return game.execute(caster,{LocalAction::CastSpell,friendPlayer.guid,d.id},group,msg);};
 LocalFactionTemplate ally;ally.id=1;ally.factionGroup=1;ally.enemyGroup=2;LocalFactionTemplate hostile;hostile.id=2;hostile.factionGroup=2;hostile.enemyGroup=1;std::array<uint32_t,12> races{};races[1]=1;races[2]=2;assert(game.setFactionTemplates({ally,hostile},races,msg));friendPlayer.race=2;assert(!cast()&&caster.mana==100);friendPlayer.race=1;
 friendPlayer.x=31;assert(!cast()&&caster.mana==100);friendPlayer.x=0;friendPlayer.dead=true;assert(!cast()&&caster.mana==100);friendPlayer.dead=false;friendPlayer.instanceId=1;assert(!cast());friendPlayer.instanceId=0;
 assert(cast()&&caster.statAuras.empty()&&friendPlayer.statAuras.size()==1&&friendPlayer.statAuras[0].casterGuid==1&&friendPlayer.statAuras[0].absorbRemaining==50);
 assert(localAbsorbDamage(friendPlayer,*c,20,4)==20&&friendPlayer.statAuras[0].absorbRemaining==50);
 assert(localAbsorbDamage(friendPlayer,*c,20,1)==0&&friendPlayer.statAuras[0].absorbRemaining==30);
 Writer w;writeProgress(w,friendPlayer);Reader r(w.bytes.data(),w.bytes.size());auto restored=friendPlayer;restored.statAuras.clear();assert(readProgress(r,restored)&&r.done()&&restored.statAuras==friendPlayer.statAuras);
 assert(localAbsorbDamage(restored,*c,40,1)==10&&restored.statAuras.empty());
 assert(cast()&&friendPlayer.statAuras.size()==1&&friendPlayer.statAuras[0].absorbRemaining==50);
 assert(game.execute(friendPlayer,{LocalAction::CancelStatAura,0,d.id},group,msg)&&friendPlayer.statAuras.empty());
 std::cout<<"PASS friendly shields: living/range/instance validation, caster identity, school filtering, partial consumption, save19 roundtrip, exhaustion, refresh and recipient cancellation\n";
 c->npcs[0].damage=20;auto enemy=rewardNpc();enemy.health=enemy.maxHealth=1000;enemy.targetGuid=friendPlayer.guid;game.setRemoteNpcs({enemy});assert(cast());game.tick(.1f,group);assert(friendPlayer.health==100&&friendPlayer.statAuras[0].absorbRemaining==30);
 std::cout<<"PASS NPC damage integration: armor precedes absorption and a shield prevents health loss\n";
 Writer old;writeProgress(old,friendPlayer,18);Reader oldReader(old.bytes.data(),old.bytes.size());auto legacy=friendPlayer;assert(readProgress(oldReader,legacy,18)&&oldReader.done()&&legacy.statAuras[0].casterGuid==0&&legacy.statAuras[0].absorbRemaining==0);
 friendPlayer.statAuras[0].absorbRemaining=1000001;assert(!validLocalStatAuras(friendPlayer));
 std::cout<<"PASS save18 migration: legacy self caster, no fabricated shield pool, excessive shield capacity rejected\n";
 std::vector<uint32_t> row(234),range(40);row[0]=990002;row[28]=1;row[40]=1;row[46]=1;row[68]=UINT32_MAX;row[71]=6;row[86]=21;row[80]=49;row[95]=69;row[110]=127;range[0]=1;range[3]=bits(2);range[4]=bits(30);
 auto sd=dbc({row}),ranges=dbc({range}),casts=dbc({{1,0,0,0}}),durations=dbc({{1,1800000,0,0}});detail::ClientSpellTables tables;tables.spells=sd.get();tables.ranges=ranges.get();tables.casts=casts.get();tables.durations=durations.get();detail::ClientSpellTables::buildIndex(ranges.get(),tables.rangeIndex);detail::ClientSpellTables::buildIndex(casts.get(),tables.castIndex);detail::ClientSpellTables::buildIndex(durations.get(),tables.durationIndex);
 LocalSpellDefinition decoded;assert(detail::decodeClientSpell(tables,0,decoded)&&decoded.buffAbsorb==50&&decoded.absorbSchoolMask==127&&!decoded.buffSelfOnly&&decoded.range==30);
 row[95]=3;row[98]=1000;sd=dbc({row});tables.spells=sd.get();decoded={};assert(!detail::decodeClientSpell(tables,0,decoded));row[95]=8;sd=dbc({row});tables.spells=sd.get();decoded={};assert(!detail::decodeClientSpell(tables,0,decoded));
 std::cout<<"PASS shield importer: all-school fixed friendly absorb, friendly range column and long periodic damage/healing rejected before training\n";
}

static void healingViewTest(){
 auto c=rewardContent();c->spells.clear();LocalSpellDefinition d;d.id=74001;d.name="Periodic healing";d.clientSpell=true;d.periodicHeal=d.periodicHealMax=7;d.periodicIntervalMs=250;d.durationMs=1000;d.range=30;d.mana=2;c->spells.push_back(d);
 LocalGameplay game;game.useContent(c);auto caster=rewardPlayer(1),target=rewardPlayer(2);caster.knownSpells={d.id};caster.mana=caster.maxMana=100;target.health=10;target.regenerationTimer=-1000;std::vector<LocalRealmPlayer*> group{&caster,&target};std::string msg;
 auto cast=[&](){return game.execute(caster,{LocalAction::CastSpell,target.guid,d.id},group,msg);};
 assert(cast()&&target.healingAuras.size()==1&&target.healingAuras[0].casterGuid==caster.guid&&target.healingAuras[0].remainingMs==1000&&target.health==10);
 game.tick(.25f,group);assert(target.health==17&&target.healingAuras[0].remainingMs==750);
 assert(!game.execute(caster,{LocalAction::CancelStatAura,0,d.id},group,msg));assert(game.execute(target,{LocalAction::CancelStatAura,0,d.id},group,msg)&&target.healingAuras.empty());game.tick(.25f,group);assert(target.health==17);
 assert(cast());for(int i=0;i<4;++i)game.tick(.25f,group);assert(target.healingAuras.empty()&&target.health==45);
 std::cout<<"PASS healing aura lifecycle: immediate display, tick timer, recipient-only cancellation, no cancelled ticks and final expiry tick\n";
 assert(cast());game.tick(.01f,{&target});assert(target.healingAuras.empty());assert(cast());target.instanceId=1;game.tick(.01f,group);assert(target.healingAuras.empty());target.instanceId=0;assert(cast());caster.dead=true;game.tick(.01f,group);assert(target.healingAuras.empty());caster.dead=false;
 std::cout<<"PASS healing aura cleanup: caster disconnect, instance travel and caster death clear display and effect\n";
 assert(cast());Writer w;writeHealingViews(w,target);Reader r(w.bytes.data(),w.bytes.size());auto other=target;other.healingAuras.clear();assert(readHealingViews(r,other)&&r.done()&&other.healingAuras==target.healingAuras);
 Writer savedWith,savedWithout;writeProgress(savedWith,target);other.healingAuras.clear();writeProgress(savedWithout,other);assert(savedWith.bytes==savedWithout.bytes);Reader saveReader(savedWith.bytes.data(),savedWith.bytes.size());assert(readProgress(saveReader,target)&&target.healingAuras.empty());
 other.healingAuras={{d.id,1000,1000,1},{d.id,1000,1000,1}};assert(!validLocalHealingAuraViews(other));other.healingAuras.resize(1);other.healingAuras[0].casterGuid=0;assert(!validLocalHealingAuraViews(other));other.healingAuras[0]={d.id,1001,1000,1};assert(!validLocalHealingAuraViews(other));Writer bad;writeHealingViews(bad,other);Reader br(bad.bytes.data(),bad.bytes.size());assert(!readHealingViews(br,target));
 std::cout<<"PASS transient healing wire: roundtrip, malformed metadata rejection, unchanged Save19 bytes and no persisted display\n";
 std::vector<uint32_t> row(234),range(40);row[0]=990003;row[28]=row[40]=row[46]=1;row[68]=UINT32_MAX;row[71]=6;row[86]=21;row[80]=6;row[95]=8;row[98]=250;row[32]=100;range[0]=1;range[3]=range[4]=bits(30);
 auto sd=dbc({row}),ranges=dbc({range}),casts=dbc({{1,0,0,0}}),durations=dbc({{1,1000,0,0}});detail::ClientSpellTables tables;tables.spells=sd.get();tables.ranges=ranges.get();tables.casts=casts.get();tables.durations=durations.get();detail::ClientSpellTables::buildIndex(ranges.get(),tables.rangeIndex);detail::ClientSpellTables::buildIndex(casts.get(),tables.castIndex);detail::ClientSpellTables::buildIndex(durations.get(),tables.durationIndex);LocalSpellDefinition decoded;assert(detail::decodeClientSpell(tables,0,decoded));
 for(auto column:{31,33,35,116}){auto invalid=row;invalid[column]=2;sd=dbc({invalid});tables.spells=sd.get();decoded={};assert(!detail::decodeClientSpell(tables,0,decoded));}
 std::cout<<"PASS periodic import guards: plain healing with unused proc chance accepted; proc flags, charges, stacking and triggered effects blocked\n";
}
static void mailboxStartTest(){
 LocalWorldContent c;LocalRealmPlayer p;p.mapId=0;p.x=-8943;p.y=-132;p.z=83.6f;
 auto* m=nearbyLocalMailbox(c,p);assert(m&&m->mapId==0);const auto guid=m->guid;
 assert(localMailboxSites(c,p).size()==1&&nearbyLocalMailbox(c,p,guid));p.dead=true;assert(!nearbyLocalMailbox(c,p));p.dead=false;p.instanceId=1;assert(!nearbyLocalMailbox(c,p));p.instanceId=0;
 p.mapId=530;p.x=10345;p.y=-6362;p.z=33.4f;m=nearbyLocalMailbox(c,p);assert(m&&m->guid!=guid);p.z+=10;assert(!nearbyLocalMailbox(c,p));
 std::cout<<"PASS starter mailbox sites: visible-site authority coordinates, map separation, range, death and instance restrictions\n";
}
int main(){mailboxStartTest();healingViewTest();shieldTest();statAuraTest();
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
 std::cout<<"PASS UDP/save19 talents: failed-save rollback, confirmed owner state, altered duplicate ignored, production reload and reset rollback\n";
 LocalMail mail;mail.recipient=2;mail.system=true;mail.senderName="Auction House";mail.subject="Won";mail.items[0]={117,2};assert(h.mailbox.append(mail));assert(h.mailReach(member,mailbox.guid));g.gameplay.useContent(c);
 assert(guest.mailAction(LocalAction::MailTakeItem,mailbox.guid,1,0));settle();assert(member.inventory.size()==1 && member.inventory[0].count==2);assert(guest.mailAction(LocalAction::MailTakeItem,mailbox.guid,1,0));settle();assert(member.inventory[0].count==2);
 std::cout<<"PASS mailbox UDP attachment collection: service object recognized on both peers, auction attachment credited once\n";
 for(uint32_t i=0;i<192;++i){LocalSpellDefinition s;s.id=1000+i;s.name="Spell";c->spells.push_back(s);}member.knownSpells.clear();for(uint32_t i=0;i<192;++i)member.knownSpells.push_back(1000+i);member.knownTaxiNodes.clear();for(uint32_t i=1;i<=512;++i)member.knownTaxiNodes.push_back(i);member.statAuras={{70001,1000,member.mapId,member.instanceId,1,25}};h.progress(h.peers[0]);receive();assert(g.self.statAuras==member.statAuras);assert(h.saveRealm());LocalRealm::Impl buffReload;assert(buffReload.parseSave(good+"/realm.wprs")&&buffReload.findSaved(2)->player.statAuras==member.statAuras);assert(g.self.knownSpells.size()==192 && g.self.knownTaxiNodes.size()==512);
 Writer legacy;writeProgress(legacy,p,16);Reader lr(legacy.bytes.data(),legacy.bytes.size());auto old=p;old.talents={{10,1}};assert(readProgress(lr,old,16) && lr.done() && old.talents.empty());
 std::cout<<"PASS progress bounds: 192 spells and 512 flight discoveries via chunk assembly; prior Save16 progress defaults to no talents\n";
 LocalSpellDefinition shield;shield.id=73001;shield.name="Network shield";shield.clientSpell=true;shield.buffAbsorb=50;shield.absorbSchoolMask=127;shield.buffSelfOnly=false;shield.durationMs=10000;shield.range=30;c->spells.push_back(shield);member.knownSpells[0]=shield.id;h.self.knownSpells.push_back(shield.id);
 assert(guest.castSpell(shield.id,h.self.guid));settle();assert(h.self.statAuras.size()==1&&h.self.statAuras[0].casterGuid==2&&h.self.statAuras[0].absorbRemaining==50);h.self.statAuras[0].absorbRemaining=7;
 Writer shieldReplay;shieldReplay.u32(h.peers[0].lastCommand);shieldReplay.u8(uint8_t(LocalAction::CastSpell));shieldReplay.u64(h.self.guid);shieldReplay.u32(shield.id);shieldReplay.u32(0);shieldReplay.u32(0);shieldReplay.u32(0);shieldReplay.u64(0);g.send(Message::Command,g.session,shieldReplay,g.host);h.receive();receive();assert(h.self.statAuras[0].absorbRemaining==7);
 member.statAuras.clear();assert(host.castSpell(shield.id,2));h.progress(h.peers[0]);receive();assert(member.statAuras==g.self.statAuras&&g.self.statAuras[0].casterGuid==h.self.guid&&g.self.statAuras[0].absorbRemaining==50);assert(h.saveRealm());LocalRealm::Impl shieldLoad;assert(shieldLoad.parseSave(good+"/realm.wprs")&&shieldLoad.findSaved(1)->player.statAuras[0].absorbRemaining==7&&shieldLoad.findSaved(2)->player.statAuras==member.statAuras);
 std::cout<<"PASS UDP friendly shield commands: guest-to-host, duplicate does not refill, host-to-guest owner snapshot and partial-capacity disk reload\n";
 LocalSpellDefinition hot;hot.id=75001;hot.name="Network healing";hot.clientSpell=true;hot.periodicHeal=hot.periodicHealMax=7;hot.periodicIntervalMs=250;hot.durationMs=1000;hot.range=30;c->spells.push_back(hot);h.self.knownSpells.push_back(hot.id);member.health=10;member.regenerationTimer=-1000;
 assert(host.castSpell(hot.id,member.guid));h.progress(h.peers[0]);receive();assert(g.self.healingAuras==member.healingAuras&&g.self.healingAuras.size()==1&&g.self.healingAuras[0].casterGuid==h.self.guid);
 h.gameplay.tick(.25f,h.activePlayers());h.progress(h.peers[0]);receive();assert(g.self.health==17&&g.self.healingAuras[0].remainingMs==750);
 assert(guest.cancelStatAura(hot.id));settle();assert(member.healingAuras.empty()&&g.self.healingAuras.empty());auto hp=member.health;h.gameplay.tick(.25f,h.activePlayers());assert(member.health==hp);
 std::cout<<"PASS UDP healing display: friendly host cast, recipient timer/health snapshot, acknowledged guest cancellation and no later heal\n";
 host.stop();guest.stop();std::filesystem::remove_all(dir);
}
