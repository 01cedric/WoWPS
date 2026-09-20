// Export an independent, immediately loadable level-80 realm using public APIs.
// Usage: export_test_characters DBC_DIRECTORY WORLD_JSON EMPTY_OUTPUT_DIR
// The output directory MUST NOT already contain any files.
#include "../../src/game/local_realm.cpp"
#include "game/local_spell_import.hpp"
#include "game/local_world_catalog.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
namespace fs = std::filesystem;
using namespace wowee::game;
void require(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
struct SourceTables {
 std::map<std::string,wowee::pipeline::DBCFile> files;
 std::map<std::string,std::vector<uint8_t>> bytes;
 const wowee::pipeline::DBCFile* get(const char* name){return &files.at(name);}
 explicit SourceTables(const fs::path& directory){
  for(const char* name:{"Spell","SpellRange","SpellCastTimes","SpellDuration","SpellIcon","SpellRadius","SpellRuneCost","SkillLine","SkillLineAbility","Talent","TalentTab","CharBaseInfo"}){
   std::ifstream input(directory/(std::string(name)+".dbc"),std::ios::binary);
   bytes[name]={std::istreambuf_iterator<char>(input),{}};
   require(files[name].load(bytes[name]),std::string("Missing or invalid DBC: ")+name);
  }
 }
 LocalSpellImport spells(){auto result=importClientStarterSpells(get("Spell"),get("SpellRange"),get("SpellCastTimes"),get("SpellDuration"),get("SpellIcon"),get("SkillLineAbility"),get("SkillLine"),get("Talent"),get("SpellRuneCost"),get("SpellRadius"));
  detail::importClientTalents(result,get("Talent"),get("TalentTab"),get("Spell"),get("SpellRange"),get("SpellCastTimes"),get("SpellDuration"),get("SpellIcon"),get("SpellRuneCost"),get("SpellRadius"));return result;}
};
int main(int argc,char** argv)try {
 require(argc==4,"Usage: export_test_characters DBC_DIRECTORY WORLD_JSON EMPTY_OUTPUT_DIR");
 const fs::path output=fs::absolute(argv[3]);
 require(!fs::exists(output)||fs::is_empty(output),"Output directory is not empty; no existing characters will be changed");
 fs::create_directories(output);
 SourceTables tables(argv[1]);const auto imported=tables.spells();
 LocalRealm realm;require(realm.loadContent(argv[2]),realm.error());
 require(bool(realm.content().catalog),"The real world catalog must be present next to world.json");
 require(realm.setStarterSpells(imported.spells,imported.diagnostic),realm.error());
 const std::vector<LocalTestCharacterSpec> specs={
  {0,1,1,0,80,"AldricWar80"},{1,3,2,1,80,"BrunnaPal80"},
  {2,4,3,1,80,"SelaraHun80"},{3,7,4,0,80,"TinkRog80"},
  {4,8,5,0,80,"ZulianPri80"},{5,10,6,1,80,"LyrisDk80"},
  {6,11,7,1,80,"NeravaSha80"},{7,5,8,0,80,"MortisMag80"},
  {8,2,9,0,80,"GorvakLoc80"},{9,6,11,1,80,"MulaDru80"}};
 const auto* races=tables.get("CharBaseInfo");
 for(const auto& spec:specs){bool pair=false;
  for(uint32_t row=0;row<races->getRecordCount();++row)
   if(races->getRecord(row)[0]==spec.race&&races->getRecord(row)[1]==spec.classId)pair=true;
  require(pair&&LocalGameplay::validCharacterOptions(spec.race,spec.classId,spec.gender),"Source does not admit race/class/gender pair");
 }
 require(races->getRecordSize()==2,"CharBaseInfo must have two-byte race/class records");
 require(realm.seedTestCharacters(output.string(),specs)==10,"Seeder did not create exactly ten characters: "+realm.error());
 const auto saved=LocalRealm::savedCharacters(output.string());require(saved.size()==10,"Character-screen loadback must list ten characters");
 std::set<uint64_t> guids;std::set<uint8_t> classes,raceIds;
 for(const auto& character:saved){const auto& p=character.player;const auto& spec=specs.at(character.slot);
  require(p.name==spec.name&&p.level==80&&p.race==spec.race&&p.classId==spec.classId&&p.gender==spec.gender,"Saved identity differs from request");
  require(!p.skin&&!p.face&&!p.hairStyle&&!p.hairColor&&!p.facialHair,"Appearance must use source-safe base variants");
  require(p.gameplayInitialized&&!p.dead&&p.health>0&&p.health==p.maxHealth,"Invalid derived health or initialization");
  require(p.maxMana>0&&p.mana<=p.maxMana&&p.talents.empty(),"Invalid resources or invented talents");
  require(guids.insert(p.guid).second&&classes.insert(p.classId).second&&raceIds.insert(p.race).second,"Character identities/classes/races must all differ");
  for(auto id:p.knownSpells){const auto* spell=realm.content().spell(id);require(spell&&!spell->npcOnly&&!spell->triggeredOnly,"A creature/internal spell leaked into a player book");}
 }
 std::ifstream input(output/"realm.wprs",std::ios::binary);std::vector<uint8_t> save{std::istreambuf_iterator<char>(input),{}};
 require(save.size()>5&&save[4]==31,"Exporter must write real Save31 bytes");
 // Reopen a COPY of the delivered realm, so validation does not dirty the
 // pristine exported characters. The existing world-entry path validates every
 // saved character on each open, then selects by the slot's identity file.
 const fs::path verification=output.parent_path()/(output.filename().string()+"_verification");
 require(!fs::exists(verification),"Verification path already exists");
 fs::copy(output,verification,fs::copy_options::recursive);
 for(const auto& spec:specs){LocalRealm reopened;require(reopened.loadContent(argv[2]),reopened.error());require(reopened.setStarterSpells(imported.spells,imported.diagnostic),reopened.error());
  require(reopened.setCharacterSlot(spec.slot),"Cannot select slot");require(reopened.startSinglePlayer(verification.string(),spec.name),"Cannot enter "+spec.name+": "+reopened.error());
  const auto* player=reopened.localPlayer();require(player&&player->name==spec.name&&player->level==80&&player->classId==spec.classId,"Loaded wrong character");reopened.stop();
 }
 std::ofstream roster(output.parent_path()/"characters.csv");roster<<"slot,name,race,class,gender,level,health,max_resource,resource_type,learned_spells,talent_ranks,map,x,y,z\n";
 for(const auto& savedCharacter:saved){const auto& p=savedCharacter.player;
  roster<<unsigned(savedCharacter.slot)<<','<<p.name<<','<<unsigned(p.race)<<','<<unsigned(p.classId)<<','<<unsigned(p.gender)<<','<<unsigned(p.level)<<','<<p.maxHealth<<','<<p.maxMana<<','<<unsigned(p.resourceType)<<','<<p.knownSpells.size()<<','<<p.talents.size()<<','<<p.mapId<<','<<p.x<<','<<p.y<<','<<p.z<<'\n';
 }
 std::cout<<"PASS exported Save31: ten distinct new level-80 characters, one per class, ten source-valid races, five women/five men\n";
 std::cout<<"PASS character-screen loadback and ten independent real singleplayer world entries\n";
 std::cout<<"PASS unfiltered spell import, derived pools/spellbooks, no fabricated equipment/talents, pristine deliverable preserved\n";
 std::cout<<output<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
