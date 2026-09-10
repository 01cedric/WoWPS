// Included in InventoryHandler's namespace, using the existing MailFrame events.
void InventoryHandler::pumpLocalMail() {
    auto* realm=owner_.localServiceRealm();
    const auto* player=realm?realm->localPlayer():nullptr;
    if(!player || !realm->ready() || (localMailOwner_ && localMailOwner_!=player->guid)){
        if(localMailOwner_){localMailOwner_=0;localMailPending_=0;localMailLootQueue_.clear();mailInbox_.clear();localMailRevision_=UINT64_MAX;if(mailboxOpen_)closeMailbox();}
        if(!player || !realm->ready())return;
    }
    localMailOwner_=player->guid;
    if(localMailPending_ && realm->mailResultRevision()!=localMailResult_){
        const auto action=LocalAction(localMailPending_);const auto id=localMailPendingId_;localMailPending_=0;
        const bool ok=realm->mailResultSuccess();
        if(ok && action==LocalAction::MailSend)clearMailAttachments();
        if(!ok){localMailLootQueue_.clear();owner_.addSystemChatMessage(realm->actionStatus());}
        if(owner_.addonEventCallbackRef()){
            if(action==LocalAction::MailSend)owner_.addonEventCallbackRef()(ok?"MAIL_SEND_SUCCESS":"MAIL_FAILED",ok?std::vector<std::string>{}:std::vector<std::string>{"0"});
            if(ok && (action==LocalAction::MailReturn || action==LocalAction::MailDelete))owner_.addonEventCallbackRef()("CLOSE_INBOX_ITEM",{std::to_string(id)});
        }
    }
    if(!mailboxOpen_)return;
    if(!realm->mailAccess(mailboxGuid_)){closeMailbox();return;}
    realm->requestMail(mailboxGuid_);
    if(!localMailPending_ && !localMailLootQueue_.empty()){
        const auto next=localMailLootQueue_.front();localMailLootQueue_.erase(localMailLootQueue_.begin());
        localMailAction(next.first,localMailLootId_,next.second);return;
    }
    if(localMailRevision_==realm->mailRevision())return;
    localMailRevision_=realm->mailRevision();mailInbox_.clear();
    bool unread=false;
    for(const auto& m:realm->inbox()){
        MailMessage row;row.messageId=m.id;row.senderGuid=m.sender;row.senderName=m.senderName;
        row.subject=m.subject;row.body=m.body;row.money=m.money;row.cod=m.cod;row.read=m.read;
        row.flags=(m.read?1:0)|(m.returned?2:0)|(m.system?4:0);row.stationeryId=41;row.expirationTime=365; // Display grace value; local letters do not auto-expire.
        for(size_t i=0;i<m.items.size();++i)if(m.items[i].itemId){
            MailAttachment a;a.slot=uint8_t(i);a.itemGuidLow=uint32_t(i+1);a.itemId=m.items[i].itemId;a.stackCount=m.items[i].count;
            row.attachments.push_back(a);owner_.cacheLocalAuctionItem(a.itemId);
        }
        unread=unread || !m.read;mailInbox_.push_back(std::move(row));
    }
    setHasNewMail(unread);
    if(owner_.addonEventCallbackRef())owner_.addonEventCallbackRef()("MAIL_INBOX_UPDATE",{});
}
void InventoryHandler::localMailAction(uint8_t action,uint32_t id,uint32_t slot){
    auto* realm=owner_.localServiceRealm();if(!realm || !mailboxOpen_ || localMailPending_)return;
    if(LocalAction(action)==LocalAction::MailRead){
        auto it=std::find_if(mailInbox_.begin(),mailInbox_.end(),[&](const auto& m){return m.messageId==id;});
        if(it==mailInbox_.end() || it->read)return;
        it->read=true; // Prevent recursive GetInboxText -> MAIL_INBOX_UPDATE.
    }
    localMailResult_=realm->mailResultRevision();localMailPending_=action;localMailPendingId_=id;
    if(!realm->mailAction(LocalAction(action),mailboxGuid_,id,slot) && realm->mailResultRevision()==localMailResult_){
        localMailPending_=0;localMailLootQueue_.clear();owner_.addSystemChatMessage("Mail action could not be queued.");
    }
    pumpLocalMail();
}
void InventoryHandler::autoLootLocalMail(uint32_t id){
    if(localMailPending_ || !localMailLootQueue_.empty() || !mailboxOpen_)return;
    const auto it=std::find_if(mailInbox_.begin(),mailInbox_.end(),[&](const auto& m){return m.messageId==id;});
    if(it==mailInbox_.end())return;
    if(it->cod){owner_.addSystemChatMessage("Open COD mail and confirm its attachment individually.");return;}
    localMailLootId_=id;
    if(it->money)localMailLootQueue_.push_back({uint8_t(LocalAction::MailTakeMoney),0});
    for(const auto& a:it->attachments)localMailLootQueue_.push_back({uint8_t(LocalAction::MailTakeItem),a.itemGuidLow-1});
    pumpLocalMail();
}
