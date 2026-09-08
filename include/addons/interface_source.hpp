#pragma once
#include "addons/toc_parser.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace wowee::addons {
// Virtual paths start at mpq/Interface. They never refer to a directory on disk.
// The archive provider owns patch and locale precedence; no second ordering here.
class InterfaceSource {
public:
    static constexpr size_t MaxSourceBytes = 8 * 1024 * 1024;
    using Read = std::function<std::vector<uint8_t>(const std::string&)>;
    using Exists = std::function<bool(const std::string&)>;
    using List = std::function<std::vector<std::string>(const std::string&)>;
    void setArchive(Read read, Exists exists, List list) {
        std::lock_guard<std::mutex> guard(cacheMutex_);
        read_ = std::move(read); exists_ = std::move(exists); list_ = std::move(list);
        textCache_.clear(); existsCache_.clear(); textCacheBytes_ = 0;
        cacheText_ = true;
    }
    // Under pressure, compiled Lua/templates remain valid; only discard copies
    // of source text. Keep caching suspended until the next provider/session.
    size_t releaseSourceCache() {
        std::lock_guard<std::mutex> guard(cacheMutex_);
        const size_t bytes = textCacheBytes_;
        textCache_.clear(); textCacheBytes_ = 0; cacheText_ = false;
        return bytes;
    }
    static std::string fold(std::string s) {
        std::replace(s.begin(),s.end(),'\\','/');
        for (char& c:s) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
    static bool archived(const std::string& p) { return fold(p).rfind("mpq/",0)==0; }
    static std::string archiveKey(const std::string& path) {
        const auto p=std::filesystem::path(fold(path)).lexically_normal().generic_string();
        if(p.rfind("mpq/interface/",0)!=0 || p.find(':')!=std::string::npos) return {};
        return p.substr(4);
    }
    bool exists(const std::string& p) const {
        if(p.empty()) return false;
        if(archived(p)) {
            const auto key=archiveKey(p); if(key.empty() || !exists_) return false;
            { std::lock_guard<std::mutex> guard(cacheMutex_);
              if (const auto it=existsCache_.find(key); it!=existsCache_.end()) return it->second; }
            const bool found=exists_(key);
            { std::lock_guard<std::mutex> guard(cacheMutex_);
              if (existsCache_.size()>=4096) existsCache_.clear();
              existsCache_[key]=found; }
            return found;
        }
        std::error_code ec;return std::filesystem::is_regular_file(p,ec);
    }
    std::filesystem::path resolve(std::filesystem::path base, std::string name) const {
        std::replace(name.begin(),name.end(),'\\','/');
        if(std::filesystem::path(name).is_absolute()) return {};
        if(archived(base.string())) {
            auto key=archiveKey((base/name).generic_string());
            return !key.empty() && exists("mpq/"+key) ? std::filesystem::path("mpq/"+key) : std::filesystem::path{};
        }
        for(const auto& part:std::filesystem::path(name)) {
            if(part=="." || part.empty()) continue;
            if(part=="..") {base=base.parent_path();continue;}
            std::error_code ec;auto exact=base/part;
            if(std::filesystem::exists(exact,ec)) {base=exact;continue;}
            bool found=false;
            for(const auto& e:std::filesystem::directory_iterator(base,ec)) {
                if(fold(e.path().filename().string())==fold(part.string())) {base=e.path();found=true;break;}
            }
            if(!found) return {};
        }
        return base;
    }
    std::optional<std::string> read(const std::string& p) const {
        std::string text;
        if(archived(p)) {
            const auto key=archiveKey(p);
            if(key.empty() || !read_) return std::nullopt;
            { std::lock_guard<std::mutex> guard(cacheMutex_);
              if (const auto it=textCache_.find(key); it!=textCache_.end()) return it->second; }
            if(!exists(p)) return std::nullopt;
            const auto bytes=read_(key);
            // The archive callback reports read/allocation failure as empty.
            // Never poison a retry by caching it as a successfully read source.
            if(bytes.empty()) return std::nullopt;
            if(bytes.size()>MaxSourceBytes) return std::nullopt;
            text.assign(bytes.begin(),bytes.end());
        } else {
            std::ifstream f(p,std::ios::binary|std::ios::ate);
            if(!f) return std::nullopt;
            const auto size=f.tellg();
            if(size<0 || static_cast<uint64_t>(size)>MaxSourceBytes) return std::nullopt;
            text.resize(static_cast<size_t>(size));f.seekg(0);
            if(!text.empty() && !f.read(text.data(),text.size())) return std::nullopt;
        }
        if(text.size()>=3 && text.compare(0,3,"\xEF\xBB\xBF")==0) text.erase(0,3);
        if (archived(p) && text.size() <= MaxCachedSourceBytes) {
            const auto key=archiveKey(p);
            std::lock_guard<std::mutex> guard(cacheMutex_);
            if (!cacheText_) return text;
            if (textCacheBytes_ + text.size() > MaxTextCacheBytes || textCache_.size() >= 2048) {
                textCache_.clear(); textCacheBytes_ = 0;
            }
            auto [it, inserted]=textCache_.emplace(key,text);
            if(inserted) textCacheBytes_ += it->second.size();
        }
        return text;
    }
    std::optional<TocFile> toc(const std::string& p) const {
        const auto text=read(p);return text ? std::optional<TocFile>(parseTocText(*text,p)) : std::nullopt;
    }
    std::vector<std::string> addonManifests(bool enumerateArchive =
#ifdef WOWEE_PS4
        false
#else
        true
#endif
    ) const {
        std::set<std::string> paths;
        // PS4: enumerating an MPQ loads every archive's listfile into resident
        // StormLib tables. Probe known TOCs directly instead (loose addons are
        // still discovered by AddonManager). Explicit enumeration remains
        // available to callers that need to discover custom archived addons.
        if(enumerateArchive && list_) for(const auto& p:list_("interface\\addons\\")) {
            auto key=archiveKey("mpq/"+p);
            if(key.size()>4 && key.ends_with(".toc") && paths.size()<4096) paths.insert("mpq/"+key);
        }
        // Known module manifests also work when an MPQ has no internal listfile.
        for(const char* n:{"AchievementUI","ArenaUI","AuctionUI","BarberShopUI","BattlefieldMinimap",
                "BindingUI","Calendar","CombatLog","CombatText","DebugTools","GlyphUI","GMChatUI",
                "GMSurveyUI","GuildBankUI","InspectUI","ItemSocketingUI","MacroUI","RaidUI",
                "TalentUI","TimeManager","TokenUI","TradeSkillUI","TrainerUI"}) {
            const std::string base=std::string("Blizzard_")+n;
            const auto path="mpq/interface/addons/"+fold(base+"/"+base+".toc");
            if(exists(path)) paths.insert(path);
        }
        return {paths.begin(),paths.end()};
    }
private:
    static constexpr size_t MaxTextCacheBytes = 24 * 1024 * 1024;
    static constexpr size_t MaxCachedSourceBytes = 512 * 1024;
    Read read_; Exists exists_; List list_;
    mutable std::mutex cacheMutex_;
    mutable std::unordered_map<std::string,std::string> textCache_;
    mutable std::unordered_map<std::string,bool> existsCache_;
    mutable size_t textCacheBytes_ = 0;
    bool cacheText_ = true;
};
}
