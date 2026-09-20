#pragma once

#include <array>
#include <chrono>
#include <cstdint>

namespace wowee::rendering {

enum class StreamLoadStage : unsigned {
    Metadata, TextureLookup, TextureRead, DiffuseUpload, NormalDecode,
    NormalGenerate, NormalUpload, Geometry, Materials, UploadSubmit, Residency, Water, Count
};

// Exclusive CPU-wall buckets. Nested scopes pause their parent's bucket;
// upload/fence time must not also be reported as normal generation time.
struct StreamLoadTiming {
    std::array<int64_t, static_cast<unsigned>(StreamLoadStage::Count)> us{};
    StreamLoadStage stage = StreamLoadStage::Metadata;
    int64_t lastUs;
    unsigned textureHits = 0, preparedTextures = 0, syncTextures = 0;
    unsigned generatedNormals = 0, preparedNormals = 0;

    explicit StreamLoadTiming(int64_t now) : lastUs(now) {}
    void switchTo(StreamLoadStage next, int64_t now) {
        if (now >= lastUs) us[static_cast<unsigned>(stage)] += now - lastUs;
        lastUs = now;
        stage = next;
    }
    int64_t totalUs() const {
        int64_t total = 0;
        for (auto value : us) total += value;
        return total;
    }
};

inline int64_t streamLoadNowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct StreamLoadStageScope {
    StreamLoadTiming* timing;
    StreamLoadStage previous = StreamLoadStage::Metadata;
    StreamLoadStageScope(StreamLoadTiming* value, StreamLoadStage stage) : timing(value) {
        if (timing) {
            previous = timing->stage;
            timing->switchTo(stage, streamLoadNowUs());
        }
    }
    ~StreamLoadStageScope() {
        if (timing) timing->switchTo(previous, streamLoadNowUs());
    }
    StreamLoadStageScope(const StreamLoadStageScope&) = delete;
    StreamLoadStageScope& operator=(const StreamLoadStageScope&) = delete;
};

// Fixed-size, allocation-free rate limiter. Skipped events are counted in the
// next receipt, so a sparse log does not imply that streaming had no overruns.
struct StreamLoadLogGate {
    int64_t windowStartUs = 0;
    unsigned emitted = 0, suppressed = 0;
    bool allow(int64_t now) {
        if (now - windowStartUs >= 5000000) {
            windowStartUs = now;
            emitted = 0;
        }
        if (emitted < 4) { ++emitted; return true; }
        ++suppressed;
        return false;
    }
};

} // namespace wowee::rendering

#ifdef WOWEE_PS4
#include "core/logger.hpp"
namespace wowee::rendering {
inline thread_local StreamLoadTiming* activeStreamLoadTiming = nullptr;
struct StreamLoadDiagnostic {
    StreamLoadTiming timing{streamLoadNowUs()};
    StreamLoadTiming* previous = activeStreamLoadTiming;
    const char* kind;
    uint64_t id;
    float budgetMs;
    unsigned gateIndex;
    StreamLoadDiagnostic(const char* kind_, uint64_t id_, float budget, unsigned category = 0)
        : kind(kind_), id(id_), budgetMs(budget), gateIndex(category < 3 ? category : 0) { activeStreamLoadTiming = &timing; }
    ~StreamLoadDiagnostic() {
        const auto now = streamLoadNowUs();
        timing.switchTo(StreamLoadStage::Metadata, now);
        activeStreamLoadTiming = previous;
        if (timing.totalUs() < 4000) return;
        static thread_local std::array<StreamLoadLogGate, 3> gates;
        auto& gate = gates[gateIndex];
        if (!gate.allow(now)) return;
        const auto ms = [&](StreamLoadStage stage) {
            return timing.us[static_cast<unsigned>(stage)] * .001;
        };
        try {
        LOG_INFO("[STREAM_LOAD_DETAIL] kind=", kind, " id=", id,
            " totalMs=", timing.totalUs() * .001, " budgetMs=", budgetMs,
            " metadataMs=", ms(StreamLoadStage::Metadata),
            " textureLookupMs=", ms(StreamLoadStage::TextureLookup),
            " textureReadMs=", ms(StreamLoadStage::TextureRead),
            " diffuseUploadMs=", ms(StreamLoadStage::DiffuseUpload),
            " normalDecodeMs=", ms(StreamLoadStage::NormalDecode),
            " normalGenerateMs=", ms(StreamLoadStage::NormalGenerate),
            " normalUploadMs=", ms(StreamLoadStage::NormalUpload),
            " geometryMs=", ms(StreamLoadStage::Geometry),
            " materialsMs=", ms(StreamLoadStage::Materials),
            " uploadSubmitMs=", ms(StreamLoadStage::UploadSubmit),
            " residencyMs=", ms(StreamLoadStage::Residency),
            " waterMs=", ms(StreamLoadStage::Water),
            " textureHits=", timing.textureHits, " preparedTextures=", timing.preparedTextures,
            " syncTextures=", timing.syncTextures, " generatedNormals=", timing.generatedNormals,
            " preparedNormals=", timing.preparedNormals, " suppressed=", gate.suppressed,
            " timingDomain=exclusive-cpu-wall uploadIncludesWait=1");
        gate.suppressed = 0;
        } catch (...) {
            // Diagnostics must not turn memory pressure or exception unwinding
            // into a new streaming failure. The active context is already restored.
        }
    }
    StreamLoadDiagnostic(const StreamLoadDiagnostic&) = delete;
};
} // namespace wowee::rendering
#else
namespace wowee::rendering {
inline constexpr StreamLoadTiming* activeStreamLoadTiming = nullptr;
struct StreamLoadDiagnostic { StreamLoadDiagnostic(const char*, uint64_t, float, unsigned = 0) {} };
}
#endif
