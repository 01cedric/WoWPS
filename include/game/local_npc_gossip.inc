// 2.40: creature gossip on the authority (Player::PrepareGossipMenu /
// SendPreparedGossip / OnGossipSelect, ConditionMgr for the gossip
// conditions, SmartAI's GOSSIP_HELLO / GOSSIP_SELECT and the gossip actions).
// Included inside LocalGameplay::Impl after local_npc_smart_motion.inc.
// ------------------------------------------------------------- gossip tables
struct GossipMenuEntry { bool found=false; LocalGossipMenu menu; };
std::map<uint32_t,std::shared_ptr<const GossipMenuEntry>> gossipMenuCache;
std::map<uint32_t,std::shared_ptr<const LocalGossipText>> gossipTextCache;
/// The game events the realm reports active (CONDITION_ACTIVE_EVENT).
std::vector<uint32_t> activeWorldEvents;
uint32_t gossipRevision=0;
/// A gossip menu by id: the content's own table (tests), else the catalog;
/// an unknown id caches as absent so the disk is asked once.
const LocalGossipMenu* gossipMenu(uint32_t menuId) {
    auto it=gossipMenuCache.find(menuId);
    if(it!=gossipMenuCache.end())return it->second->found?&it->second->menu:nullptr;
    if(gossipMenuCache.size()>=512)gossipMenuCache.clear();
    GossipMenuEntry entry;
    if(content) {
        const auto own=content->gossipMenus.find(menuId);
        if(own!=content->gossipMenus.end()){entry.menu=own->second;entry.found=true;}
        else if(content->catalog){std::string error;entry.found=content->catalog->gossipMenu(menuId,entry.menu,error);if(!entry.found&&!error.empty())LOG_WARNING("[LOCAL_GOSSIP] menu ",menuId,": ",error);}
    }
    if(!entry.found)entry.menu={};
    entry.menu.id=menuId;
    auto stored=std::make_shared<const GossipMenuEntry>(std::move(entry));
    gossipMenuCache.emplace(menuId,stored);
    return stored->found?&stored->menu:nullptr;
}
const LocalGossipText* gossipText(uint32_t textId) {
    if(!textId||textId==kLocalGossipDefaultText)return nullptr;
    auto it=gossipTextCache.find(textId);
    if(it!=gossipTextCache.end())return it->second->id?it->second.get():nullptr;
    if(gossipTextCache.size()>=512)gossipTextCache.clear();
    LocalGossipText text;bool found=false;
    if(content) {
        const auto own=content->gossipTexts.find(textId);
        if(own!=content->gossipTexts.end()){text=own->second;found=true;}
        else if(content->catalog){std::string error;found=content->catalog->gossipText(textId,text,error);if(!found&&!error.empty())LOG_WARNING("[LOCAL_GOSSIP] text ",textId,": ",error);}
    }
    if(!found)text={};else text.id=textId;
    auto stored=std::make_shared<const LocalGossipText>(std::move(text));
    gossipTextCache.emplace(textId,stored);
    return found?stored.get():nullptr;
}
bool gossipOwner(uint32_t entry,LocalGossipOwner& out) const {
    out={};
    if(!content)return false;
    const auto own=content->gossipOwners.find(entry);
    if(own!=content->gossipOwners.end()){out=own->second;return true;}
    return content->catalog&&content->catalog->gossipOwner(entry,out);
}
/// creature_template.npcflag as the reference reads it: the gossip table's
/// full flags when it carries the creature, else the service transcription.
uint32_t npcBaseFlags(const LocalNpcDefinition& d) const {
    LocalGossipOwner owner;
    if(gossipOwner(d.id,owner)&&owner.npcFlags)return owner.npcFlags;
    return localEffectiveNpcFlags(d);
}
/// Creature::GetGossipMenuId: the script's override, else the template's.
uint32_t npcGossipMenuId(const LocalRealmNpc& n) const {
    if(n.npcGossipMenuOverridden)return n.npcGossipMenuOverride;
    LocalGossipOwner owner;return gossipOwner(n.entry,owner)?owner.menuId:0u;
}
// ------------------------------------------------------------- conditions
static bool gossipCompare(uint32_t value,uint32_t reference,uint32_t comparison) {
    switch(comparison){case 0:return value==reference;case 1:return value>reference;case 2:return value<reference;case 3:return value>=reference;case 4:return value<=reference;default:return false;}
}
static bool localPlayerHasAura(const LocalRealmPlayer& p,uint32_t spellId) {
    if(!spellId)return false;
    for(const auto& a:p.statAuras)if(a.spellId==spellId)return true;
    for(const auto& a:p.harmfulAuras)if(a.spellId==spellId)return true;
    for(const auto& a:p.healingAuras)if(a.spellId==spellId)return true;
    for(const auto& a:p.areaAuras)if(a.spellId==spellId)return true;
    return p.formSpellId==spellId||p.mountSpellId==spellId;
}
static uint32_t localPlayerItemCount(const LocalRealmPlayer& p,uint32_t itemId,bool bank) {
    uint64_t count=0;
    for(const auto& s:p.inventory)if(s.itemId==itemId)count+=s.count;
    if(bank)for(const auto& s:p.bank)if(s.itemId==itemId)count+=s.count;
    return uint32_t(std::min<uint64_t>(count,UINT32_MAX));
}
/// ConditionMgr::IsPlayerMeetingCondition for the types the catalog carries.
bool gossipConditionHolds(const LocalRealmPlayer& p,const LocalRealmNpc& n,const LocalGossipCondition& c) const {
    bool result=false;
    const auto* progress=localQuestProgress(p,c.value1);
    const bool rewarded=std::binary_search(p.completedQuestIds.begin(),p.completedQuestIds.end(),c.value1)||(progress&&progress->status==LocalQuestStatus::Rewarded);
    switch(c.type) {
        case 1:result=localPlayerHasAura(p,c.value1);break;
        case 2:result=localPlayerItemCount(p,c.value1,c.value3!=0)>=std::max<uint32_t>(1,c.value2);break;
        case 3:result=std::find(p.equipment.begin(),p.equipment.end(),c.value1)!=p.equipment.end()&&c.value1;break;
        case 5:result=(c.value2&(1u<<localReputationRank(p,c.value1)))!=0;break;
        case 6:{const bool alliance=p.race==1||p.race==3||p.race==4||p.race==7||p.race==11;result=c.value1==469?alliance:c.value1==67?!alliance:false;break;}
        case 7:result=std::any_of(p.professions.begin(),p.professions.end(),[&](const auto& s){return s.skillId==c.value1&&s.current>=c.value2;});break;
        case 8:result=rewarded;break;
        case 9:result=progress&&progress->status==LocalQuestStatus::Active;break;
        case 12:result=std::find(activeWorldEvents.begin(),activeWorldEvents.end(),c.value1)!=activeWorldEvents.end();break;
        case 14:result=!progress&&!rewarded;break;
        case 15:result=p.classId>=1&&p.classId<=32&&(c.value1&(1u<<(p.classId-1)));break;
        case 16:result=p.race>=1&&p.race<=32&&(c.value1&(1u<<(p.race-1)));break;
        case 20:result=p.gender==c.value1;break;
        case 25:result=std::find(p.knownSpells.begin(),p.knownSpells.end(),c.value1)!=p.knownSpells.end();break;
        case 27:result=gossipCompare(p.level,c.value1,c.value2);break;
        case 28:result=progress&&progress->status==LocalQuestStatus::Complete;break;
        case 29:{const float range=float(std::min<uint32_t>(c.value2,1000));const bool alive=c.value3==0;
            result=std::any_of(npcs.begin(),npcs.end(),[&](const auto& m){return m.entry==c.value1&&m.mapId==p.mapId&&m.instanceId==p.instanceId&&m.dead!=alive&&!m.npcUnsummoned&&distance2(p,m)<=range*range;});break;}
        case 47:{uint32_t state=0;
            if(rewarded)state|=64;else if(!progress)state|=1;else if(progress->status==LocalQuestStatus::Complete)state|=2;else state|=8;
            result=(c.value2&state)!=0;break;}
        default:result=false;break;
    }
    (void)n;
    return c.negative?!result:result;
}
/// ConditionMgr::IsObjectMeetToConditions: the conditions of one else group
/// must all hold; any else group holding satisfies the list; an empty list holds.
bool gossipConditionsHold(const LocalRealmPlayer& p,const LocalRealmNpc& n,const std::vector<LocalGossipCondition>& list) const {
    if(list.empty())return true;
    std::array<uint8_t,256> seen{},failed{};
    for(const auto& c:list){seen[c.elseGroup]=1;if(!gossipConditionHolds(p,n,c))failed[c.elseGroup]=1;}
    for(size_t g=0;g<seen.size();++g)if(seen[g]&&!failed[g])return true;
    return false;
}
// ------------------------------------------------------------- the page
void gossipTouch(LocalRealmPlayer& p){p.gossip.revision=++gossipRevision;}
void gossipClose(LocalRealmPlayer& p) {
    if(!p.gossip.open()&&p.gossip.options.empty()&&!p.gossip.offeredQuestId)return;
    p.gossip=LocalGossipState{};gossipTouch(p);
}
/// Player::PrepareGossipMenu(source, menuId, showQuests): the options of the
/// menu (menu 0's when the creature's own default menu has none) that pass
/// their conditions, the creature's npcflag and the type's own rule; the
/// quest list flag when asked and the creature gives quests.
void gossipPrepare(LocalRealmPlayer& p,const LocalRealmNpc& n,uint32_t menuId,bool showQuests) {
    p.gossip.npcGuid=n.guid;p.gossip.menuId=menuId;p.gossip.options.clear();p.gossip.offeredQuestId=0;p.gossip.questMenu=false;
    const uint32_t npcflags=npcEffectiveFlags(n);
    const LocalGossipMenu* menu=gossipMenu(menuId);
    if((!menu||menu->options.empty())&&menuId==npcGossipMenuId(n))menu=gossipMenu(0);
    if(showQuests&&n.questGiver)p.gossip.questMenu=true;
    if(!menu)return;
    for(const auto& o:menu->options) {
        if(p.gossip.options.size()>=kLocalMaxGossipOptions)break;
        if(!gossipConditionsHold(p,n,o.conditions))continue;
        if(!(o.npcFlag&npcflags))continue;
        bool canTalk=true;
        switch(o.type) {
            case 1:break;                                                   // GOSSIP
            case 2:canTalk=false;break;                                     // QUESTGIVER: the quest list, never an option
            case 3:canTalk=n.vendor;break;                                  // VENDOR: an empty stock shows nothing
            case 4:canTalk=n.flightMaster;break;                            // TAXIVENDOR
            case 5:canTalk=(n.classTrainer&&n.trainerClass)||(n.professionTrainer&&n.trainerSkill);break; // TRAINER
            case 8:canTalk=n.innkeeper;break;                               // INNKEEPER
            case 9:canTalk=n.banker;break;                                  // BANKER
            case 13:canTalk=n.auctioneer;break;                             // AUCTIONEER
            case 14:canTalk=p.classId==3;break;                             // STABLEPET: hunters
            case 16:canTalk=n.classTrainer&&n.trainerClass==p.classId;break;// UNLEARNTALENTS: CanResetTalents
            default:canTalk=false;break;
        }
        if(!canTalk)continue;
        LocalGossipShownOption shown;shown.id=o.id;shown.icon=o.icon;shown.type=o.type;shown.actionMenuId=o.actionMenuId;shown.boxMoney=o.boxMoney;shown.text=o.text;shown.boxText=o.boxText;
        p.gossip.options.push_back(std::move(shown));
    }
}
/// Player::GetGossipTextId(menuId): the last gossip_menu text whose
/// conditions hold, else DEFAULT_GOSSIP_MESSAGE.
uint32_t gossipTextId(const LocalRealmPlayer& p,const LocalRealmNpc& n,uint32_t menuId) {
    uint32_t textId=kLocalGossipDefaultText;
    if(!menuId)return textId;
    const auto* menu=gossipMenu(menuId);if(!menu)return textId;
    for(const auto& t:menu->texts)if(gossipConditionsHold(p,n,t.conditions))textId=t.textId;
    return textId;
}
/// Player::SendPreparedGossip: a creature without UNIT_NPC_FLAG_GOSSIP whose
/// quest list is not empty shows the quest list; otherwise the menu's text.
void gossipSend(LocalRealmPlayer& p,const LocalRealmNpc& n) {
    const uint32_t npcflags=npcEffectiveFlags(n);
    if(!(npcflags&0x1u)&&p.gossip.questMenu){p.gossip.options.clear();p.gossip.textId=kLocalGossipDefaultText;gossipTouch(p);return;}
    p.gossip.textId=gossipTextId(p,n,p.gossip.menuId);
    if(!(npcflags&0x1u))p.gossip.questMenu=false;
    gossipTouch(p);
}
/// WorldSession::HandleGossipHelloOpcode: the default menu with the quests,
/// sent, then SmartAI's GOSSIP_HELLO rows (SEND_GOSSIP_MENU replaces the
/// page, CLOSE_GOSSIP ends it).
void gossipHello(LocalRealmPlayer& p,LocalRealmNpc& n,const std::vector<LocalRealmPlayer*>& players) {
    gossipPrepare(p,n,npcGossipMenuId(n),true);
    gossipSend(p,n);
    LOG_INFO("[LOCAL_GOSSIP] hello player=",p.guid," npc=",n.guid," entry=",n.entry," menu=",p.gossip.menuId," text=",p.gossip.textId," options=",p.gossip.options.size()," quests=",p.gossip.questMenu);
    smartFireEvents(n,kLocalSmartEventGossipHello,p.guid,players);
}
/// HandleGossipSelectOptionOpcode: SmartAI's GOSSIP_SELECT rows first
/// (sGossipSelect), then Player::OnGossipSelect unless the rows closed or
/// replaced the page: the action menu, the quest list, the box price.
bool gossipSelect(LocalRealmPlayer& p,LocalRealmNpc& n,uint32_t menuId,uint32_t optionId,const std::vector<LocalRealmPlayer*>& players,std::string& error) {
    if(!p.gossip.open()||p.gossip.npcGuid!=n.guid){error="No conversation with this character";return false;}
    if(p.gossip.menuId!=menuId){error="That menu is no longer shown";return false;}
    const auto it=std::find_if(p.gossip.options.begin(),p.gossip.options.end(),[&](const auto& o){return o.id==optionId;});
    if(it==p.gossip.options.end()){error="That option is not offered";return false;}
    const LocalGossipShownOption option=*it;
    if(p.money<option.boxMoney){error="Not enough money";gossipClose(p);return false;}
    const uint32_t revisionBefore=p.gossip.revision;
    smartFireEvents(n,kLocalSmartEventGossipSelect,p.guid,players,menuId,optionId);
    LOG_INFO("[LOCAL_GOSSIP] select player=",p.guid," npc=",n.guid," menu=",menuId," option=",optionId," type=",int(option.type));
    // OnGossipSelect: "if not same, then something funky is going on".
    if(p.gossip.open()&&p.gossip.npcGuid==n.guid&&p.gossip.menuId==menuId&&p.gossip.revision==revisionBefore) {
        switch(option.type) {
            case 1:                                                         // GOSSIP: the action menu
                if(option.actionMenuId){gossipPrepare(p,n,option.actionMenuId,false);gossipSend(p,n);}
                break;
            case 2:p.gossip.questMenu=true;p.gossip.options.clear();gossipTouch(p);break; // QUESTGIVER: the quest list
            case 16:gossipClose(p);break;                                   // UNLEARNTALENTS: SendCloseGossip + the confirm
            default:break;                                                  // the services open on the client
        }
    }
    if(option.boxMoney){p.money-=option.boxMoney;LOG_INFO("[LOCAL_GOSSIP] box price player=",p.guid," copper=",option.boxMoney);}
    return true;
}
/// The page ends when the creature is no longer there to talk to.
void gossipTick(LocalRealmPlayer& p) {
    if(!p.gossip.open())return;
    const auto* n=npc(p.gossip.npcGuid);
    if(!n||n->dead||n->npcUnsummoned||n->npcDespawned||n->npcHidden||n->scriptActorRetired||npcInvisible(*n)||!localNpcInTalkRange(p,*n)||
       !localPhaseVisible(p.phaseMask,n->requiredPhaseMask,n->excludedPhaseMask))gossipClose(p);
}
/// SMART_ACTION_SEND_GOSSIP_MENU (menuId, npcTextId) at a player target.
void gossipSendMenu(LocalRealmPlayer& p,const LocalRealmNpc& n,uint32_t menuId,uint32_t textId) {
    if(menuId)gossipPrepare(p,n,menuId,true);
    else {p.gossip.npcGuid=n.guid;p.gossip.menuId=0;p.gossip.options.clear();p.gossip.questMenu=false;p.gossip.offeredQuestId=0;}
    p.gossip.textId=textId?textId:gossipTextId(p,n,menuId);
    gossipTouch(p);
    LOG_INFO("[LOCAL_GOSSIP] script menu player=",p.guid," npc=",n.guid," menu=",menuId," text=",p.gossip.textId," options=",p.gossip.options.size());
}
/// SMART_ACTION_OFFER_QUEST: directAdd puts the quest in the log at once
/// (AddQuestAndCheckCompletion), else the details page opens for this creature.
void gossipOfferQuest(LocalRealmPlayer& p,const LocalRealmNpc& n,uint32_t questId,bool directAdd) {
    const auto* def=content?content->quest(questId):nullptr;if(!def)return;
    if(localQuestProgress(p,questId)||std::binary_search(p.completedQuestIds.begin(),p.completedQuestIds.end(),questId))return;
    if(directAdd) {
        if(localQuestAcceptanceError(p,*def))return;
        LocalQuestProgress q;q.id=questId;q.progress.resize(def->objectives.size(),0);p.quests.push_back(std::move(q));
        questStatus(p,*content);
        LOG_INFO("[LOCAL_GOSSIP] quest added player=",p.guid," quest=",questId," by npc=",n.guid);
        return;
    }
    if(!p.gossip.open()||p.gossip.npcGuid!=n.guid){p.gossip=LocalGossipState{};p.gossip.npcGuid=n.guid;p.gossip.textId=kLocalGossipDefaultText;}
    p.gossip.offeredQuestId=questId;gossipTouch(p);
    LOG_INFO("[LOCAL_GOSSIP] quest offered player=",p.guid," quest=",questId," by npc=",n.guid);
}
/// SMART_ACTION_FAIL_QUEST: the quest leaves the log as a failed one does
/// (the reference keeps it as QUEST_STATUS_FAILED until abandoned; the local
/// log has no failed state, so the abandon is immediate).
void gossipFailQuest(LocalRealmPlayer& p,uint32_t questId) {
    const auto it=std::find_if(p.quests.begin(),p.quests.end(),[&](const auto& q){return q.id==questId&&q.status!=LocalQuestStatus::Rewarded;});
    if(it==p.quests.end())return;
    p.quests.erase(it);
    if(content)questStatus(p,*content);
    LOG_INFO("[LOCAL_GOSSIP] quest failed player=",p.guid," quest=",questId);
}
