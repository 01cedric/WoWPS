// Included inside LocalRealm::Impl; mail uses the same atomic realm snapshot.
    void clearMail() {
        remoteMail.clear();for(auto& rows:mailChunks)rows.clear();mailReceived.fill(false);
        mailSequence=collectingMail=0;mailParts=0;lastMailRequest=-1;++mailRevision;
        mailResultSuccess=false;++mailResultRevision;
    }
    void receiveMailQuery(Peer& peer,Reader& r) {
        const auto service=r.u64();const auto* record=findSaved(peer.guid);
        if(!r.done() || !record || !mailReach(record->player,service) || (peer.lastMailQuery>=0 && now-peer.lastMailQuery<0.5))return;
        peer.lastMailQuery=now;peer.lastSeen=now;
        std::vector<LocalMail> rows;for(const auto& m:mailbox.messages)if(m.recipient==peer.guid)rows.push_back(m);
        const auto parts=std::max(size_t(1),(rows.size()+1)/2);if(!++mailTick)++mailTick;
        for(size_t part=0;part<parts;++part){
            const auto begin=part*2,end=std::min(begin+2,rows.size());
            Writer w;w.u32(mailTick);w.u64(peer.guid);w.u8(uint8_t(part));w.u8(uint8_t(parts));w.u8(uint8_t(end-begin));
            for(auto i=begin;i<end;++i)writeMail(w,rows[i]);send(Message::MailState,peer.session,w,peer.address);
        }
    }
    void receiveMailState(Reader& r) {
        const auto tick=r.u32();const auto owner=r.u64();const auto part=r.u8(),parts=r.u8(),count=r.u8();
        if(!tick || owner!=self.guid || !newer(tick,mailSequence) || !parts || parts>32 || part>=parts || count>2 || (part+1<parts && count!=2) || (parts>1 && !count))return;
        std::vector<LocalMail> rows;for(unsigned i=0;i<count;++i){auto m=readMail(r);if(!r.valid || m.recipient!=owner)return;rows.push_back(std::move(m));}if(!r.done())return;
        if(tick!=collectingMail){if(collectingMail && !newer(tick,collectingMail))return;collectingMail=tick;mailParts=parts;mailReceived.fill(false);}
        if(parts!=mailParts || (mailReceived[part] && mailChunks[part]!=rows))return;
        mailChunks[part]=std::move(rows);mailReceived[part]=true;
        if(!std::all_of(mailReceived.begin(),mailReceived.begin()+parts,[](bool v){return v;}))return;
        LocalMailbox candidate;candidate.nextId=UINT32_MAX;
        for(unsigned i=0;i<parts;++i)candidate.messages.insert(candidate.messages.end(),mailChunks[i].begin(),mailChunks[i].end());
        if(!candidate.valid())return;
        if(remoteMail!=candidate.messages){remoteMail=std::move(candidate.messages);++mailRevision;}
        mailSequence=tick;lastSeen=now;
    }
    bool mailReach(const LocalRealmPlayer& player,uint64_t service)const {
        if(!tradeAvailable(player))return false;
        if(nearbyLocalMailbox(gameplay.content(),player,service))return true;
        for(const auto& npc:gameplay.npcs())if(npc.guid==service && npc.innkeeper && localNpcInTalkRange(player,npc))return true;
        return false;
    }
    bool hasMailAssets(uint64_t guid)const {
        for(const auto& m:mailbox.messages)if((m.recipient==guid || m.sender==guid) && (m.money || m.cod || m.hasItems()))return true;
        for(const auto& d:botDirector.deliveries())if(d.recipient==guid)return true;
        for(const auto& a:botDirector.auctions())if(a.seller==guid || a.highestBidder==guid)return true;
        return false;
    }
    bool executeMail(LocalRealmPlayer& player,const LocalRealmCommand& cmd,std::string& result) {
        auto reject=[&](const char* why){result=why;return false;};
        if(!mailReach(player,cmd.serviceNpcGuid))return reject("Stand near a mailbox or visit an available innkeeper");
        auto candidate=mailbox;auto candidatePlayer=player;
        if(cmd.action==LocalAction::MailSend){
            const LocalRealmPlayer* recipient=nullptr;
            for(const auto& row:saved)if(localChatNameEqual(row.player.name,cmd.mailRecipient)){
                if(recipient)return reject("Recipient name is ambiguous");recipient=&row.player;
            }
            if(!recipient)return reject("No saved character by that name in this realm");
            LocalMail letter;
            if(!candidate.room(recipient->guid))return reject("Recipient mailbox is full");
            if(!prepareLocalMail(player,*recipient,cmd.mailSubject,cmd.mailBody,cmd.bid,cmd.buyout,cmd.mailAttachments,gameplay.content(),candidatePlayer,letter,result))return false;
            if(!candidate.append(std::move(letter)))return reject("Mailbox is full");
        }else{
            auto* mail=candidate.find(player.guid,cmd.id);if(!mail)return reject("That letter is no longer in your mailbox");
            if(cmd.action==LocalAction::MailTakeMoney){
                if(candidatePlayer.money>1000000000)return reject("Invalid wallet balance");
                const auto amount=std::min(mail->money,1000000000u-candidatePlayer.money);
                if(!amount)return reject("No money to collect or wallet is full");
                candidatePlayer.money+=amount;mail->money-=amount;mail->read=true;
            }else if(cmd.action==LocalAction::MailTakeItem){
                if(cmd.target>=mail->items.size() || !giveLocalMailItem(candidatePlayer,mail->items[cmd.target],gameplay.content()))return reject("The attachment is missing or your backpack is full");
                if(mail->cod){
                    if(candidatePlayer.money<mail->cod || !findSaved(mail->sender) || !candidate.room(mail->sender))return reject("COD cannot be paid: check gold and sender mailbox capacity");
                    LocalMail payment;payment.recipient=mail->sender;payment.sender=player.guid;payment.senderName=player.name;
                    payment.subject="COD payment";payment.money=mail->cod;payment.system=true;
                    candidatePlayer.money-=mail->cod;
                    candidate.append(std::move(payment));mail=candidate.find(player.guid,cmd.id);mail->cod=0;
                }
                mail->items[cmd.target]={};mail->read=true;
            }else if(cmd.action==LocalAction::MailReturn){
                if(mail->system || mail->returned || !findSaved(mail->sender) || !candidate.room(mail->sender))return reject("This letter cannot be returned");
                const auto recipient=mail->sender;mail->sender=player.guid;mail->senderName=player.name;mail->recipient=recipient;
                mail->returned=true;mail->read=false;mail->cod=0;
            }else if(cmd.action==LocalAction::MailDelete){
                if(mail->money || mail->hasItems())return reject("Collect or return the attachments before deleting");
                std::erase_if(candidate.messages,[&](const auto& m){return m.id==cmd.id;});
            }else if(cmd.action==LocalAction::MailRead)mail->read=true;
            else return reject("Invalid mail action");
        }
        auto previousPlayer=player;
        const auto* ownRecord=findSaved(self.guid);auto previousSelf=ownRecord?ownRecord->player:LocalRealmPlayer{};
        auto previousMail=std::move(mailbox);
        player=std::move(candidatePlayer);mailbox=std::move(candidate);
        if(!saveRealm()){
            player=std::move(previousPlayer);mailbox=std::move(previousMail);if(auto* own=findSaved(self.guid))own->player=std::move(previousSelf);
            LOG_ERROR("[LOCAL_MAIL] transaction rollback: ",error);return reject("Mail was not saved; no items or money changed");
        }
        ++mailRevision;result="Mail transaction saved";
        LOG_INFO("[LOCAL_MAIL] saved owner=",player.guid," action=",int(cmd.action)," id=",cmd.id," messages=",mailbox.messages.size());return true;
    }
    bool migrateAuctionMail() {
        if(botDirector.deliveries().empty())return false;
        auto remaining=botDirector.deliveries();auto candidate=mailbox;bool changed=false;
        for(auto it=remaining.begin();it!=remaining.end();){
            if(botDirector.isBot(it->recipient) || LocalBotDirector::isMarketSeller(it->recipient)){it=remaining.erase(it);changed=true;continue;}
            if(!findSaved(it->recipient) || !candidate.room(it->recipient)){++it;continue;}
            LocalMail mail;mail.recipient=it->recipient;mail.system=true;mail.senderName="Auction House";
            mail.subject="Auction delivery";mail.body="Your auction item, proceeds or refund. Collect at an innkeeper.";
            mail.money=it->money;if(it->itemId)mail.items[0]={it->itemId,it->count};
            if(!candidate.append(std::move(mail))){++it;continue;}
            it=remaining.erase(it);changed=true;
        }
        if(changed){botDirector.restoreDeliveries(remaining);mailbox=std::move(candidate);++mailRevision;LOG_INFO("[LOCAL_MAIL] auction escrow migrated, pending=",remaining.size());}
        return changed;
    }
