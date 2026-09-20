#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
namespace wowee::rendering {
// Input-keyed, not frame-keyed: movement/turning/scale changes are visible to
// every caller immediately, including attachment and reflection queries.
class CharacterPlacementCache {
public:
    glm::mat4 get(const glm::vec3& position, const glm::vec3& rotation, float scale) {
        if (!valid_ || position != position_ || rotation != rotation_ || scale != scale_) {
            // Preserve the character's original Z, X, Y order. World doodad
            // placement uses a different order and must not be substituted.
            glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
            model = glm::rotate(model, rotation.z, glm::vec3(0,0,1));
            model = glm::rotate(model, rotation.x, glm::vec3(1,0,0));
            model = glm::rotate(model, rotation.y, glm::vec3(0,1,0));
            matrix_ = glm::scale(model, glm::vec3(scale));
            position_ = position; rotation_ = rotation; scale_ = scale;
            valid_ = true;
            ++rebuilds_;
        }
        return matrix_;
    }
    uint64_t rebuilds() const { return rebuilds_; }
private:
    glm::vec3 position_{}, rotation_{};
    glm::mat4 matrix_{1.0f};
    float scale_ = 1.0f;
    bool valid_ = false;
    uint64_t rebuilds_ = 0;
};
}
