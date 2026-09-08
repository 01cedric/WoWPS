#pragma once
#include "pipeline/m2_loader.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace wowee::pipeline {
struct IntroSound {
    uint32_t id = 0;
    std::string path;
    float volume = 1.0f;
};
struct CharacterIntroShot {
    uint32_t cameraId = 0;
    uint32_t durationMs = 0;
    std::string modelPath;
    glm::vec3 originServer{0.0f};
    float originFacing = 0.0f;
    M2Camera camera;
    IntroSound narration;
};
struct CharacterIntroPlan {
    uint32_t sequenceId = 0;
    uint32_t mapId = 0;
    uint32_t durationMs = 0;
    IntroSound sequenceSound;
    std::vector<CharacterIntroShot> shots;
    size_t decodedBytes = 0;
};
// The provider must apply maxBytes before allocating where its backend supports
// a file-size query. The loader checks again and retains only compact tracks.
using IntroReadFile = std::function<std::vector<uint8_t>(const std::string&, size_t maxBytes)>;
// Stock WotLK 3.3.5a / 12340 DBC schemas. A class cinematic overrides the race
// cinematic when nonzero. No filename, camera flight or narration is invented.
// Any missing/invalid camera rejects the whole sequence; gameplay may proceed.
bool loadCharacterIntro(uint8_t race, uint8_t classId, uint32_t mapId,
                        const IntroReadFile& read, CharacterIntroPlan& out,
                        std::string& reason);
// Camera-only MD20 parser; never decodes mesh/skin/texture/bone payloads.
bool parseCharacterIntroCamera(const std::vector<uint8_t>& bytes,
                               M2Camera& out, uint32_t& durationMs,
                               size_t& decodedBytes, std::string& reason);
} // namespace wowee::pipeline
