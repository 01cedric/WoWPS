#pragma once
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace wowee::core {
// Single producer; one active write and at most one pending immutable snapshot.
// New autosaves coalesce. flush() is the barrier before manual/transactional
// saves or loading another character from the same file.
class SnapshotWriter {
public:
    using Bytes = std::vector<uint8_t>;
    using Write = std::function<bool(const std::string&, const Bytes&)>;
    explicit SnapshotWriter(Write write) : write_(std::move(write)) {}
    SnapshotWriter(const SnapshotWriter&) = delete;
    SnapshotWriter& operator=(const SnapshotWriter&) = delete;
    ~SnapshotWriter() {
        flush();
        { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
        wake_.notify_one();
        if (thread_.joinable()) thread_.join();
    }
    void submit(std::string path, Bytes bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        // If thread creation fails, no snapshot has been accepted/lost.
        if (!thread_.joinable()) thread_ = std::thread([this] { run(); });
        pending_ = Job{std::move(path), std::move(bytes)};
        wake_.notify_one();
    }
    void flush() {
        std::unique_lock<std::mutex> lock(mutex_);
        drained_.wait(lock, [this] { return !busy_ && !pending_; });
    }
    bool takeFailure() {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool result = failed_; failed_ = false; return result;
    }
private:
    struct Job { std::string path; Bytes bytes; };
    void run() {
        for (;;) {
            std::optional<Job> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
                if (stopping_) return;
                job = std::move(pending_); pending_.reset(); busy_ = true;
            }
            bool success = false;
            try { success = write_(job->path, job->bytes); } catch (...) {}
            job.reset(); // release payload before advertising the drained state
            {
                std::lock_guard<std::mutex> lock(mutex_);
                failed_ = failed_ || !success; busy_ = false;
            }
            drained_.notify_all();
        }
    }
    Write write_;
    std::mutex mutex_;
    std::condition_variable wake_, drained_;
    std::optional<Job> pending_;
    std::thread thread_;
    bool stopping_ = false, busy_ = false, failed_ = false;
};
}
