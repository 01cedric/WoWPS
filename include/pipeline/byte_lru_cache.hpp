#pragma once
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace wowee::pipeline {
// Caller serializes access. Returned immutable handles may outlive eviction;
// bytes() counts cache ownership, not memory still held by active readers.
class ByteLruCache {
public:
    using Bytes = std::vector<uint8_t>;
    using Handle = std::shared_ptr<const Bytes>;
    ByteLruCache() = default;
    ByteLruCache(const ByteLruCache&) = delete;
    ByteLruCache& operator=(const ByteLruCache&) = delete;
    Handle get(const std::string& key) {
        auto it = entries_.find(key);
        if (it == entries_.end()) return {};
        order_.splice(order_.begin(), order_, it->second.order);
        return it->second.data;
    }
    bool put(const std::string& key, Handle data, size_t budget) {
        if (!data || data->empty() || data->size() > budget) return false;
        if (get(key)) return false;
        const size_t size = data->size();
        // Allocate metadata before eviction, with rollback if insertion fails.
        order_.push_front(key);
        try { entries_.emplace(key, Entry{std::move(data), order_.begin()}); }
        catch (...) { order_.pop_front(); throw; }
        bytes_ += size;
        trim(budget);
        return true;
    }
    size_t trim(size_t budget) {
        const size_t before = bytes_;
        while (bytes_ > budget && !order_.empty()) erase(order_.back());
        return before - bytes_;
    }
    void erase(const std::string& key) {
        auto it = entries_.find(key);
        if (it == entries_.end()) return;
        bytes_ -= it->second.data->size();
        order_.erase(it->second.order);
        entries_.erase(it);
    }
    void clear() { entries_.clear(); order_.clear(); bytes_ = 0; }
    size_t bytes() const { return bytes_; }
private:
    struct Entry { Handle data; std::list<std::string>::iterator order; };
    std::list<std::string> order_;
    std::unordered_map<std::string, Entry> entries_;
    size_t bytes_ = 0;
};
}
