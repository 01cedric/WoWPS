#pragma once
#include "game/local_social.hpp"
#include <array>
#include <unordered_set>

namespace wowee::game {
struct LocalMail {
    uint32_t id=0;
    uint64_t sender=0,recipient=0;
    std::string senderName,subject,body;
    uint32_t money=0,cod=0;
    std::array<LocalItemStack,12> items{};
    bool read=false,returned=false,system=false;
    bool operator==(const LocalMail&)const=default;
    bool hasItems()const {return std::any_of(items.begin(),items.end(),[](const auto& s){return s.itemId!=0;});}
};
class LocalMailbox {
public:
    static constexpr size_t MaxMessages=2048,MaxInbox=64;
    std::vector<LocalMail> messages;
    uint32_t nextId=1;
    size_t count(uint64_t owner)const {return std::count_if(messages.begin(),messages.end(),[&](const auto& m){return m.recipient==owner;});}
    LocalMail* find(uint64_t owner,uint32_t id) {for(auto& m:messages)if(m.id==id && m.recipient==owner)return &m;return nullptr;}
    bool room(uint64_t owner)const {return owner && count(owner)<MaxInbox && messages.size()<MaxMessages && nextId && nextId<UINT32_MAX;}
    bool append(LocalMail mail) {if(!room(mail.recipient))return false;mail.id=nextId;messages.push_back(std::move(mail));++nextId;return true;}
    bool valid()const {
        if(!nextId || messages.size()>MaxMessages)return false;
        std::unordered_set<uint32_t> ids;
        for(const auto& m:messages){
            if(!m.id || m.id>=nextId || !ids.insert(m.id).second || !m.recipient ||
               m.senderName.size()>16 || m.subject.size()>64 || m.body.size()>160 ||
               m.money>1000000000 || m.cod>1000000000 || (!m.system && !m.sender) ||
               (m.cod && (m.money || !m.hasItems())) || count(m.recipient)>MaxInbox)return false;
            for(const auto& item:m.items)if(bool(item.itemId)!=bool(item.count))return false;
        }
        return true;
    }
};
inline bool localMailTextValid(const std::string& text,size_t max) {
    if(text.size()>max)return false;
    for(unsigned char c:text)if(c==0 || (c<32 && c!='\n' && c!='\t'))return false;
    return true;
}
inline bool prepareLocalMail(const LocalRealmPlayer& sender,const LocalRealmPlayer& recipient,
        const std::string& subject,const std::string& body,uint32_t money,uint32_t cod,
        const std::vector<LocalTradeItem>& attachments,const LocalWorldContent& content,
        LocalRealmPlayer& out,LocalMail& mail,std::string& error) {
    auto reject=[&](const char* why){error=why;return false;};
    if(sender.guid==recipient.guid || !recipient.guid || localChatTeam(sender.race)!=localChatTeam(recipient.race))return reject("Choose another saved character of your faction");
    if(!localMailTextValid(subject,64) || !localMailTextValid(body,160) || attachments.size()>12 || money>1000000000 || cod>1000000000 || (cod && (money || attachments.empty())))return reject("Invalid letter, attachment count or money");
    std::array<bool,LocalGameplay::MaxInventory> seen{};
    for(const auto& item:attachments){
        if(!item.item || item.bag>=seen.size() || seen[item.bag] || !localTradeItemValid(sender,item,content))return reject("An attachment changed, is equipped or cannot be mailed");
        seen[item.bag]=true;
    }
    const uint64_t postage=30*std::max(size_t(1),attachments.size());
    if(uint64_t(money)+postage>sender.money)return reject("Not enough money for postage and attachment");
    out=sender;normalizeLocalInventory(out);out.money-=uint32_t(money+postage);
    mail={};mail.sender=sender.guid;mail.senderName=sender.name;mail.recipient=recipient.guid;
    mail.subject=subject;mail.body=body;mail.money=money;mail.cod=cod;
    for(size_t i=0;i<attachments.size();++i){const auto& item=attachments[i];out.inventory[localInventoryIndex(out,item.bag)].count-=item.count;mail.items[i]={item.item,item.count};}
    std::erase_if(out.inventory,[](const auto& item){return !item.count;});return true;
}
inline bool giveLocalMailItem(LocalRealmPlayer& player,const LocalItemStack& item,const LocalWorldContent& content) {
    normalizeLocalInventory(player);
    const auto* def=content.item(item.itemId);if(!def || !item.count)return false;
    const unsigned limit=std::max(uint16_t(1),def->stack);unsigned left=item.count;
    for(auto& stack:player.inventory)if(stack.itemId==item.itemId && stack.count<limit){const auto n=std::min(left,limit-stack.count);stack.count+=n;left-=n;}
    while(left){if(player.inventory.size()>=LocalGameplay::MaxInventory)return false;const auto n=std::min(left,limit);player.inventory.push_back({item.itemId,uint16_t(n)});left-=n;}
    normalizeLocalInventory(player);return true; // The caller uses a candidate player and commits only on success.
}
} // namespace wowee::game
