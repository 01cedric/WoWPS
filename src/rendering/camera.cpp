#include "rendering/camera.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

namespace wowee {
namespace rendering {

Camera::Camera() {
    updateViewMatrix();
    updateProjectionMatrix();
}

void Camera::updateViewMatrix() {
    const glm::vec3 front = getForward();
    const glm::vec3 right = getRight();
    const glm::vec3 up = glm::cross(right, front);
    // Build the same Z-up view directly from its orthonormal basis. Forming
    // position + front rounds a one-unit direction at large world coordinates
    // and made orientation wobble as a perfectly smooth camera translated.
    viewMatrix = glm::mat4(1.0f);
    viewMatrix[0][0] = right.x; viewMatrix[1][0] = right.y; viewMatrix[2][0] = right.z;
    viewMatrix[0][1] = up.x;    viewMatrix[1][1] = up.y;    viewMatrix[2][1] = up.z;
    viewMatrix[0][2] = -front.x;viewMatrix[1][2] = -front.y;viewMatrix[2][2] = -front.z;
    viewMatrix[3][0] = -glm::dot(right, position);
    viewMatrix[3][1] = -glm::dot(up, position);
    viewMatrix[3][2] = glm::dot(front, position);
}

void Camera::updateProjectionMatrix() {
    projectionMatrix = glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
    // Vulkan clip-space has Y pointing down; flip the projection's Y axis.
    projectionMatrix[1][1] *= -1.0f;
    unjitteredProjectionMatrix = projectionMatrix;

    // Re-apply jitter if active
    if (jitterOffset.x != 0.0f || jitterOffset.y != 0.0f) {
        projectionMatrix[2][0] += jitterOffset.x;
        projectionMatrix[2][1] += jitterOffset.y;
    }
}

glm::vec3 Camera::getForward() const {
    // WoW coordinate system: X/Y horizontal, Z vertical
    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.z = sin(glm::radians(pitch));
    return glm::normalize(front);
}

glm::vec3 Camera::getRight() const {
    // Use Z-up for WoW coordinate system. If forward is parallel to the up
    // axis (camera staring straight up/down), cross is zero and normalize
    // returns NaN - fall back to world +X so view/proj stay finite.
    glm::vec3 c = glm::cross(getForward(), glm::vec3(0.0f, 0.0f, 1.0f));
    float len = glm::length(c);
    const glm::vec3 right = len < 1e-6f ? glm::vec3(1.0f, 0.0f, 0.0f) : c / len;
    const glm::vec3 up = glm::normalize(glm::cross(right, getForward()));
    return right * std::cos(roll) + up * std::sin(roll);
}

glm::vec3 Camera::getUp() const {
    glm::vec3 c = glm::cross(getRight(), getForward());
    float len = glm::length(c);
    if (len < 1e-6f) return glm::vec3(0.0f, 0.0f, 1.0f);
    return c / len;
}

void Camera::setJitter(float jx, float jy) {
    // Sub-pixel jitter for temporal anti-aliasing (TAA / FSR2).
    // Column 2 of the projection matrix holds the NDC x/y offset - modifying
    // [2][0] and [2][1] shifts the entire rendered image by a sub-pixel amount
    // each frame, giving the upscaler different sample positions to reconstruct.
    projectionMatrix[2][0] -= jitterOffset.x;
    projectionMatrix[2][1] -= jitterOffset.y;
    jitterOffset = glm::vec2(jx, jy);
    projectionMatrix[2][0] += jitterOffset.x;
    projectionMatrix[2][1] += jitterOffset.y;
}

void Camera::clearJitter() {
    projectionMatrix[2][0] -= jitterOffset.x;
    projectionMatrix[2][1] -= jitterOffset.y;
    jitterOffset = glm::vec2(0.0f);
}

Ray Camera::screenToWorldRay(float screenX, float screenY, float screenW, float screenH) const {
    float ndcX = (2.0f * screenX / screenW) - 1.0f;
    // Vulkan Y-flip is baked into projectionMatrix, so NDC Y maps directly:
    // screen top (y=0) → NDC -1, screen bottom (y=H) → NDC +1
    float ndcY = (2.0f * screenY / screenH) - 1.0f;

    glm::mat4 invVP = glm::inverse(projectionMatrix * viewMatrix);

    // Vulkan / GLM_FORCE_DEPTH_ZERO_TO_ONE: NDC z ∈ [0, 1]
    glm::vec4 nearPt = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::vec4 farPt  = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    nearPt /= nearPt.w;
    farPt  /= farPt.w;

    return { .origin = glm::vec3(nearPt), .direction = glm::normalize(glm::vec3(farPt - nearPt)) };
}

} // namespace rendering
} // namespace wowee
