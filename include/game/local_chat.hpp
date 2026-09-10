#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace wowee::game {
enum class LocalChatChannel : uint8_t { Say=1, Party=2, Yell=6, Whisper=7, WhisperInform=9 };
struct LocalChatLine {
    LocalChatChannel channel=LocalChatChannel::Say;
    uint64_t sender=0;
    std::string senderName, receiverName, text;
};
struct LocalChatActor {
    uint64_t guid=0;
    std::string name;
    uint32_t map=0, instance=0, party=0;
    uint8_t race=0;
    float x=0,y=0,z=0;
    bool loading=false,dead=false;
};
inline uint8_t localChatTeam(uint8_t race) {
    switch(race){case 1:case 3:case 4:case 7:case 11:return 1;case 2:case 5:case 6:case 8:case 10:return 2;default:return 0;}
}
inline bool validLocalChatText(const std::string& text) {
    if(text.empty() || text.size()>160)return false;
    bool visible=false;
    for(size_t i=0;i<text.size();) {
        const uint8_t c=uint8_t(text[i++]);
        if(c<32 || c==127)return false;
        if(c<128){visible=visible||c!=' ';continue;}
        uint32_t cp=0,minimum=0;unsigned extra=0;
        if(c>=0xc2 && c<=0xdf){cp=c&31;extra=1;minimum=0x80;}
        else if(c>=0xe0 && c<=0xef){cp=c&15;extra=2;minimum=0x800;}
        else if(c>=0xf0 && c<=0xf4){cp=c&7;extra=3;minimum=0x10000;}
        else return false;
        if(text.size()-i<extra)return false;
        for(unsigned n=0;n<extra;++n){const auto b=uint8_t(text[i++]);if((b&0xc0)!=0x80)return false;cp=(cp<<6)|(b&63);}
        if(cp<minimum || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff) || (cp>=0x80 && cp<=0x9f))return false;
        visible=true;
    }
    return visible;
}
inline bool localChatNameEqual(const std::string& a,const std::string& b) {
    if(a.size()!=b.size())return false;
    auto lower=[](unsigned char c){return c>='A' && c<='Z'?c+32:c;};
    for(size_t i=0;i<a.size();++i)if(lower(a[i])!=lower(b[i]))return false;
    return true;
}
struct LocalChatRoute {std::vector<uint64_t> recipients;std::string receiver,error;};
inline LocalChatRoute routeLocalChat(const std::vector<LocalChatActor>& actors,uint64_t sender,
        LocalChatChannel channel,const std::string& text,const std::string& target) {
    LocalChatRoute out;
    auto reject=[&](const char* reason){out.error=reason;return out;};
    if(!validLocalChatText(text))return reject("Chat needs 1-160 UTF-8 bytes without control characters");
    if(channel!=LocalChatChannel::Say && channel!=LocalChatChannel::Yell && channel!=LocalChatChannel::Party && channel!=LocalChatChannel::Whisper)
        return reject("This chat channel is not available in a local realm");
    const LocalChatActor* from=nullptr;
    for(const auto& a:actors)if(a.guid==sender)from=&a;
    if(!from || from->loading || !localChatTeam(from->race))return reject("Chat is unavailable while the character is loading");
    if(channel==LocalChatChannel::Whisper) {
        const LocalChatActor* to=nullptr;
        for(const auto& a:actors)if(localChatNameEqual(a.name,target)) {if(to)return reject("Player name is ambiguous");to=&a;}
        if(!to || to->loading || localChatTeam(to->race)!=localChatTeam(from->race))return reject("That player is not available for whispers");
        out.receiver=to->name;
        out.recipients.push_back(from->guid);
        if(to->guid!=from->guid)out.recipients.push_back(to->guid);
        return out;
    }
    if(!target.empty())return reject("Only whispers accept a target name");
    if(channel==LocalChatChannel::Party && !from->party)return reject("You are not in a party");
    for(const auto& a:actors) {
        if(a.loading || localChatTeam(a.race)!=localChatTeam(from->race))continue;
        if(channel==LocalChatChannel::Party){if(a.party!=from->party)continue;}
        else {
            if(a.map!=from->map || a.instance!=from->instance || a.dead!=from->dead)continue;
            const double dx=double(a.x)-from->x,dy=double(a.y)-from->y,dz=double(a.z)-from->z;
            const double distance=dx*dx+dy*dy+dz*dz,range=channel==LocalChatChannel::Say?25.0:300.0;
            if(!std::isfinite(distance) || distance>range*range)continue;
        }
        out.recipients.push_back(a.guid);
    }
    if(out.recipients.empty())return reject("No valid chat location is available");
    return out;
}
struct LocalChatRate {
    double tokens=4,last=0;
    bool consume(double now) {
        if(!std::isfinite(now) || now<last)return false;
        tokens=std::min(4.0,tokens+now-last);last=now;
        if(tokens<1)return false;
        tokens-=1;return true;
    }
};
// User text is plain text, never authority to embed FrameXML texture/link markup.
inline std::string localChatDisplayText(const std::string& text) {
    std::string out;out.reserve(text.size());
    for(char c:text){out+=c;if(c=='|')out+='|';}
    return out;
}
}
