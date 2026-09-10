#include "game/local_aura_presentation.hpp"
#include "addons/local_framexml.hpp"
#include "addons/local_framexml_lua.hpp"
#include "addons/local_auction_framexml_lua.hpp"
#include "addons/local_merchant_framexml_lua.hpp"
#include "addons/local_services_framexml_lua.hpp"
#include "addons/local_party_framexml_lua.hpp"
#include "addons/local_social_framexml_lua.hpp"
#include <cstdio>
#include "addons/lua_engine.hpp"
#include "game/local_realm.hpp"
#include "game/local_services.hpp"
#include "game/local_mount.hpp"
#include "game/game_handler.hpp"
#include "game/local_quest_dialogue.hpp"
#include "core/logger.hpp"
#include "ui/framexml_takeover.hpp"
#include <algorithm>
#include <cmath>
#include "core/app_clock.hpp"
#ifdef WOWEE_PS4
#include "platform/ps4/input_ps4.hpp"
#endif
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace wowee::addons {
namespace {
void str(lua_State* L,const char* key,const std::string& s){lua_pushlstring(L,s.data(),s.size());lua_setfield(L,-2,key);}
void num(lua_State* L,const char* key,double n){lua_pushnumber(L,n);lua_setfield(L,-2,key);}
void flag(lua_State* L,const char* key,bool b){lua_pushboolean(L,b);lua_setfield(L,-2,key);}
}
std::string LocalFrameXml::itemIcon(uint32_t displayId) const {
    // The injected resolver first, then whatever the Lua bindings registered.
    // Both answer empty for a display id ItemDisplayInfo.dbc does not name, and
    // the shim turns that - and only that - into the question mark.
    if(itemIcon_)return itemIcon_(displayId);
    return ui::frameXmlItemIconPath(displayId);
}
void LocalFrameXml::install(LuaEngine& e,std::function<game::LocalRealm*()> realm,
        std::function<uint64_t()> target,std::function<void()> logout,std::function<void(uint64_t)> greeting,
        std::function<std::string(uint32_t)> itemIcon, game::GameHandler* handler) {
    reset();engine_=&e;realm_=std::move(realm);target_=std::move(target);logout_=std::move(logout);greeting_=std::move(greeting);
    itemIcon_=std::move(itemIcon);handler_=handler;
    if(handler_) handler_->setLocalAuctionRealm(realm_);
    auto* L=e.getState();lua_pushlightuserdata(L,this);lua_pushcclosure(L,command,1);
    lua_setglobal(L,"__WoWPSLocalCommand");
    lua_pushlightuserdata(L,this);lua_pushcclosure(L,socialCommand,1);lua_setglobal(L,"__WoWPSLocalSocialCommand");
    lua_pushlightuserdata(L,this);lua_pushcclosure(L,[](lua_State* state)->int {
        auto* bridge=static_cast<LocalFrameXml*>(lua_touserdata(state,lua_upvalueindex(1)));
        const char* name=luaL_checkstring(state,1);std::vector<std::string> args;
        for(int i=2;i<=lua_gettop(state);++i){const char* value=lua_tostring(state,i);args.emplace_back(value?value:"");}
        if(bridge->engine_)bridge->engine_->fireEvent(name,args);return 0;
    },1);lua_setglobal(L,"__WoWPSLocalSocialEvent");
    lua_pushlightuserdata(L,this);lua_pushcclosure(L,partyCommand,1);lua_setglobal(L,"__WoWPSLocalPartyCommand");
    lua_pushlightuserdata(L,this);lua_pushcclosure(L,[](lua_State* state)->int {
        const char* icon=luaL_optstring(state,1,"");ui::frameXmlSetCursorItem(icon);
        auto* bridge=static_cast<LocalFrameXml*>(lua_touserdata(state,lua_upvalueindex(1)));
        if(bridge->engine_)bridge->engine_->fireEvent("CURSOR_UPDATE");
        return 0;
    },1);lua_setglobal(L,"__WoWPSLocalBankCursorIcon");publish();
    installed_=e.executeSource(kLocalFrameXmlLua,"@WoWPS/LocalFrameXML.lua") &&
        e.executeSource(kLocalMerchantFrameXmlLua,"@WoWPS/LocalMerchantFrameXML.lua") &&
        e.executeSource(kLocalServicesFrameXmlLua,"@WoWPS/LocalServicesFrameXML.lua") &&
        e.executeSource(kLocalPartyFrameXmlLua,"@WoWPS/LocalPartyFrameXML.lua") &&
        e.executeSource(kLocalSocialFrameXmlLua,"@WoWPS/LocalSocialFrameXML.lua");
    if(!installed_)
        LOG_ERROR("[LOCAL_FRAMEXML] API bridge failed: ",e.lastError());
}
void LocalFrameXml::activate(bool enabled) {
    enabled_ = enabled && installed_;
    if (!enabled_ || !engine_) return;
    if (!engine_->executeString("assert(WoWPS_InstallLocalFrameXmlHooks(), 'retail bag/watch functions missing after FrameXML load')")) {
        enabled_ = false;
        LOG_ERROR("[LOCAL_FRAMEXML] post-load hooks failed: ", engine_->lastError());
        return;
    }
    LOG_INFO("[LOCAL_FRAMEXML] post-load bag and quest hooks installed");
}
void LocalFrameXml::reset(){
#ifdef WOWEE_PS4
platform::ps4::setInputMenuNavigation(platform::ps4::MenuOwner::FrameXml,false);
platform::ps4::setInputActionBars(false);
#endif
ui::frameXmlClearPadCursorAnchor();
socialRevision_=UINT64_MAX;partyRevision_=partyRosterRevision_=0;partyInvite_=0;craftSkill_=0;handler_=nullptr;installed_=false;closing_=false;enabled_=false;focus_=0;padFocus_.clear();navigationRoot_=0;phase_=DialoguePhase::None;pendingQuest_=0;pendingTurnIn_=false;missingNpcSeconds_=pendingQuestSeconds_=merchantRefreshSeconds_=0;padCrossHandled_=false;targetShouldersReleased_=false;engine_=nullptr;realm_={};target_={};logout_={};greeting_={};itemIcon_={};changes_={};npc_=lastTarget_=revision_=0;selected_=0;timer_=0;}
bool LocalFrameXml::ready() const {
    if(!ui::frameXmlActive() || !enabled_ || !engine_ || !realm_ || !realm_()) return false;
    auto& tree=engine_->widgets();return tree.findByName("MainMenuBar") && tree.findByName("PlayerFrame");
}
void LocalFrameXml::publish() {
    if(!engine_ || !realm_) return;
    auto* realm=realm_();auto* p=realm?realm->localPlayer():nullptr;
    auto* L=engine_->getState();if(!L)return;
    if(!p){lua_pushnil(L);lua_setglobal(L,"__WoWPSLocal");return;}
    // Change-gated snapshot (at most every 200ms); APIs never copy the world.
    const auto& c=realm->content();const int top=lua_gettop(L);lua_newtable(L);
    snapshotTime_=core::appTimeSeconds();num(L,"time",snapshotTime_);
    str(L,"name",p->name);str(L,"bankOwner",std::to_string(p->guid));str(L,"bankNpc",std::to_string(npc_));num(L,"money",p->money);num(L,"xp",p->xp);num(L,"xpMax",p->xpToLevel);
    num(L,"level",p->level);num(L,"selected",selected_);flag(L,"dead",p->dead);num(L,"mountedSpell",p->mountSpellId);
    num(L,"health",p->health);num(L,"maxHealth",p->maxHealth);num(L,"power",p->mana);num(L,"maxPower",p->maxMana);
    const game::LocalRealmNpc* npc=nullptr;
    for(const auto& n:realm->npcs())if(n.guid==npc_){npc=&n;break;}
    str(L,"npcName",npc?npc->name:"");
    flag(L,"banker",npc && npc->banker);flag(L,"bankOpen",phase_==DialoguePhase::Bank);
    flag(L,"innkeeper",npc && npc->innkeeper);
    flag(L,"classTrainer",npc && npc->classTrainer);flag(L,"trainer",npc && (npc->professionTrainer || npc->classTrainer));flag(L,"tradeTrainer",npc && npc->professionTrainer && npc->trainerSkill!=762);flag(L,"trainerOpen",phase_==DialoguePhase::Trainer);
    num(L,"ridingSkill",p->ridingSkill);flag(L,"onTaxi",p->flight.active);
    flag(L,"flightMaster",npc && npc->flightMaster);
    flag(L,"taxiKnown",npc && std::find(p->knownTaxiNodes.begin(),p->knownTaxiNodes.end(),npc->taxiNodeId)!=p->knownTaxiNodes.end());
    lua_newtable(L);int flightIndex=0;
    if(npc && npc->flightMaster)for(auto id:realm->flightDestinations()){
        const auto* node=realm->travel().node(id);const auto* route=realm->travel().directPath(npc->taxiNodeId,id);
        if(node && route){lua_newtable(L);num(L,"id",id);num(L,"cost",route->cost);str(L,"name",node->name);lua_rawseti(L,-2,++flightIndex);}
    }lua_setfield(L,-2,"flights");
    num(L,"trainerSkill",npc?npc->trainerSkill:0);num(L,"craftSkill",craftSkill_);
    lua_newtable(L);int bankIndex=0;
    for(const auto& slot:p->bank){
        ++bankIndex;if(!slot.itemId)continue;const auto* def=c.item(slot.itemId);if(!def)continue;
        lua_newtable(L);num(L,"id",slot.itemId);num(L,"count",slot.count);num(L,"maxStack",def->stack);str(L,"name",def->name);str(L,"icon",itemIcon(def->displayId));
        lua_rawseti(L,-2,bankIndex);
    }lua_setfield(L,-2,"bank");
    lua_newtable(L);int professionIndex=0;
    for(const auto& skill:p->professions){const auto* line=game::localProfession(realm->skillLines(),skill.skillId);if(!line)continue;
        lua_newtable(L);num(L,"id",skill.skillId);str(L,"name",line->name);num(L,"rank",skill.current);num(L,"max",skill.max);
        flag(L,"primary",line->category==game::kLocalSkillCategoryProfession);lua_rawseti(L,-2,++professionIndex);
    }lua_setfield(L,-2,"professions");
    lua_newtable(L);int recipeIndex=0;
    for(auto id:p->knownRecipes){const auto* recipe=c.recipe(id);if(!recipe || recipe->skillId!=craftSkill_)continue;
        const auto* product=c.item(recipe->createdItemId);lua_newtable(L);num(L,"id",id);str(L,"name",recipe->name);
        num(L,"itemId",recipe->createdItemId);num(L,"made",recipe->createdCount);str(L,"icon",itemIcon(product?product->displayId:0));
        const auto learned=std::find_if(p->professions.begin(),p->professions.end(),[&](const auto& s){return s.skillId==craftSkill_;});
        num(L,"chance",learned!=p->professions.end()?game::localCraftSkillChance(*recipe,learned->current):0);
        str(L,"unavailable",!recipe->unsupportedReason.empty()?recipe->unsupportedReason:
            !game::localRecipeAllows(*recipe,*p)?"This recipe is not available to your race or class":"");
        lua_newtable(L);int toolIndex=0;
        for(auto tool:recipe->tools)if(tool){const auto* definition=c.item(tool);lua_newtable(L);
            str(L,"name",definition?definition->name:"Item #"+std::to_string(tool));
            flag(L,"have",std::any_of(p->inventory.begin(),p->inventory.end(),[&](const auto& stack){return stack.itemId==tool && stack.count;}));
            lua_rawseti(L,-2,++toolIndex);
        }lua_setfield(L,-2,"tools");
        lua_newtable(L);int reagentIndex=0;
        uint32_t available=game::localRecipeAllows(*recipe,*p) && game::localRecipeHasTools(*recipe,*p) &&
            learned!=p->professions.end() && learned->current>=recipe->requiredSkill?20:0;
        for(const auto& reagent:recipe->reagents){const auto* item=c.item(reagent.itemId);
            const uint32_t have=game::localRecipeReagentCount(*recipe,*p,reagent.itemId);
            available=std::min(available,have/reagent.count);lua_newtable(L);str(L,"name",item?item->name:"Item");
            str(L,"icon",itemIcon(item?item->displayId:0));num(L,"need",reagent.count);num(L,"have",have);num(L,"id",reagent.itemId);
            lua_rawseti(L,-2,++reagentIndex);
        }lua_setfield(L,-2,"reagents");num(L,"available",available);lua_rawseti(L,-2,++recipeIndex);
    }lua_setfield(L,-2,"recipes");
    lua_newtable(L);int serviceIndex=0;
    auto service=[&](const char* action,uint32_t id,const std::string& name,uint32_t cost,uint32_t level=0,
                     const std::string& skillName=std::string{},uint32_t skill=0,bool available=true,bool skillMet=true){
        lua_newtable(L);str(L,"action",action);num(L,"id",id);str(L,"name",name);num(L,"cost",cost);
        num(L,"level",level);str(L,"skillName",skillName);num(L,"skill",skill);flag(L,"available",available);flag(L,"skillMet",skillMet);
        lua_rawseti(L,-2,++serviceIndex);
    };
    if(npc && npc->classTrainer)for(auto id:realm->trainableSpells(npc_))if(const auto* spell=c.spell(id)){
        service("learn_spell",id,spell->name,game::localTrainerSpellCost(*spell),spell->baseLevel);
        if(serviceIndex>=256)break;
    }
    if(npc && npc->professionTrainer && npc->trainerSkill==762)for(const auto& rank:game::LocalRidingRanks){
        if(p->ridingSkill<rank.skill)service("train_riding",rank.skill,rank.name,rank.cost,rank.level,"Riding",rank.previous,
            p->ridingSkill==rank.previous && p->level>=rank.level,p->ridingSkill>=rank.previous);
    }
    if(npc && npc->professionTrainer){
        const auto* line=game::localProfession(realm->skillLines(),npc->trainerSkill);
        const auto known=std::find_if(p->professions.begin(),p->professions.end(),[&](const auto& s){return s.skillId==npc->trainerSkill;});
        if(line && known==p->professions.end()){
            const auto& rank=game::localProfessionRanks().front();
            const auto requiredLevel=line->category==game::kLocalSkillCategoryProfession?rank.level:0;
            size_t primaryCount=0;for(const auto& other:p->professions){const auto* d=game::localProfession(realm->skillLines(),other.skillId);if(d && d->category==game::kLocalSkillCategoryProfession)++primaryCount;}
            service("learn_profession",line->id,line->name,rank.cost,requiredLevel,{},0,
                p->level>=requiredLevel && p->professions.size()<game::LocalGameplay::MaxProfessions &&
                (line->category!=game::kLocalSkillCategoryProfession || primaryCount<game::kLocalMaxPrimaryProfessions));
        }
        if(known!=p->professions.end()){
            if(const auto* rank=game::localNextProfessionRank(known->max))service("train_rank",known->skillId,rank->name,rank->cost,rank->level,
                line?line->name:"Skill",known->max>25?known->max-25:0,p->level>=rank->level && known->current+25>=known->max,known->current+25>=known->max);
            for(const auto& recipe:c.recipes)if(recipe.skillId==known->skillId && game::localRecipeAllows(recipe,*p) && c.item(recipe.createdItemId) && recipe.requiredSkill<=known->current && !std::binary_search(p->knownRecipes.begin(),p->knownRecipes.end(),recipe.spellId)) {
                service("learn_recipe",recipe.spellId,recipe.name,game::localRecipeCost(recipe),0,line?line->name:"Skill",recipe.requiredSkill,p->knownRecipes.size()<game::LocalGameplay::MaxRecipes);
                if(serviceIndex>=256)break;
            }
        }
    }lua_setfield(L,-2,"training");
    str(L,"greeting",npc?game::localNpcGreeting(*p,*npc,c):std::string{});
    if (npc && (npc->vendor || npc->repairer)) {
        lua_newtable(L);flag(L,"vendor",npc->vendor);flag(L,"repair",npc->repairer);
        flag(L,"open",phase_==DialoguePhase::Merchant);
        lua_newtable(L);int merchantIndex=0;
        if (phase_==DialoguePhase::Merchant) for (auto itemId : realm->vendorStock(npc_)) {
            const auto* source=c.item(itemId);if(!source)continue;
            // A subsequent catalog read may evict the source definition.
            const auto definition=*source;
            const auto bundle=game::localVendorBuyCount(itemId);
            lua_newtable(L);num(L,"id",itemId);str(L,"name",definition.name);
            str(L,"icon",itemIcon(definition.displayId));num(L,"stack",definition.stack);
            num(L,"bundle",bundle);num(L,"price",game::localVendorBuyPrice(definition,bundle));
            num(L,"remaining",realm->vendorRemaining(itemId,npc_));lua_rawseti(L,-2,++merchantIndex);
        }
        lua_setfield(L,-2,"items");
        lua_newtable(L);int buybackIndex=0;
        const auto buyback=phase_==DialoguePhase::Merchant ? realm->vendorBuyback(npc_) : std::vector<game::LocalMerchantBuyback>{};
        // Retail's compact buyback button uses GetNumBuybackItems() as the
        // newest entry. The durable authority ledger itself is newest first.
        for(auto it=buyback.rbegin();it!=buyback.rend();++it) {
            const auto* source=c.item(it->itemId);if(!source)continue;const auto definition=*source;
            lua_newtable(L);num(L,"id",it->id);num(L,"itemId",it->itemId);num(L,"count",it->count);num(L,"price",it->price);
            str(L,"name",definition.name);str(L,"icon",itemIcon(definition.displayId));lua_rawseti(L,-2,++buybackIndex);
        }
        lua_setfield(L,-2,"buyback");lua_setfield(L,-2,"merchant");
    }
    lua_newtable(L);
    std::vector<game::LocalQuestDefinition> quests;
    for(const auto& progress:p->quests)if(const auto* q=c.quest(progress.id)) quests.push_back(*q);
    if(npc)for(const auto& q:realm->questsForNpc(npc->entry))
        if(game::localQuestOffered(*p,*npc,q) && std::none_of(quests.begin(),quests.end(),[&](const auto& a){return a.id==q.id;}))quests.push_back(q);
    for(const auto& q:quests){
        lua_newtable(L);num(L,"id",q.id);str(L,"title",q.title);str(L,"description",q.description);
        num(L,"level",q.minLevel);num(L,"money",q.money);num(L,"xp",q.xp);
        const auto* progress=game::localQuestProgress(*p,q.id);
        flag(L,"active",progress);flag(L,"complete",progress && progress->status==game::LocalQuestStatus::Complete);
        flag(L,"offered",npc && game::localQuestOffered(*p,*npc,q));
        lua_newtable(L);int i=0;
        for(const auto& o:q.objectives){lua_newtable(L);
            const auto* item=c.item(o.entry);const auto* mob=c.npc(o.entry);
            str(L,"text",o.type==game::LocalQuestObjective::Type::Collect?(item?item->name:"Item"):(mob?mob->name:"Target"));
            str(L,"type",o.type==game::LocalQuestObjective::Type::Collect?"item":"monster");
            num(L,"count",o.count);num(L,"done",progress && size_t(i)<progress->progress.size()?progress->progress[i]:0);
            lua_rawseti(L,-2,++i);
        }lua_setfield(L,-2,"objectives");
        const auto rewardRow=[&](const game::LocalItemStack& reward){
            const auto* item=c.item(reward.itemId);lua_newtable(L);
            num(L,"id",reward.itemId);num(L,"count",reward.count);str(L,"name",item?item->name:"Item");
            str(L,"icon",itemIcon(item?item->displayId:0));
        };
        if(q.rewardItem){rewardRow({q.rewardItem,q.rewardCount});lua_setfield(L,-2,"reward");}
        lua_newtable(L);for(size_t index=0;index<game::localQuestRewardCount(q);++index){
            rewardRow(game::localQuestRewardAt(q,index));lua_rawseti(L,-2,int(index+1));
        }lua_setfield(L,-2,"rewards");
        lua_newtable(L);for(size_t index=0;index<q.rewardChoices.size();++index){
            rewardRow(q.rewardChoices[index]);lua_rawseti(L,-2,int(index+1));
        }lua_setfield(L,-2,"choices");
        lua_rawseti(L,-2,q.id);
    }lua_setfield(L,-2,"quests");
    lua_newtable(L);int i=0;for(const auto& q:p->quests){lua_pushnumber(L,q.id);lua_rawseti(L,-2,++i);}lua_setfield(L,-2,"log");
    const auto bagLayout=game::localInventoryLayout(*p);
    lua_newtable(L);i=0;for(const auto& s:p->inventory){const auto* item=c.item(s.itemId);lua_newtable(L);
        num(L,"id",s.itemId);num(L,"count",s.count);str(L,"name",item?item->name:"Item");num(L,"equip",item?item->inventoryType:0);flag(L,"restores",item && (item->heal || item->mana));
        // The artwork the bag draws. Every stack answered a question mark before
        // this, because the snapshot carried the display id nowhere.
        str(L,"icon",itemIcon(item?item->displayId:0));num(L,"maxStack",item?item->stack:1);
        lua_rawseti(L,-2,bagLayout[i++]+1);}lua_setfield(L,-2,"bags");
    lua_newtable(L);i=0;for(auto id:p->knownSpells){const auto* s=c.spell(id);if(!s)continue;lua_newtable(L);
        num(L,"id",id);str(L,"name",s->name);str(L,"icon",s->iconPath);flag(L,"heal",s->heal!=0);
        num(L,"mountDisplay",s->mountDisplayId);num(L,"mountCreature",s->mountCreatureId);
        flag(L,"usable",s->unsupportedReason.empty());num(L,"cost",s->mana);num(L,"cast",s->castTimeMs);
        uint32_t cooldown=p->globalCooldownMs;for(const auto& cd:p->cooldowns)if(cd.spellId==id)cooldown=std::max(cooldown,cd.remainingMs);
        num(L,"cooldown",cooldown/1000.0);lua_rawseti(L,-2,++i);}lua_setfield(L,-2,"spells");
    const auto& party=realm->partyView();lua_newtable(L);
    num(L,"id",party.partyId);num(L,"inviteId",party.inviteId);
    str(L,"leader",party.members.empty()?"":std::to_string(party.members.front().guid));
    str(L,"inviter",party.inviterName);str(L,"targetGuid",std::to_string(target_?target_():0));
    lua_newtable(L);int partyIndex=0;
    for(const auto& member:party.members){
        lua_newtable(L);str(L,"guid",std::to_string(member.guid));str(L,"name",member.name);
        char unitGuid[24];std::snprintf(unitGuid,sizeof(unitGuid),"0x%016llX",static_cast<unsigned long long>(member.guid));str(L,"unitGuid",unitGuid);
        const bool own=member.guid==p->guid;
        num(L,"health",own?p->health:member.health);num(L,"maxHealth",own?p->maxHealth:member.maxHealth);
        num(L,"power",own?p->mana:member.power);num(L,"maxPower",own?p->maxMana:member.maxPower);
        num(L,"level",member.level);num(L,"class",member.classId);num(L,"race",member.race);num(L,"powerType",member.powerType);
        flag(L,"dead",own?p->dead:member.dead);
        const bool same=member.mapId==p->mapId && member.instanceId==p->instanceId;
        const float dx=member.x-p->x,dy=member.y-p->y,dz=member.z-p->z;
        flag(L,"sameInstance",same);flag(L,"inRange",same && dx*dx+dy*dy+dz*dz<=1600.0f);
        lua_rawseti(L,-2,++partyIndex);
    }lua_setfield(L,-2,"members");lua_setfield(L,-2,"party");
    lua_newtable(L);
    lua_newtable(L);int ignored=0;for(const auto& name:realm->ignoredNames()){lua_pushlstring(L,name.data(),name.size());lua_rawseti(L,-2,++ignored);}lua_setfield(L,-2,"ignores");
    const auto& ready=realm->readyCheck();lua_newtable(L);num(L,"id",ready.id);num(L,"state",ready.state);num(L,"remaining",realm->readyTimeLeft());
    for(const auto& member:ready.members)if(member.guid==ready.initiator)str(L,"initiator",member.name);
    lua_newtable(L);int memberIndex=0;for(const auto& member:ready.members){lua_newtable(L);str(L,"guid",std::to_string(member.guid));str(L,"name",member.name);num(L,"answer",member.answer);lua_rawseti(L,-2,++memberIndex);}lua_setfield(L,-2,"members");lua_setfield(L,-2,"ready");
    const auto& trade=realm->tradeView();const auto own=trade.side(p->guid);lua_newtable(L);
    num(L,"id",trade.id);num(L,"revision",trade.revision);num(L,"state",trade.state);num(L,"side",own);
    if(own>=0){str(L,"peerName",trade.names[1-own]);
        for(unsigned side=0;side<2;++side){const auto source=side?1-own:own;
            num(L,side?"peerMoney":"ownMoney",trade.money[source]);flag(L,side?"peerAccepted":"ownAccepted",trade.accepted[source]);
            lua_newtable(L);for(unsigned slot=0;slot<6;++slot){const auto& item=trade.items[source][slot];lua_newtable(L);num(L,"id",item.item);num(L,"count",item.count);
                if(const auto* def=c.item(item.item)){str(L,"name",def->name);str(L,"icon",itemIcon(def->displayId));}
                if(const auto* meta=game::localAuctionMetadata(item.item))num(L,"quality",meta->quality);
                lua_rawseti(L,-2,int(slot+1));}lua_setfield(L,-2,side?"peerItems":"ownItems");
        }
    }lua_setfield(L,-2,"trade");lua_setfield(L,-2,"social");
    lua_setglobal(L,"__WoWPSLocal");lua_settop(L,top);
}
bool LocalFrameXml::open(uint64_t npc){
    if(!ready())return false;
    auto* r=realm_();const auto* p=r->localPlayer();
    for(const auto& n:r->npcs())if(n.guid==npc && p && game::localNpcInTalkRange(*p,n)){
        focus_=0;navigationRoot_=0;
        if (n.auctioneer && handler_) {
            // Build the real load-on-demand addon before the service event.
            // A harvested MPQ file is not a constructed AuctionFrame, and a
            // generic stub named AuctionFrame_LoadUI must not count as one.
            if (!engine_->widgets().findByName("AuctionFrame")) {
                const bool loaded = engine_->executeString(kLocalAuctionLoadLua);
                if (!loaded || !engine_->widgets().findByName("AuctionFrame")) {
                    LOG_ERROR("[LOCAL_AUCTION] original UI load failed: ", engine_->lastError());
                    engine_->fireEvent("UI_ERROR_MESSAGE", {"The auction interface could not be loaded."});
                    return true; // preserve original UI ownership; no invisible native dialog
                }
            }
            npc_=npc;phase_=DialoguePhase::None;publish();if(greeting_)greeting_(npc);
            handler_->openAuctionHouse(npc);
            if (handler_->isAuctionHouseOpen()) {
                engine_->executeString(kLocalAuctionShowLua);
                LOG_INFO("[LOCAL_AUCTION] original auction window opened guid=", npc,
                         " listings=", r->auctions().size(), " walkingBots=", r->playerbotsEnabled());
            }
            return handler_->isAuctionHouseOpen();
        }
        npc_=npc;selected_=0;phase_=DialoguePhase::Gossip;missingNpcSeconds_=0;pendingQuest_=0;
        if (n.banker && r->questsForNpc(n.entry).empty()) {if(greeting_)greeting_(npc);return act("bank_open",0);}
        if (!n.innkeeper && (n.vendor || n.repairer) && r->questsForNpc(n.entry).empty()) {
            if(greeting_)greeting_(npc);return act("merchant_open",0);
        }
        publish();if(greeting_)greeting_(npc);engine_->fireEvent("GOSSIP_SHOW");return true;}
    return false;
}
void LocalFrameXml::update(float dt){
    if(handler_)handler_->pumpLocalSocial();
    if (!ui::frameXmlActive()) { if (engine_) reset(); return; }
    if(!engine_ || !realm_ || !realm_())return;
    timer_-=dt;if(timer_>0)return;timer_=.2f;
    auto* r=realm_();const auto* p=r->localPlayer();if(!p){
        if(socialRevision_!=r->socialRevision()){socialRevision_=r->socialRevision();publish();engine_->executeString("if WoWPS_LocalSocialUpdate then WoWPS_LocalSocialUpdate() end");}return;}
    if(handler_)handler_->refreshLocalAuctions();
    if(npc_){
        const game::LocalRealmNpc* talker=nullptr;
        for(const auto& n:r->npcs())if(n.guid==npc_){talker=&n;break;}
        // A missing paged NPC snapshot is not a server instruction to close a
        // dialog. A present NPC outside talk range still closes immediately.
        missingNpcSeconds_=talker?0:missingNpcSeconds_+.2f;
        if((talker && !game::localNpcInTalkRange(*p,*talker)) || missingNpcSeconds_>=2.0f){
            LOG_INFO("[LOCAL_QUEST_UI] closing invalid/out-of-range conversation");
            act("close",0);
        }
    }
    if(pendingQuest_){
        pendingQuestSeconds_+=.2f;
        const bool confirmed=pendingTurnIn_
            ? std::binary_search(p->completedQuestIds.begin(),p->completedQuestIds.end(),pendingQuest_)
            : game::localQuestProgress(*p,pendingQuest_)!=nullptr;
        if(confirmed){
            LOG_INFO("[LOCAL_QUEST_UI] host confirmed quest=",pendingQuest_," reward=",pendingTurnIn_);
            pendingQuest_=0;npc_=0;selected_=0;phase_=DialoguePhase::None;closing_=true;
            publish();engine_->fireEvent("QUEST_FINISHED");closing_=false;
        } else if(pendingQuestSeconds_>=10.0f){
            LOG_WARNING("[LOCAL_QUEST_UI] quest not confirmed; dialog retained id=",pendingQuest_);
            pendingQuest_=0; // Host rejection/timeout must allow another action.
        }
    }
    const auto target=target_?target_():0;
    uint32_t hp=0,maxHp=0;
    for(const auto& n:r->npcs()) if(n.guid==target){hp=n.health;maxHp=n.maxHealth;break;}
    for(const auto& n:r->players()) if(n.guid==target){hp=n.health;maxHp=n.maxHealth;break;}
    const unsigned dirty=changes_.observe(*p,target,hp,maxHp);
    merchantRefreshSeconds_+=.2f;
    const bool refreshMerchant=phase_==DialoguePhase::Merchant && merchantRefreshSeconds_>=1.0f;
    const bool partyChanged=partyRevision_!=r->partyRevision();
    const bool socialChanged=socialRevision_!=r->socialRevision();
    if(!dirty && revision_==r->actionStatusRevision() && !refreshMerchant && !partyChanged && !socialChanged) return;
    if(refreshMerchant){merchantRefreshSeconds_=0;r->refreshMerchant(npc_);}
    revision_=r->actionStatusRevision();
    publish();
    if(socialChanged){socialRevision_=r->socialRevision();engine_->executeString("if WoWPS_LocalSocialUpdate then WoWPS_LocalSocialUpdate() end");}
    if(partyChanged) {
        partyRevision_=r->partyRevision();const auto& party=r->partyView();
        if(partyRosterRevision_!=r->partyRosterRevision()) {
            partyRosterRevision_=r->partyRosterRevision();
            engine_->fireEvent("PARTY_MEMBERS_CHANGED");engine_->fireEvent("PARTY_LEADER_CHANGED");
        }
        if(partyInvite_!=party.inviteId) {
            if(partyInvite_)engine_->executeString("if StaticPopup_Hide then StaticPopup_Hide('PARTY_INVITE') end");
            partyInvite_=party.inviteId;
            if(partyInvite_)engine_->fireEvent("PARTY_INVITE_REQUEST",{party.inviterName});
        }
        unsigned index=0;
        for(const auto& m:party.members)if(m.guid!=p->guid){
            const std::string unit="party"+std::to_string(++index);
            engine_->fireEvent("UNIT_NAME_UPDATE",{unit});engine_->fireEvent("UNIT_HEALTH",{unit});engine_->fireEvent("UNIT_MAXHEALTH",{unit});
            engine_->fireEvent("UNIT_DISPLAYPOWER",{unit});
            const char* event=m.powerType==1?"UNIT_RAGE":m.powerType==3?"UNIT_ENERGY":m.powerType==6?"UNIT_RUNIC_POWER":"UNIT_MANA";
            engine_->fireEvent(event,{unit});
            const char* maximum=m.powerType==1?"UNIT_MAXRAGE":m.powerType==3?"UNIT_MAXENERGY":m.powerType==6?"UNIT_MAXRUNIC_POWER":"UNIT_MAXMANA";
            engine_->fireEvent(maximum,{unit});
        }
    }
    if(lastTarget_!=target){lastTarget_=target;engine_->fireEvent("PLAYER_TARGET_CHANGED");}
    using Change=game::LocalUiChanges;
    if((dirty&Change::Travel) && phase_==DialoguePhase::Gossip){
        if(p->flight.active)act("close",0);
        else engine_->fireEvent("GOSSIP_SHOW");
    }
    if(dirty&Change::Life){engine_->fireEvent("UNIT_HEALTH",{"player"});engine_->fireEvent("UNIT_MAXHEALTH",{"player"});}
    if(dirty&Change::Power) {
        engine_->fireEvent("UNIT_DISPLAYPOWER", {"player"});
        const char* valueEvent = "UNIT_MANA";
        const char* maximumEvent = "UNIT_MAXMANA";
        switch(p->resourceType) {
            case game::LocalResourceType::Rage: valueEvent="UNIT_RAGE"; maximumEvent="UNIT_MAXRAGE"; break;
            case game::LocalResourceType::Energy: valueEvent="UNIT_ENERGY"; maximumEvent="UNIT_MAXENERGY"; break;
            case game::LocalResourceType::RunicPower: valueEvent="UNIT_RUNIC_POWER"; maximumEvent="UNIT_MAXRUNIC_POWER"; break;
            default: break;
        }
        engine_->fireEvent(valueEvent, {"player"});
        engine_->fireEvent(maximumEvent, {"player"});
    }
    if(dirty&Change::Target){engine_->fireEvent("UNIT_HEALTH",{"target"});engine_->fireEvent("UNIT_MAXHEALTH",{"target"});}
    if(dirty&Change::Experience) engine_->fireEvent("PLAYER_XP_UPDATE",{"player"});
    if(dirty&Change::Cooldowns) engine_->fireEvent("ACTIONBAR_UPDATE_COOLDOWN");
    if(dirty&Change::Quests) {
        engine_->executeString("if WoWPS_RefreshLocalQuestTracking then WoWPS_RefreshLocalQuestTracking() end");
        engine_->fireEvent("QUEST_LOG_UPDATE");
        engine_->fireEvent("UNIT_QUEST_LOG_CHANGED", {"player"});
    }
    if(dirty&Change::Bags) engine_->fireEvent("BAG_UPDATE",{"0"});
    if(dirty&Change::Money) engine_->fireEvent("PLAYER_MONEY");
    if(phase_==DialoguePhase::Merchant) engine_->fireEvent("MERCHANT_UPDATE");
    if(dirty&Change::Bank){engine_->fireEvent("BAG_UPDATE",{"-1"});for(size_t slot=1;slot<=game::kLocalBankSlots;++slot)engine_->fireEvent("PLAYERBANKSLOTS_CHANGED",{std::to_string(slot)});}
    if(dirty&Change::Professions){
        engine_->fireEvent("SKILL_LINES_CHANGED");
        // ChatFrame compares the second argument with zero, including on the
        // first world snapshot. A profession refresh grants no spendable
        // talent/skill points; match the connected spell handler's payload.
        engine_->fireEvent("CHARACTER_POINTS_CHANGED", {"0", "0"});
    }
    if((dirty&(Change::Bags|Change::Professions)) && craftSkill_)engine_->fireEvent("TRADE_SKILL_UPDATE");
    if(phase_==DialoguePhase::Trainer)engine_->fireEvent("TRAINER_UPDATE");
    if(dirty&Change::Spells){engine_->fireEvent("SPELLS_CHANGED");engine_->fireEvent("ACTIONBAR_SLOT_CHANGED",{"0"});}
    if(dirty&(Change::Mounts|Change::Spells))engine_->fireEvent("COMPANION_UPDATE",{"MOUNT"});

}
int LocalFrameXml::socialCommand(lua_State* L) {
    auto* self=static_cast<LocalFrameXml*>(lua_touserdata(L,lua_upvalueindex(1)));
    const std::string op=luaL_checkstring(L,1);const char* name=luaL_optstring(L,2,"");
    uint32_t values[7]{};for(int i=0;i<7;++i){const double value=luaL_optnumber(L,i+3,0);
        if(!std::isfinite(value) || value<0 || value>UINT32_MAX || std::floor(value)!=value){lua_pushboolean(L,false);return 1;}values[i]=uint32_t(value);}
    bool ok=false;
    try {auto* r=self->realm_?self->realm_():nullptr;if(r && r->ready()){
        if(op=="ready_start")ok=r->startReadyCheck();
        else if(op=="ready_answer" && values[2]<=1){ok=r->answerReadyCheck(values[0],values[2]!=0);if(ok && self->handler_)self->handler_->dismissReadyCheck();}
        else if(op=="ignore_add" || op=="ignore_remove")ok=r->changeIgnore(name,op=="ignore_add");
        else if(op=="trade_request")ok=r->tradeAction(game::LocalAction::TradeRequest,0,0,r->partyPlayerByName(name));
        else {game::LocalAction action{};
            if(op=="trade_open")action=game::LocalAction::TradeOpen;
            else if(op=="trade_offer")action=game::LocalAction::TradeOffer;
            else if(op=="trade_money")action=game::LocalAction::TradeMoney;
            else if(op=="trade_accept")action=game::LocalAction::TradeAccept;
            else if(op=="trade_unaccept")action=game::LocalAction::TradeUnaccept;
            else if(op=="trade_cancel")action=game::LocalAction::TradeCancel;
            if(action>=game::LocalAction::TradeOpen && action<=game::LocalAction::TradeCancel && values[6]<=65535)
                ok=r->tradeAction(action,values[0],values[1],values[2],values[3],values[4],values[5],uint16_t(values[6]));
        }
        self->timer_=0;self->publish();
    }}catch(const std::exception& e){LOG_ERROR("[LOCAL_SOCIAL_UI] command failed: ",e.what());}
    lua_pushboolean(L,ok);return 1;
}
int LocalFrameXml::partyCommand(lua_State* L) {
    auto* self=static_cast<LocalFrameXml*>(lua_touserdata(L,lua_upvalueindex(1)));
    const char* action=luaL_checkstring(L,1);const char* name=luaL_optstring(L,2,"");
    const double token=luaL_optnumber(L,3,0);bool ok=false;
    try {
        auto* realm=self->realm_?self->realm_():nullptr;
        if(realm && realm->ready() && std::isfinite(token) && token>=0 && token<=UINT32_MAX && std::floor(token)==token) {
            const std::string operation=action;
            if(operation=="accept" || operation=="decline")
                ok=realm->partyCommand(operation=="accept"?game::LocalPartyAction::Accept:game::LocalPartyAction::Decline,0,uint32_t(token));
            else if(operation=="leave")ok=realm->partyCommand(game::LocalPartyAction::Leave);
            else {
                const uint64_t guid=realm->partyPlayerByName(name);
                if(operation=="invite")ok=realm->partyCommand(game::LocalPartyAction::Invite,guid);
                else if(operation=="remove")ok=realm->partyCommand(game::LocalPartyAction::Remove,guid);
                else if(operation=="promote")ok=realm->partyCommand(game::LocalPartyAction::Promote,guid);
                else if(operation=="target" && guid && self->handler_) {
                    const auto& party=realm->partyView();
                    if(std::any_of(party.members.begin(),party.members.end(),[&](const auto& m){return m.guid==guid;})){
                        self->handler_->setTarget(guid);ok=true;
                    }
                }
            }
            self->timer_=0;self->publish();
        }
    }catch(const std::exception& e){LOG_ERROR("[LOCAL_PARTY_UI] command failed: ",e.what());}
    lua_pushboolean(L,ok);return 1;
}
int LocalFrameXml::command(lua_State* L){
    auto* self=static_cast<LocalFrameXml*>(lua_touserdata(L,lua_upvalueindex(1)));
    const char* name=luaL_checkstring(L,1);const double id=luaL_optnumber(L,2,0);
    const double count=luaL_optnumber(L,3,0);
    bool ok=false;try{
        const std::string action=name;
        if(action=="bank_move" || action=="bank_deposit_slot" || action=="bank_deposit_from_slot" || action=="bank_withdraw" || action=="bag_move" || action=="bank_withdraw_slot") {
            // Every numeric field is bounded before narrowing. Values describe
            // the UI snapshot, not a silently substituted current slot occupant.
            uint32_t args[7]{};const int fields=(action=="bank_withdraw" || action=="bank_deposit_from_slot")?4:7;
            bool valid=true;
            for(int i=0;i<fields;++i){const double n=luaL_optnumber(L,i+2,0);
                if(!std::isfinite(n)||n<0||n>UINT32_MAX||std::floor(n)!=n){valid=false;break;}args[i]=uint32_t(n);}
            auto* realm=self->realm_?self->realm_():nullptr;
            if(valid && realm && (action=="bag_move" || (self->phase_==DialoguePhase::Bank && self->npc_)) && args[1]>0 && args[1]<=65535) {
                if(action=="bank_deposit_from_slot" && args[3]>0 && args[3]<=65535)ok=realm->depositBankFromSlot(args[0],uint16_t(args[1]),{args[2],uint16_t(args[3])},self->npc_);
                else if((action=="bag_move" || action=="bank_withdraw_slot") && args[5]>0 && args[5]<=65535 && args[6]<=65535)
                    ok=realm->moveBackpackItem(args[0],args[2],uint16_t(args[1]),{args[3],uint16_t(args[5])},{args[4],uint16_t(args[6])},self->npc_,action=="bank_withdraw_slot");
                else if(action=="bank_deposit_slot" && args[5]>0 && args[5]<=65535 && args[6]<=65535)
                    ok=realm->depositBankSlot(args[0],args[2],uint16_t(args[1]),
                        {args[3],uint16_t(args[5])},{args[4],uint16_t(args[6])},self->npc_);
                else if(action=="bank_move" && args[5]<=65535 && args[6]<=65535)
                    ok=realm->moveBankItem(args[0],args[2],uint16_t(args[1]),
                        {args[3],uint16_t(args[5])},{args[4],uint16_t(args[6])},self->npc_);
                else if(action=="bank_withdraw" && args[3]>0 && args[3]<=65535)
                    ok=realm->withdrawBankItem(args[0],uint16_t(args[1]),args[2],self->npc_,uint16_t(args[3]));
                self->timer_=0;self->publish();
            }
        } else if(std::isfinite(id) && id>=0 && id<=UINT32_MAX && std::floor(id)==id &&
                        std::isfinite(count) && count>=0 && count<=65535 && std::floor(count)==count)
        ok=self->act(name,static_cast<uint32_t>(id),static_cast<uint32_t>(count));}
    catch(const std::exception& e){LOG_ERROR("[LOCAL_FRAMEXML] command failed: ",e.what());}
    lua_pushboolean(L,ok);return 1;
}
bool LocalFrameXml::act(const std::string& name,uint32_t id,uint32_t quantity){
    auto* r=realm_?realm_():nullptr;const auto* p=r?r->localPlayer():nullptr;if(!p)return false;
    bool ok=false;
    if(name=="logout"){logout_();return true;}
    if(name=="talents_reset")return r->resetTalents();
    if(name=="mail_attach")return handler_ && handler_->isMailComposeOpen() && id>=1 && id<=24 && handler_->attachItemFromBackpack(int(id-1));
    if(name=="mail_open"){
        if(!handler_ || !r->mailAccess(npc_) || !engine_->widgets().findByName("MailFrame"))return false;
        closing_=true;phase_=DialoguePhase::Mail;publish();engine_->fireEvent("GOSSIP_CLOSED");engine_->fireEvent("QUEST_FINISHED");
        handler_->openMailbox(npc_);closing_=false;return handler_->isMailboxOpen();
    }
    if(name=="bank_open" || name=="trainer_open") {
        const bool bank=name=="bank_open";
        if(!npc_ || (bank?!r->nearbyBanker(npc_):std::none_of(r->npcs().begin(),r->npcs().end(),[&](const auto& n){return n.guid==npc_ && (n.professionTrainer || n.classTrainer) && game::localNpcInTalkRange(*p,n);})))return false;
        if(!bank && !engine_->widgets().findByName("ClassTrainerFrame"))
            if(!engine_->executeString("local ok,why=LoadAddOn('Blizzard_TrainerUI');if not ok then error(tostring(why)) end"))return false;
        if(!engine_->widgets().findByName(bank?"BankFrame":"ClassTrainerFrame"))return false;
        closing_=true;phase_=bank?DialoguePhase::Bank:DialoguePhase::Trainer;publish();
        if(bank)engine_->executeString("if ClearCursor then ClearCursor() end");
        engine_->fireEvent("GOSSIP_CLOSED");engine_->fireEvent("QUEST_FINISHED");
        engine_->fireEvent(bank?"BANKFRAME_OPENED":"TRAINER_SHOW");closing_=false;return true;
    }
    if(name=="bank_close" || name=="trainer_close") {
        if(closing_ || phase_!=(name=="bank_close"?DialoguePhase::Bank:DialoguePhase::Trainer))return true;
        return act("close",0);
    }
    if(name=="taxi_discover" || name=="taxi_fly"){
        if(phase_!=DialoguePhase::Gossip || !npc_)return false;
        const auto* master=r->nearbyFlightMaster();if(!master || master->guid!=npc_)return false;
        ok=name=="taxi_discover"?r->discoverTaxi(npc_):r->takeFlight(id);
        timer_=0;publish();return ok;
    }
    if(name=="craft_open") {
        if(std::none_of(p->professions.begin(),p->professions.end(),[&](const auto& s){return s.skillId==id;}))return false;
        if(!engine_->widgets().findByName("TradeSkillFrame"))
            if(!engine_->executeString("local ok,why=LoadAddOn('Blizzard_TradeSkillUI');if not ok then error(tostring(why)) end"))return false;
        if(!engine_->widgets().findByName("TradeSkillFrame"))return false;
        act("close",0);craftSkill_=id;publish();engine_->fireEvent("TRADE_SKILL_SHOW");return true;
    }
    if(name=="craft_close"){craftSkill_=0;publish();return true;}
    if(name=="train_riding" || name=="bank_deposit" || name=="craft" || name=="unlearn_profession" || name=="learn_profession" || name=="train_rank" || name=="learn_recipe" || name=="learn_spell") {
        if(name=="bank_deposit") {
            if(phase_!=DialoguePhase::Bank || !npc_ || !quantity)return false;
            ok=r->depositBankItem(id,uint16_t(quantity),npc_);
        } else if(name=="craft")ok=r->craftRecipe(id,quantity);
        else if(name=="unlearn_profession")ok=r->unlearnProfession(id);
        else {
            if(phase_!=DialoguePhase::Trainer || !npc_)return false;
            if(name=="train_riding")ok=id<=150 && r->trainRiding(uint16_t(id),npc_);
            else if(name=="learn_spell")ok=r->learnSpell(id,npc_);
            else if(name=="learn_profession")ok=r->learnProfession(id,npc_);
            else if(name=="train_rank")ok=r->trainProfessionRank(id,npc_);
            else ok=r->learnRecipe(id,npc_);
        }
        timer_=0;publish();return ok;
    }
    if(name=="merchant_open") {
        if(!npc_ || (!r->nearbyVendor(npc_) && !r->nearbyRepairer(npc_)))return false;
        if(!engine_->widgets().findByName("MerchantFrame")) {
            engine_->fireEvent("UI_ERROR_MESSAGE",{"The merchant interface could not be loaded."});
            return false;
        }
        closing_=true;phase_=DialoguePhase::Merchant;merchantRefreshSeconds_=0;r->refreshMerchant(npc_);publish();
        engine_->fireEvent("GOSSIP_CLOSED");engine_->fireEvent("QUEST_FINISHED");
        engine_->fireEvent("MERCHANT_SHOW");closing_=false;
        LOG_INFO("[LOCAL_MERCHANT] original window opened npc=",npc_," offers=",r->vendorStock(npc_).size());
        return true;
    }
    if(name=="merchant_close") {
        if(closing_ || phase_!=DialoguePhase::Merchant)return true;
        return act("close",0);
    }
    if(name=="merchant_buy" || name=="merchant_sell" || name=="merchant_buyback" || name=="merchant_repair") {
        if(phase_!=DialoguePhase::Merchant || !npc_)return false;
        if(name=="merchant_repair")ok=r->repairEquipment(npc_);
        else if(name=="merchant_buyback")ok=r->buybackItem(id,npc_);
        else if(!quantity || quantity>65535)return false;
        else if(name=="merchant_buy")ok=r->buyFromVendor(id,uint16_t(quantity),npc_);
        else ok=r->sellToVendor(id,uint16_t(quantity),npc_);
        publish();timer_=0;engine_->fireEvent("MERCHANT_UPDATE");
        return ok;
    }
    if(name=="close_gossip" || name=="close_quest"){
        const bool gossip=name=="close_gossip";
        // A delayed GossipFrame OnHide after QUEST_DETAIL must not cancel the
        // new quest. Likewise Hide(); Show() rebuilds must not cancel themselves.
        if(closing_ || phase_==DialoguePhase::None ||
           (gossip && phase_!=DialoguePhase::Gossip) ||
           (!gossip && (phase_==DialoguePhase::Gossip || phase_==DialoguePhase::Merchant || phase_==DialoguePhase::Bank || phase_==DialoguePhase::Trainer || phase_==DialoguePhase::Mail)))return true;
        const auto* frame=engine_->widgets().findByName(gossip?"GossipFrame":"QuestFrame");
        if(frame && frame->shown)return true;
        return act("close",0);
    }
    if(name=="close"){
        if(closing_ || (!npc_ && !selected_))return true;
        r->refreshMerchant(0);
        closing_=true;
        if(phase_==DialoguePhase::Bank)engine_->executeString("if ClearCursor then ClearCursor() end");
        if(phase_==DialoguePhase::Mail && handler_)handler_->closeMailbox();
        npc_=0;selected_=0;pendingQuest_=0;phase_=DialoguePhase::None;publish();
        engine_->fireEvent("GOSSIP_CLOSED");engine_->fireEvent("QUEST_FINISHED");engine_->fireEvent("MERCHANT_CLOSED");engine_->fireEvent("BANKFRAME_CLOSED");engine_->fireEvent("TRAINER_CLOSED");closing_=false;return true;
    }
    if(name=="detail" || name=="progress" || name=="reward"){
        for(const auto& n:r->npcs())if(n.guid==npc_)for(const auto& q:r->questsForNpc(n.entry))
            if(q.id==id && game::localQuestOffered(*p,n,q)){
                const auto* progress=game::localQuestProgress(*p,id);
                if(name=="reward" && (!progress || progress->status!=game::LocalQuestStatus::Complete))return false;
                selected_=id;pendingQuest_=0;
                // Progress and reward reuse QuestFrame. Re-select the newly
                // shown action even when the outer panel has not changed.
                focus_=0;navigationRoot_=0;
                phase_=name=="detail"?DialoguePhase::Detail:name=="reward"?DialoguePhase::Reward:DialoguePhase::Progress;
                publish();closing_=true;engine_->fireEvent("GOSSIP_CLOSED");
                engine_->fireEvent(name=="detail"?"QUEST_DETAIL":name=="reward"?"QUEST_COMPLETE":"QUEST_PROGRESS");closing_=false;
                LOG_INFO("[LOCAL_QUEST_UI] opened quest=",id," phase=",name," npc=",npc_);return true;
            }return false;
    }
    if((name=="accept" || name=="turnin") && pendingQuest_)return true;
    if(name=="accept")ok=r->acceptQuest(id,npc_);
    else if(name=="turnin")ok=r->turnInQuest(id,npc_,quantity);
    else if(name=="abandon")ok=r->abandonQuest(id);
    else if(name=="interact_or_attack"){
        const auto guid=target_?target_():0;
        for(const auto& n:r->npcs())if(n.guid==guid){
            if(n.dead)return r->loot(guid);
            if(!n.hostile){ok=r->interact(guid);if(ok)open(guid);return ok;}
            break;
        }
        ok=r->attack(guid);
    }
    else if(name=="attack")ok=r->attack(target_());
    else if(name=="stop")ok=r->stopAttack();
    else if(name=="cast"){
        const auto* s=r->content().spell(id);
        if(s)ok=r->castSpell(id,localSpellCommandTarget(*s,*p,target_?target_():0,r->players()));
    }
    else if(name=="cancelcast")ok=r->cancelCast();
    else if(name=="use")ok=r->useItem(id);
    else if(name=="dismount")ok=r->dismount();
    else if(name=="equip")ok=r->equipItem(id);
    else if(name=="respawn")ok=r->respawn();
    else if(name=="save")ok=r->save();
    else if(name=="interact"){ok=r->interact(target_());if(ok)open(target_());}
    if(ok && (name=="accept" || name=="turnin")){
        // On a guest, true means queued, not committed. Leave the details and
        // their NPC identity alive until the host's progress snapshot confirms it.
        pendingQuest_=id;pendingTurnIn_=name=="turnin";pendingQuestSeconds_=0;
    }
    publish();timer_=0; // the dirty-state pass emits only the notifications this action changed
    return ok;
}
}
