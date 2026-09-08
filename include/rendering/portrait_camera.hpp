#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
namespace wowee::rendering {
struct PortraitCameraPose { glm::vec3 eye, target; };
inline PortraitCameraPose placePortraitCamera(const glm::vec3& eye, const glm::vec3& target,
                                              const glm::vec3& origin, float yawDegrees) {
    const auto rotation = glm::rotate(glm::mat4(1.f), glm::radians(yawDegrees), glm::vec3(0,0,1));
    return {origin + glm::vec3(rotation * glm::vec4(eye,1.f)),
            origin + glm::vec3(rotation * glm::vec4(target,1.f))};
}
}
