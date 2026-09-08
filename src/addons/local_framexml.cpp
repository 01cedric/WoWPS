#include "addons/local_framexml.hpp"
#include "addons/local_framexml_lua.hpp"
#include "addons/local_auction_framexml_lua.hpp"
#include "addons/local_merchant_framexml_lua.hpp"
#include "addons/lua_engine.hpp"
#include "game/local_realm.hpp"
#include "game/local_services.hpp"
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
    lua_setglobal(L,"__WoWPSLocalCommand");publish();
    installed_=e.executeSource(kLocalFrameXmlLua,"@WoWPS/LocalFrameXML.lua") &&
        e.executeSource(kLocalMerchantFrameXmlLua,"@WoWPS/LocalMerchantFrameXML.lua");
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
handler_=nullptr;installed_=false;closing_=false;enabled_=false;focus_=0;padFocus_.clear();navigationRoot_=0;phase_=DialoguePhase::None;pendingQuest_=0;pendingTurnIn_=false;missingNpcSeconds_=pendingQuestSeconds_=merchantRefreshSeconds_=0;padCrossHandled_=false;engine_=nullptr;realm_={};target_={};logout_={};greeting_={};itemIcon_={};changes_={};npc_=lastTarget_=revision_=0;selected_=0;timer_=0;}
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
    str(L,"name",p->name);num(L,"money",p->money);num(L,"xp",p->xp);num(L,"xpMax",p->xpToLevel);
    num(L,"level",p->level);num(L,"selected",selected_);flag(L,"dead",p->dead);num(L,"mountedSpell",p->mountSpellId);
    num(L,"health",p->health);num(L,"maxHealth",p->maxHealth);num(L,"power",p->mana);num(L,"maxPower",p->maxMana);
    const game::LocalRealmNpc* npc=nullptr;
    for(const auto& n:realm->npcs())if(n.guid==npc_){npc=&n;break;}
    str(L,"npcName",npc?npc->name:"");
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
        lua_setfield(L,-2,"items");lua_setfield(L,-2,"merchant");
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
        if(q.rewardItem){const auto* item=c.item(q.rewardItem);lua_newtable(L);
            num(L,"id",q.rewardItem);num(L,"count",q.rewardCount);str(L,"name",item?item->name:"Item");
            str(L,"icon",itemIcon(item?item->displayId:0));lua_setfield(L,-2,"reward");}
        lua_rawseti(L,-2,q.id);
    }lua_setfield(L,-2,"quests");
    lua_newtable(L);int i=0;for(const auto& q:p->quests){lua_pushnumber(L,q.id);lua_rawseti(L,-2,++i);}lua_setfield(L,-2,"log");
    lua_newtable(L);i=0;for(const auto& s:p->inventory){const auto* item=c.item(s.itemId);lua_newtable(L);
        num(L,"id",s.itemId);num(L,"count",s.count);str(L,"name",item?item->name:"Item");num(L,"equip",item?item->inventoryType:0);flag(L,"restores",item && (item->heal || item->mana));
        // The artwork the bag draws. Every stack answered a question mark before
        // this, because the snapshot carried the display id nowhere.
        str(L,"icon",itemIcon(item?item->displayId:0));
        lua_rawseti(L,-2,++i);}lua_setfield(L,-2,"bags");
    lua_newtable(L);i=0;for(auto id:p->knownSpells){const auto* s=c.spell(id);if(!s)continue;lua_newtable(L);
        num(L,"id",id);str(L,"name",s->name);str(L,"icon",s->iconPath);flag(L,"heal",s->heal!=0);
        num(L,"mountDisplay",s->mountDisplayId);num(L,"mountCreature",s->mountCreatureId);
        flag(L,"usable",s->unsupportedReason.empty());num(L,"cost",s->mana);num(L,"cast",s->castTimeMs);
        uint32_t cooldown=p->globalCooldownMs;for(const auto& cd:p->cooldowns)if(cd.spellId==id)cooldown=std::max(cooldown,cd.remainingMs);
        num(L,"cooldown",cooldown/1000.0);lua_rawseti(L,-2,++i);}lua_setfield(L,-2,"spells");
    lua_setglobal(L,"__WoWPSLocal");lua_settop(L,top);
}
bool LocalFrameXml::open(uint64_t npc){
    if(!ready())return false;
    auto* r=realm_();const auto* p=r->localPlayer();
    for(const auto& n:r->npcs())if(n.guid==npc && p && game::localNpcInTalkRange(*p,n)){
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
        if ((n.vendor || n.repairer) && r->questsForNpc(n.entry).empty()) {
            if(greeting_)greeting_(npc);return act("merchant_open",0);
        }
        publish();if(greeting_)greeting_(npc);engine_->fireEvent("GOSSIP_SHOW");return true;}
    return false;
}
void LocalFrameXml::update(float dt){
    if (!ui::frameXmlActive()) { if (engine_) reset(); return; }
    if(!engine_ || !realm_ || !realm_())return;
    timer_-=dt;if(timer_>0)return;timer_=.2f;
    auto* r=realm_();const auto* p=r->localPlayer();if(!p)return;
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
    if(!dirty && revision_==r->actionStatusRevision() && !refreshMerchant) return;
    if(refreshMerchant)merchantRefreshSeconds_=0;
    revision_=r->actionStatusRevision();
    publish();
    if(lastTarget_!=target){lastTarget_=target;engine_->fireEvent("PLAYER_TARGET_CHANGED");}
    using Change=game::LocalUiChanges;
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
    if(dirty&Change::Spells){engine_->fireEvent("SPELLS_CHANGED");engine_->fireEvent("ACTIONBAR_SLOT_CHANGED",{"0"});}
    if(dirty&(Change::Mounts|Change::Spells))engine_->fireEvent("COMPANION_UPDATE",{"MOUNT"});

}
int LocalFrameXml::command(lua_State* L){
    auto* self=static_cast<LocalFrameXml*>(lua_touserdata(L,lua_upvalueindex(1)));
    const char* name=luaL_checkstring(L,1);const double id=luaL_optnumber(L,2,0);
    const double count=luaL_optnumber(L,3,0);
    bool ok=false;try{if(std::isfinite(id) && id>=0 && id<=UINT32_MAX && std::floor(id)==id &&
                        std::isfinite(count) && count>=0 && count<=65535 && std::floor(count)==count)
        ok=self->act(name,static_cast<uint32_t>(id),static_cast<uint32_t>(count));}
    catch(const std::exception& e){LOG_ERROR("[LOCAL_FRAMEXML] command failed: ",e.what());}
    lua_pushboolean(L,ok);return 1;
}
bool LocalFrameXml::act(const std::string& name,uint32_t id,uint32_t quantity){
    auto* r=realm_?realm_():nullptr;const auto* p=r?r->localPlayer():nullptr;if(!p)return false;
    bool ok=false;
    if(name=="logout"){logout_();return true;}
    if(name=="merchant_open") {
        if(!npc_ || (!r->nearbyVendor(npc_) && !r->nearbyRepairer(npc_)))return false;
        if(!engine_->widgets().findByName("MerchantFrame")) {
            engine_->fireEvent("UI_ERROR_MESSAGE",{"The merchant interface could not be loaded."});
            return false;
        }
        closing_=true;phase_=DialoguePhase::Merchant;merchantRefreshSeconds_=0;publish();
        engine_->fireEvent("GOSSIP_CLOSED");engine_->fireEvent("QUEST_FINISHED");
        engine_->fireEvent("MERCHANT_SHOW");closing_=false;
        LOG_INFO("[LOCAL_MERCHANT] original window opened npc=",npc_," offers=",r->vendorStock(npc_).size());
        return true;
    }
    if(name=="merchant_close") {
        if(closing_ || phase_!=DialoguePhase::Merchant)return true;
        return act("close",0);
    }
    if(name=="merchant_buy" || name=="merchant_sell" || name=="merchant_repair") {
        if(phase_!=DialoguePhase::Merchant || !npc_)return false;
        if(name=="merchant_repair")ok=r->repairEquipment(npc_);
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
           (!gossip && (phase_==DialoguePhase::Gossip || phase_==DialoguePhase::Merchant)))return true;
        const auto* frame=engine_->widgets().findByName(gossip?"GossipFrame":"QuestFrame");
        if(frame && frame->shown)return true;
        return act("close",0);
    }
    if(name=="close"){
        if(closing_ || (!npc_ && !selected_))return true;
        closing_=true;npc_=0;selected_=0;pendingQuest_=0;phase_=DialoguePhase::None;publish();
        engine_->fireEvent("GOSSIP_CLOSED");engine_->fireEvent("QUEST_FINISHED");engine_->fireEvent("MERCHANT_CLOSED");closing_=false;return true;
    }
    if(name=="detail" || name=="progress" || name=="reward"){
        for(const auto& n:r->npcs())if(n.guid==npc_)for(const auto& q:r->questsForNpc(n.entry))
            if(q.id==id && game::localQuestOffered(*p,n,q)){
                const auto* progress=game::localQuestProgress(*p,id);
                if(name=="reward" && (!progress || progress->status!=game::LocalQuestStatus::Complete))return false;
                selected_=id;pendingQuest_=0;
                phase_=name=="detail"?DialoguePhase::Detail:name=="reward"?DialoguePhase::Reward:DialoguePhase::Progress;
                publish();closing_=true;engine_->fireEvent("GOSSIP_CLOSED");
                engine_->fireEvent(name=="detail"?"QUEST_DETAIL":name=="reward"?"QUEST_COMPLETE":"QUEST_PROGRESS");closing_=false;
                LOG_INFO("[LOCAL_QUEST_UI] opened quest=",id," phase=",name," npc=",npc_);return true;
            }return false;
    }
    if((name=="accept" || name=="turnin") && pendingQuest_)return true;
    if(name=="accept")ok=r->acceptQuest(id,npc_);
    else if(name=="turnin")ok=r->turnInQuest(id,npc_);
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
    else if(name=="cast"){const auto* s=r->content().spell(id);ok=s && r->castSpell(id,(s->damage || s->periodicDamage)?target_():p->guid);}
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
