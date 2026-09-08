#pragma once

#include "network/net_platform.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace wowee::game::lan {
// Discovery is deliberately separate from the authenticated gameplay messages.
// No user identity, character data or address is advertised in the payload.
inline constexpr uint16_t Port = 3725;
inline constexpr uint8_t GameplayVersion = 14;
inline constexpr size_t MaxRealms = 32, MaxName = 48, QuerySize = 16, ReplySize = 80;
inline constexpr double ScanSeconds = 3.0, ExpirySeconds = 10.0;
inline bool validName(const std::string& name) {
    if (name.empty() || name.size() > MaxName || name.front() == ' ' || name.back() == ' ') return false;
    for (unsigned char c : name)
        if (!(c == ' ' || c == '-' || c == '_' || c == '\'' ||
              (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) return false;
    return true;
}
inline void addressInit(sockaddr_in& address) {
    address = {}; address.sin_family = AF_INET;
#if defined(WOWEE_PS4) || defined(__FreeBSD__) || defined(__APPLE__)
    address.sin_len = sizeof(address);
#endif
}
inline uint64_t get(const uint8_t* p, size_t n) {
    uint64_t v = 0; for (size_t i = 0; i < n; ++i) v = (v << 8) | p[i]; return v;
}
inline void put(uint8_t* p, uint64_t v, size_t n) {
    for (size_t i = n; i > 0; --i) { p[i-1] = uint8_t(v); v >>= 8; }
}
inline bool header(const uint8_t* p, size_t size, uint8_t kind, size_t expected) {
    return p && size == expected && std::memcmp(p, "WPD1", 4) == 0 &&
        p[4] == 1 && p[5] == kind && get(p+6,2) == expected;
}
inline std::array<uint8_t, QuerySize> query(uint64_t nonce) {
    std::array<uint8_t, QuerySize> p{}; std::memcpy(p.data(),"WPD1",4);
    p[4]=1; p[5]=1; put(p.data()+6,QuerySize,2); put(p.data()+8,nonce,8); return p;
}
inline bool readQuery(const uint8_t* p, size_t n, uint64_t& nonce) {
    if (!header(p,n,1,QuerySize)) return false;
    nonce = get(p+8,8); return nonce != 0;
}
struct Advertisement {
    uint64_t realmId = 0;
    uint16_t port = Port, players = 0, capacity = 8;
    uint8_t protocol = GameplayVersion;
    std::string name = "LAN Realm";
};
inline std::array<uint8_t, ReplySize> reply(uint64_t nonce, const Advertisement& a) {
    std::array<uint8_t, ReplySize> p{}; std::memcpy(p.data(),"WPD1",4);
    p[4]=1; p[5]=2; put(p.data()+6,ReplySize,2); put(p.data()+8,nonce,8);
    put(p.data()+16,a.realmId,8); put(p.data()+24,a.port,2);
    put(p.data()+26,a.players,2); put(p.data()+28,a.capacity,2); p[30]=a.protocol;
    const std::string name = validName(a.name) ? a.name : "LAN Realm";
    p[31]=uint8_t(name.size()); std::memcpy(p.data()+32,name.data(),name.size()); return p;
}
inline bool readReply(const uint8_t* p, size_t n, uint64_t nonce, Advertisement& a) {
    if (!nonce || !header(p,n,2,ReplySize) || get(p+8,8)!=nonce || !p[31] || p[31]>MaxName) return false;
    Advertisement next;
    next.realmId=get(p+16,8); next.port=uint16_t(get(p+24,2)); next.players=uint16_t(get(p+26,2));
    next.capacity=uint16_t(get(p+28,2)); next.protocol=p[30]; next.name.assign(reinterpret_cast<const char*>(p+32),p[31]);
    if (!next.realmId || !next.port || next.capacity<2 || next.capacity>100 ||
        !next.players || next.players>next.capacity || !validName(next.name)) return false;
    for(size_t i=32+p[31]; i<n; ++i) if(p[i]) return false;
    a=std::move(next); return true;
}
struct Realm : Advertisement {
    // Private transport metadata: never pass these members to a UI label/log.
    uint32_t ipv4 = 0;
    double lastSeen = 0;
    bool compatible() const { return protocol == GameplayVersion; }
    bool joinable() const { return compatible() && players < capacity; }
    std::string key() const { return std::to_string(realmId)+"/"+std::to_string(ipv4)+"/"+std::to_string(port); }
    std::string endpoint() const {
        in_addr address{}; address.s_addr=ipv4; char text[INET_ADDRSTRLEN]{};
        return ::inet_ntop(AF_INET,&address,text,sizeof(text)) ? text : "";
    }
};

class Browser {
public:
    Browser() = default;
    Browser(const Browser&)=delete; Browser& operator=(const Browser&)=delete;
    ~Browser(){ close(); }
    bool refresh() {
        close(); realms_.clear(); error_.clear();
        net::ensureInit(); socket_=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if(socket_==INVALID_SOCK) return fail("LAN search could not open a socket.");
        int enabled=1;
#ifdef WOWEE_PS4
        // Kernel socket interface uses BSD SOL_SOCKET/SO_BROADCAST, not Linux musl values.
        constexpr int level=0xffff, option=0x0020;
#else
        constexpr int level=SOL_SOCKET, option=SO_BROADCAST;
#endif
        if(::setsockopt(socket_,level,option,reinterpret_cast<const char*>(&enabled),sizeof(enabled))!=0 ||
           !net::configureDatagramNonBlocking(socket_)) return fail("LAN broadcast search is unavailable.");
        sockaddr_in address{}; addressInit(address); address.sin_addr.s_addr=htonl(INADDR_ANY);
        if(::bind(socket_,reinterpret_cast<const sockaddr*>(&address),sizeof(address))!=0)
            return fail("LAN search could not bind a socket.");
        started_=Clock::now(); now_=0; nextQuery_=0; nonce_=uint64_t(started_.time_since_epoch().count()) ^ (++serial_ * 0x9e3779b97f4a7c15ULL);
        if(!nonce_) nonce_=1;
        searching_=true; tick(); return true;
    }
    void close() { if(socket_!=INVALID_SOCK) net::closeSocket(socket_); socket_=INVALID_SOCK; searching_=false; }
    void tick() {
        if(socket_==INVALID_SOCK) return;
        now_=std::chrono::duration<double>(Clock::now()-started_).count();
        realms_.erase(std::remove_if(realms_.begin(),realms_.end(),[&](const Realm& r){return now_-r.lastSeen>=ExpirySeconds;}),realms_.end());
        if(now_>=ScanSeconds) searching_=false;
        if(searching_ && now_>=nextQuery_) {
            const auto data=query(nonce_); sockaddr_in address{}; addressInit(address);
            address.sin_port=htons(Port); address.sin_addr.s_addr=htonl(INADDR_BROADCAST);
            const auto n=::sendto(socket_,reinterpret_cast<const char*>(data.data()),int(data.size()),net::datagramFlags(),
                                 reinterpret_cast<const sockaddr*>(&address),sizeof(address));
            if(n<0 && !net::isWouldBlock(net::lastError())) error_="LAN broadcast failed. Check the network connection.";
            nextQuery_=now_+0.5;
        }
        std::array<uint8_t,ReplySize+1> buffer{};
        for(unsigned i=0; i<64; ++i) {
            sockaddr_in source{}; socklen_t length=sizeof(source);
            const auto n=::recvfrom(socket_,reinterpret_cast<char*>(buffer.data()),int(buffer.size()),net::datagramFlags(),
                                    reinterpret_cast<sockaddr*>(&source),&length);
            if(n<0) break;
            if(!searching_ || length!=sizeof(source) || source.sin_family!=AF_INET ||
               !source.sin_addr.s_addr || source.sin_addr.s_addr==htonl(INADDR_BROADCAST)) continue;
            Advertisement a;
            if(!readReply(buffer.data(),size_t(n),nonce_,a) || a.port!=ntohs(source.sin_port)) continue;
            auto it=std::find_if(realms_.begin(),realms_.end(),[&](const Realm& r){return r.realmId==a.realmId && r.ipv4==source.sin_addr.s_addr && r.port==a.port;});
            if(it==realms_.end()) {
                if(realms_.size()>=MaxRealms) continue;
                realms_.emplace_back(); it=realms_.end()-1;
            }
            static_cast<Advertisement&>(*it)=std::move(a); it->ipv4=source.sin_addr.s_addr; it->lastSeen=now_;
        }
        std::stable_sort(realms_.begin(),realms_.end(),[](const Realm& a,const Realm& b){return a.name==b.name ? a.key()<b.key() : a.name<b.name;});
    }
    const std::vector<Realm>& realms() const {return realms_;}
    const std::string& error() const {return error_;}
    bool searching() const {return searching_;}
private:
    using Clock=std::chrono::steady_clock;
    bool fail(const char* text){error_=text;close();return false;}
    socket_t socket_=INVALID_SOCK;
    Clock::time_point started_{};
    uint64_t nonce_=0,serial_=0;
    double now_=0,nextQuery_=0;
    bool searching_=false;
    std::vector<Realm> realms_;
    std::string error_;
};
} // namespace wowee::game::lan
