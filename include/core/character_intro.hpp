#pragma once
#include "pipeline/character_intro.hpp"
#include <optional>
namespace wowee::core {
struct CharacterIntroFrame {
    glm::vec3 canonicalPosition{0.0f};
    glm::vec3 canonicalTarget{0.0f};
    float rollRadians = 0.0f;
    float diagonalFovRadians = 0.0f;
    uint32_t mapId = 0;
    uint32_t sequenceId = 0;
    uint32_t cameraId = 0;
    size_t shotIndex = 0;
    uint32_t shotTimeMs = 0;
    uint32_t sequenceTimeMs = 0;
};
class CharacterIntro {
public:
    bool start(pipeline::CharacterIntroPlan plan);
    void cancel();
    // Keep time/narration paused while the next camera tile streams. The caller
    // owns terrain requests, audio pause, first-login persistence and controls.
    void advance(float deltaSeconds, bool terrainReady);
    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] bool paused() const { return paused_; }
    [[nodiscard]] bool finished() const { return finished_; }
    [[nodiscard]] bool failed() const { return failed_; }
    [[nodiscard]] std::optional<CharacterIntroFrame> frame() const;
    [[nodiscard]] std::optional<CharacterIntroFrame> peekAhead(uint32_t milliseconds) const;
    [[nodiscard]] const pipeline::CharacterIntroPlan& plan() const { return plan_; }
private:
    std::optional<CharacterIntroFrame> sample(double timeMs) const;
    pipeline::CharacterIntroPlan plan_;
    double elapsedMs_ = 0;
    bool active_ = false, paused_ = false, finished_ = false, failed_ = false;
};
} // namespace wowee::core
