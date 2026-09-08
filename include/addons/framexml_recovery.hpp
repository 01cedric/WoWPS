#pragma once
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

namespace wowee::addons {
// Bump only when the bootstrap/recovery implementation materially changes.
inline constexpr const char* kFrameXmlRecoveryPolicy = "B36-FrameXML-1";
inline std::string frameXmlRecoveryPath(const std::string& configRoot) {
    return configRoot + "/framexml-session.state";
}
struct FrameXmlRecoveryRecord {
    bool present = false, readable = true;
    char policy[48]{}, state[24]{}, stage[160]{};
    bool allowsAttempt() const {
        if (!readable) return false;
        if (!present) return true;
        if (std::strcmp(policy, kFrameXmlRecoveryPolicy) != 0) return true;
        return std::strcmp(state, "clean") == 0;
    }
};
inline FrameXmlRecoveryRecord readFrameXmlRecovery(const std::string& path) {
    FrameXmlRecoveryRecord record;
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) { record.readable = errno == ENOENT; return record; }
    record.present = true;
    const auto line = [&](char* out, size_t size) {
        if (!std::fgets(out, static_cast<int>(size), file)) return false;
        const size_t length = std::strlen(out);
        if (!length || out[length-1] != '\n') return false;
        out[length-1] = 0;
        return true;
    };
    record.readable = line(record.policy, sizeof(record.policy)) &&
        line(record.state, sizeof(record.state)) && line(record.stage, sizeof(record.stage)) &&
        std::fgetc(file) == EOF && !std::ferror(file);
    std::fclose(file);
    // A policy version change permits one new attempt; corrupted magic does not.
    const char* policy = record.policy;
    bool validPolicy = *policy++ == 'B';
    const char* digits = policy;
    while (*policy >= '0' && *policy <= '9') ++policy;
    validPolicy = validPolicy && policy != digits && std::strncmp(policy, "-FrameXML-", 10) == 0;
    if (validPolicy) {
        policy += 10;
        digits = policy;
        while (*policy >= '0' && *policy <= '9') ++policy;
        validPolicy = policy != digits && !*policy;
    }
    if (!validPolicy) record.readable = false;
    if (record.readable && std::strcmp(record.state,"clean") &&
        std::strcmp(record.state,"loading") && std::strcmp(record.state,"active") &&
        std::strcmp(record.state,"failed")) record.readable = false;
    return record;
}
inline bool writeFrameXmlRecovery(const std::string& path, const char* state, const char* stage) {
    if (!state || (std::strcmp(state,"clean") && std::strcmp(state,"loading") &&
        std::strcmp(state,"active") && std::strcmp(state,"failed"))) return false;
    // Small bounded FILE reads/writes; never size an allocation from PS4 stat.
    char safeStage[160]{};
    size_t count = 0;
    if (stage) for (; *stage && count < sizeof(safeStage)-2; ++stage)
        safeStage[count++] = (*stage == '\n' || *stage == '\r') ? ' ' : *stage;
    const std::string temporary = path + ".tmp";
    FILE* file = std::fopen(temporary.c_str(), "wb");
    if (!file) return false;
    bool ok = std::fprintf(file, "%s\n%s\n%s\n", kFrameXmlRecoveryPolicy, state, safeStage) > 0;
    ok = std::fflush(file) == 0 && ok;
    if (ok) ok = ::fsync(::fileno(file)) == 0;
    ok = std::fclose(file) == 0 && ok;
    if (ok) ok = std::rename(temporary.c_str(), path.c_str()) == 0;
    if (!ok) std::remove(temporary.c_str());
    return ok;
}
} // namespace wowee::addons
