#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace wowee::pipeline {
struct M2Model;
// Weak ownership avoids keeping decoded models alive after all previews close.
// Keys contain the model and every resolved appearance texture, not just race.
class PreviewModelCache {
public:
    using Key=std::vector<std::string>;
    std::shared_ptr<const M2Model> find(const Key& key, uint64_t& generation) {
        std::lock_guard<std::mutex> lock(mutex_);
        generation=generation_;
        auto it=models_.find(key);
        return it==models_.end()?nullptr:it->second.lock();
    }
    void remember(const Key& key, uint64_t generation, const std::shared_ptr<const M2Model>& model) {
        if (!model) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation!=generation_) return; // Data source changed during load.
        for (auto it=models_.begin();it!=models_.end();)
            if (it->second.expired()) it=models_.erase(it); else ++it;
        if (models_.size()>=64) return; // Cache metadata is bounded too.
        try { models_.insert_or_assign(key,model); }
        catch (const std::bad_alloc&) {} // Caching is optional; the model is valid.
    }
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;models_.clear();
    }
private:
    std::mutex mutex_;
    uint64_t generation_=0;
    std::map<Key,std::weak_ptr<const M2Model>> models_;
};
} // namespace wowee::pipeline
