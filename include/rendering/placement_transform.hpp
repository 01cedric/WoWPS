#pragma once

/// The model matrix a placement's position, rotation and scale become.
///
/// MDDF places doodads and MODF places buildings, and both store the same three
/// degrees of rotation the same way. The two renderers composed them into a
/// matrix separately, and that difference cost a long time to find: the
/// buildings were composed Z, Y, X and the doodads X, Y, Z, and with no pitch
/// and no roll every composition order is the same rotation. An upright tree or
/// a building on flat ground looks correct whichever order built it, so four
/// attempts to change the doodad order were each judged against evidence that
/// could not tell them apart, and each was reported worse.
///
/// Darkshore's bridges are the case that can say something - WMOs, crossing
/// ravines, with real pitch - and they were visibly askew. The tell that it was
/// the composition rather than a constant: a yaw offset of -10 degrees stood
/// the bridges up and put every building out, which is how an offset behaves
/// when it is standing in for something that varies with the placement's own
/// rotation. Both compose X, Y, Z now.
///
/// One function so the next person to change the order changes it for both, and
/// so that a difference between them is a failing test rather than a building
/// that looks fine until it is on a slope.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace wowee::rendering {

/// Translate, then rotate X, Y, Z, then scale uniformly.
///
/// `eulerRadians` is the placement's rotation already mapped into render axes.
/// The order is the whole point of this living in one place; see above.
inline glm::mat4 placementModelMatrix(const glm::vec3& position,
                                      const glm::vec3& eulerRadians,
                                      float scale) {
    glm::mat4 m(1.0f);
    m = glm::translate(m, position);
    m = glm::rotate(m, eulerRadians.x, glm::vec3(1.0f, 0.0f, 0.0f));
    m = glm::rotate(m, eulerRadians.y, glm::vec3(0.0f, 1.0f, 0.0f));
    m = glm::rotate(m, eulerRadians.z, glm::vec3(0.0f, 0.0f, 1.0f));
    m = glm::scale(m, glm::vec3(scale));
    return m;
}

}  // namespace wowee::rendering
