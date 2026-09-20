// Real unfiltered import -> source-owned pet casting, never a synthetic spell.
#include "game/local_spell_import.hpp"
#include "game/local_pet.hpp"
#include "game/local_pet_spell.hpp"
#include "game/pet_action.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
using namespace wowee::game;
struct Tables {
 std::map<std::string,wowee::pipeline::DBCFile> files;
 std::map<std::string,std::vector<uint8_t>> bytes;
 const wowee::pipeline::DBCFile* get(const char* n){return &files.at(n);}
 explicit Tables(const char* path){for(const char* n:{"Spell","SpellRange","SpellCastTimes","SpellDuration","SpellIcon","SpellRadius","SpellRuneCost","SkillLine","SkillLineAbility","Talent","TalentTab"}){
  std::ifstream in(std::filesystem::path(path)/(std::string(n)+".dbc"),std::ios::binary);
  bytes[n]={std::istreambuf_iterator<char>(in),{}};assert(files[n].load(bytes[n]));}}
 LocalSpellImport imported(){auto out=importClientStarterSpells(get("Spell"),get("SpellRange"),get("SpellCastTimes"),get("SpellDuration"),get("SpellIcon"),get("SkillLineAbility"),get("SkillLine"),get("Talent"),get("SpellRuneCost"),get("SpellRadius"));
  detail::importClientTalents(out,get("Talent"),get("TalentTab"),get("Spell"),get("SpellRange"),get("SpellCastTimes"),get("SpellDuration"),get("SpellIcon"),get("SpellRuneCost"),get("SpellRadius"));return out;}
};
struct Fixture {
 LocalGameplay game;LocalRealmPlayer owner;std::vector<LocalRealmPlayer*> players;std::string why;
 uint64_t targetGuid=0xF130000000000032ull;
 uint64_t targetEpoch=0;
 explicit Fixture(const std::vector<LocalSpellDefinition>& spells,uint8_t level=20,float targetReach=1.5f){
  auto content=std::make_shared<LocalWorldContent>();LocalNpcDefinition d;d.id=50;d.name="Target";d.level=level;d.health=100000;d.damage=1;d.hostile=true;d.armor=0;d.xp=1;d.combatReach=targetReach;content->npcs={d};LocalNpcSpawn spawn;spawn.id=50;spawn.entry=50;spawn.x=25;content->spawns={spawn};
  game.useContent(content);assert(game.setStarterSpells(spells,"",why));
  owner.x=owner.y=owner.z=0;owner.guid=1;owner.classId=9;owner.race=1;owner.name="Warlock";owner.level=level;owner.health=owner.maxHealth=5000;owner.mana=owner.maxMana=20000;owner.knownSpells={688};owner.gameplayInitialized=true;players={&owner};
  assert(game.execute(owner,{LocalAction::CastSpell,0,688},players,why));
  for(unsigned i=0;i<400&&game.pets().empty();++i)game.tick(.05f,players);assert(game.pets().size()==1);
  auto pets=game.pets();pets[0].react=LocalPetReact::Passive;pets[0].fireboltAutocast=false;game.setRemotePets(pets);
  assert(game.npcs().size()==1&&game.npcs()[0].guid==targetGuid);targetEpoch=game.npcs()[0].combatEpoch;
  auto npcs=game.npcs();npcs[0].attackTimer=1000;game.setRemoteNpcs(npcs);
 }
 void tick(unsigned count){for(unsigned i=0;i<count;++i){game.tick(.05f,players);assert(game.npcs().size()==1&&game.npcs()[0].guid==targetGuid);}}
 uint64_t petGuid()const{return game.pets()[0].guid;}
 uint32_t rank()const{return localPetFireboltSpell(416,owner.level);}
 bool cast(uint32_t id=0,uint64_t guid=0){LocalRealmCommand cmd;cmd.action=LocalAction::PetAction;cmd.target=guid?guid:petGuid();cmd.id=pet::packPetAction(pet::ActionType::Disabled,id?id:rank());cmd.serviceNpcGuid=targetGuid;return game.execute(owner,cmd,players,why);}
 std::vector<LocalCombatEvent> events(LocalCombatEventKind kind)const {auto all=game.combatEvents();std::erase_if(all,[&](const auto& e){return e.spell!=rank()||e.kind!=kind;});return all;}
};
int main(int argc,char** argv){assert(argc==2);Tables tables(argv[1]);auto imported=tables.imported();
 const std::map<uint32_t,uint32_t> expectedRanks={{3110,1},{7799,8},{7800,18},{7801,28},{7802,38},{11762,48},{11763,58},{27267,68},{47964,78}};
 std::map<uint32_t,uint32_t> skillRanks;
 const auto* skill=tables.get("SkillLineAbility");const auto* spellTable=tables.get("Spell");
 for(uint32_t row=0;row<skill->getRecordCount();++row)if(skill->getUInt32(row,1)==188&&expectedRanks.contains(skill->getUInt32(row,2))){
  assert(skill->getUInt32(row,9)==2);skillRanks[skill->getUInt32(row,2)]=0;
 }
 for(uint32_t row=0;row<spellTable->getRecordCount();++row)if(skillRanks.contains(spellTable->getUInt32(row,0)))
  skillRanks[spellTable->getUInt32(row,0)]=spellTable->getUInt32(row,39);
 assert(skillRanks==expectedRanks);
 for(const auto& rank:kLocalPetFireboltRanks){auto it=std::find_if(imported.spells.begin(),imported.spells.end(),[&](const auto& d){return d.id==rank.id;});assert(it!=imported.spells.end()&&it->npcOnly&&!it->allowableClasses&&it->unsupportedReason.empty());assert(it->mana&&it->damage&&it->sourceProjectileSpeed==16.f);}
 Fixture f(imported.spells);assert(!f.cast(47964));assert(!f.cast(f.rank(),123));
 assert(!f.game.execute(f.owner,{LocalAction::CastSpell,f.targetGuid,f.rank()},f.players,f.why));
 LocalRealmCommand toggle;toggle.action=LocalAction::PetSpellAutocast;toggle.target=f.petGuid();toggle.id=f.rank();toggle.bid=2;assert(!f.game.execute(f.owner,toggle,f.players,f.why));toggle.bid=0;assert(f.game.execute(f.owner,toggle,f.players,f.why));toggle.target=0;assert(!f.game.execute(f.owner,toggle,f.players,f.why));
 toggle.target=f.petGuid();f.owner.dead=true;assert(!f.game.execute(f.owner,toggle,f.players,f.why));assert(!f.cast());f.owner.dead=false;
 auto mana=f.game.pets()[0].power;assert(f.cast());assert(!f.cast());f.tick(20);assert(f.game.npcs()[0].combatEpoch==f.targetEpoch&&f.game.npcs()[0].health==100000);assert(f.events(LocalCombatEventKind::SpellCast).empty());assert(f.game.pets()[0].power==mana);
 f.tick(22);assert(f.events(LocalCombatEventKind::SpellCast).size()==1);assert(f.events(LocalCombatEventKind::SpellDamage).empty());assert(f.game.pets()[0].power<mana);
 f.tick(30);assert(f.game.npcs()[0].combatEpoch==f.targetEpoch);auto hits=f.events(LocalCombatEventKind::SpellDamage);assert(hits.size()==1);assert(hits[0].source==f.petGuid()&&hits[0].source!=f.owner.guid&&hits[0].target==f.targetGuid&&hits[0].schoolMask==4);assert(f.events(LocalCombatEventKind::SpellFinish).size()==1);
 std::cout<<"PASS unfiltered import: nine creature-only Firebolt ranks; manual ownership/rank/resource/cast/travel/phase identity\n";
 Fixture cancel(imported.spells);auto power=cancel.game.pets()[0].power;assert(cancel.cast());cancel.tick(10);LocalRealmCommand follow;follow.action=LocalAction::PetAction;follow.target=cancel.petGuid();follow.id=pet::packPetAction(pet::ActionType::Command,pet::kFollow);assert(cancel.game.execute(cancel.owner,follow,cancel.players,cancel.why));cancel.tick(70);assert(cancel.events(LocalCombatEventKind::SpellCast).empty());assert(cancel.game.pets()[0].power==power);
 Fixture empty(imported.spells);auto roster=empty.game.pets();roster[0].power=0;empty.game.setRemotePets(roster);assert(!empty.cast());empty.tick(40);assert(empty.game.pets()[0].power>0&&empty.game.pets()[0].power<=empty.game.pets()[0].maxPower);
 Fixture stale(imported.spells);assert(stale.cast());stale.tick(41);auto enemies=stale.game.npcs();assert(enemies.size()==1);enemies[0].combatEpoch=stale.targetEpoch+1;stale.game.setRemoteNpcs(enemies);stale.tick(20);assert(stale.events(LocalCombatEventKind::SpellDamage).empty());
 std::cout<<"PASS cancellation and negative paths: follow, insufficient mana, target epoch replacement and catalog-spawn continuity\n";
 Fixture boundary(imported.spells,20,10.f);auto distant=boundary.game.npcs();
 const auto petPosition=boundary.game.pets()[0];distant[0].x=petPosition.x+40.75f;distant[0].y=petPosition.y;distant[0].z=petPosition.z;
 boundary.game.setRemoteNpcs(distant);assert(!boundary.cast());
 distant[0].x-=.01f;boundary.game.setRemoteNpcs(distant);assert(boundary.cast());
 distant[0].x+=3.f;boundary.game.setRemoteNpcs(distant);boundary.tick(41);assert(boundary.events(LocalCombatEventKind::SpellCast).size()==1);
 std::cout<<"PASS pet spell range: actual Imp and large-target reaches, strict boundary, completion tolerance\n";
 Fixture automatic(imported.spells);auto pets=automatic.game.pets();pets[0].react=LocalPetReact::Aggressive;pets[0].fireboltAutocast=true;automatic.game.setRemotePets(pets);automatic.tick(160);assert(automatic.events(LocalCombatEventKind::SpellCast).size()>=2);assert(!automatic.events(LocalCombatEventKind::SpellDamage).empty());
 std::cout<<"PASS autocast: rank chosen from actual Imp skill line, repeated authority casts and impacts\n";
}
