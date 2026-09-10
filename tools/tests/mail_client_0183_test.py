#!/usr/bin/env python3
"""Exercise production local mail UI methods with controllable authority acknowledgments."""
from pathlib import Path
import subprocess,tempfile,os
r=Path(__file__).resolve().parents[2]
s=(r/'src/game/inventory_handler.cpp').read_text()
def local_prefix(signature, end):
 start=s.index(signature);stop=s.index(end,start)
 return s[start:stop]+'}\n'
with tempfile.TemporaryDirectory(prefix='wowps-mail-ui-') as folder:
 t=Path(folder)
 (t/'send.inc').write_text(local_prefix('void InventoryHandler::sendMail(', '    if (owner_.getState()'))
 (t/'attach.inc').write_text(local_prefix('bool InventoryHandler::attachItemFromBackpack(', '    if (backpackIndex <')[:-2]+'return false;\n}\n')
 (t/'test.cpp').write_text(r'''
#include "game/local_mail.hpp"
#include <functional>
#include <iostream>
#include <cassert>
using namespace wowee::game;
struct ItemDef{uint32_t itemId=117,stackCount=20;};
struct ItemSlot{ItemDef item;bool empty()const{return !item.itemId;}};
struct Inventory{static constexpr int NUM_EQUIP_SLOTS=19;};
struct MailAttachment{uint8_t slot=0;uint32_t itemGuidLow=0,itemId=0,stackCount=0;};
struct MailMessage{uint32_t messageId=0,flags=0,stationeryId=0;uint64_t senderGuid=0,money=0,cod=0;bool read=false;float expirationTime=0;std::string senderName,subject,body;std::vector<MailAttachment> attachments;};
struct Realm {
 LocalRealmPlayer player;bool online=true,access=true,ok=true,queue=true;uint64_t revision=1,result=1;unsigned sends=0,requests=0;
 std::vector<LocalMail> rows;std::vector<LocalTradeItem> attachments;std::vector<std::pair<LocalAction,uint32_t>> actions;
 const LocalRealmPlayer* localPlayer(){return online?&player:nullptr;}bool ready(){return online;}
 uint64_t mailResultRevision(){return result;}uint64_t mailRevision(){return revision;}bool mailResultSuccess(){return ok;}
 std::string actionStatus(){return "Rejected";}bool mailAccess(uint64_t){return access;}void requestMail(uint64_t){++requests;}
 auto inbox(){return rows;}
 bool mailAction(LocalAction a,uint64_t,uint32_t,uint32_t slot){actions.push_back({a,slot});return queue;}
 bool sendMail(uint64_t,const std::string&,const std::string&,const std::string&,uint32_t,uint32_t,const std::vector<LocalTradeItem>& a){++sends;attachments=a;return queue;}
};
struct Owner {
 Realm realm;ItemSlot slot;std::vector<std::string> events,errors;std::function<void(std::string,std::vector<std::string>)> event=[this](auto e,auto){events.push_back(e);};
 Realm* localServiceRealm(){return &realm;}auto& addonEventCallbackRef(){return event;}
 void addSystemChatMessage(const std::string& s){errors.push_back(s);}void cacheLocalAuctionItem(uint32_t){}
 const ItemSlot* localBagSlot(int i,uint64_t* guid){if(i<0 || i>=24)return nullptr;*guid=100+i;return &slot;}
};
struct InventoryHandler{
 struct MailAttachSlot{uint64_t itemGuid=0;ItemDef item;uint8_t srcBag=255,srcSlot=0;bool occupied()const{return itemGuid!=0;}};
 Owner owner_;std::array<MailAttachSlot,12> mailAttachments_;std::vector<MailMessage> mailInbox_;
 uint64_t localMailOwner_=0,localMailRevision_=UINT64_MAX,localMailResult_=0,mailboxGuid_=10;
 uint8_t localMailPending_=0;uint32_t localMailPendingId_=0,localMailLootId_=0;
 bool mailboxOpen_=true;std::vector<std::pair<uint8_t,uint32_t>> localMailLootQueue_;
 void clearMailAttachments(){if(!localMailPending_)mailAttachments_={};}
 void notifyMailComposeChanged(){owner_.events.push_back("MAIL_SEND_INFO_UPDATE");}
 void setHasNewMail(bool){}void closeMailbox(){mailboxOpen_=false;localMailLootQueue_.clear();}
 void refuseSend(const std::string& s,const char*){owner_.errors.push_back(s);owner_.events.push_back("MAIL_FAILED");}
 void pumpLocalMail();void localMailAction(uint8_t,uint32_t,uint32_t=0);void autoLootLocalMail(uint32_t);
 void sendMail(const std::string&,const std::string&,const std::string&,uint64_t,uint64_t);bool attachItemFromBackpack(int);
};
#include "src/game/local_mail_inventory.inc"
#include "send.inc"
#include "attach.inc"
int main(){
 InventoryHandler ui;auto& r=ui.owner_.realm;r.player.guid=1;ui.pumpLocalMail();
 for(int i=0;i<12;++i)assert(ui.attachItemFromBackpack(i));assert(!ui.attachItemFromBackpack(12) && !ui.attachItemFromBackpack(0));
 ui.sendMail("Guest","Subject","Body",100,0);assert(r.sends==1 && r.attachments.size()==12 && r.attachments[11].bag==11 && r.attachments[0].sourceCount==20);
 ui.sendMail("Guest","Again","",100,0);assert(r.sends==1 && ui.mailAttachments_[0].occupied());
 r.ok=false;++r.result;ui.pumpLocalMail();assert(!ui.localMailPending_ && ui.mailAttachments_[0].occupied() && ui.owner_.events.back()=="MAIL_FAILED");
 ui.sendMail("Guest","Retry","",100,0);assert(r.sends==2);r.ok=true;++r.result;ui.pumpLocalMail();assert(!ui.mailAttachments_[0].occupied() && ui.owner_.events.back()=="MAIL_SEND_SUCCESS");
 std::cout<<"PASS production mail compose: twelve fingerprinted attachments, no duplicates/overflow, pending send guard, draft retained on rejection, cleared only on acknowledgment\n";
 LocalMail m;m.id=7;m.recipient=1;m.sender=2;m.senderName="Guest";m.subject="Many";m.money=10;m.items[0]={117,2};m.items[11]={117,3};r.rows={m};++r.revision;ui.pumpLocalMail();
 assert(ui.mailInbox_[0].attachments[1].itemGuidLow==12);
 ui.autoLootLocalMail(7);assert(r.actions.size()==1 && r.actions[0].first==LocalAction::MailTakeMoney);
 ++r.result;ui.pumpLocalMail();assert(r.actions.size()==2 && r.actions[1].second==0);
 ++r.result;ui.pumpLocalMail();assert(r.actions.size()==3 && r.actions[2].second==11);
 ++r.result;ui.pumpLocalMail();assert(!ui.localMailPending_ && ui.localMailLootQueue_.empty());
 ui.autoLootLocalMail(7);r.ok=false;++r.result;ui.pumpLocalMail();assert(ui.localMailLootQueue_.empty() && r.actions.size()==4);
 r.rows[0].cod=500;++r.revision;ui.pumpLocalMail();ui.autoLootLocalMail(7);assert(r.actions.size()==4);
 std::cout<<"PASS production mail collection: stable attachment IDs, auto-loot serialized through acknowledgments, stop on failure, no automatic COD charge\n";
 r.rows[0].cod=0;++r.revision;ui.pumpLocalMail();ui.localMailAction(uint8_t(LocalAction::MailRead),7);auto n=r.actions.size();
 ui.localMailAction(uint8_t(LocalAction::MailRead),7);assert(r.actions.size()==n);
 r.ok=true;++r.result;r.rows[0].read=true;++r.revision;ui.pumpLocalMail();assert(ui.mailInbox_[0].read);
 r.online=false;ui.pumpLocalMail();assert(ui.mailInbox_.empty() && !ui.mailboxOpen_ && !ui.localMailOwner_ && !ui.localMailPending_);
 std::cout<<"PASS production mail lifecycle: read recursion guard and disconnect clears private inbox, pending state and mailbox\n";
}
''')
 cmd=[os.getenv('CXX','c++'),'-std=c++20','-O1','-g','-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'extern/glm'),str(t/'test.cpp'),'-o',str(t/'test')]
 if os.getenv('SANITIZE')=='1':cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run(cmd,check=True)
 env=dict(os.environ);env.setdefault('ASAN_OPTIONS','detect_leaks=0') # ASan/UBSan, without unsupported process-scanning LSan.
 subprocess.run([str(t/'test')],check=True,env=env)
