#pragma once

#include <glm/glm.hpp>
#if defined(__SSE2__) && !defined(WOWEE_DISABLE_SIMD)
#include <emmintrin.h>
#endif

namespace wowee::rendering {
// Preserve GLM's column-major layout and left-to-right summation; unaligned
// loads keep this independent of GLM alignment configuration and matrix ABI.
inline glm::mat4 multiplyBoneMatrices(const glm::mat4& a, const glm::mat4& b) {
#if defined(__SSE2__) && !defined(WOWEE_DISABLE_SIMD)
    const __m128 a0 = _mm_loadu_ps(&a[0][0]);
    const __m128 a1 = _mm_loadu_ps(&a[1][0]);
    const __m128 a2 = _mm_loadu_ps(&a[2][0]);
    const __m128 a3 = _mm_loadu_ps(&a[3][0]);
    glm::mat4 result;
    for (int col = 0; col < 4; ++col) {
        __m128 sum = _mm_mul_ps(a0, _mm_set1_ps(b[col][0]));
        sum = _mm_add_ps(sum, _mm_mul_ps(a1, _mm_set1_ps(b[col][1])));
        sum = _mm_add_ps(sum, _mm_mul_ps(a2, _mm_set1_ps(b[col][2])));
        sum = _mm_add_ps(sum, _mm_mul_ps(a3, _mm_set1_ps(b[col][3])));
        _mm_storeu_ps(&result[col][0], sum);
    }
    return result;
#else
    return a * b;
#endif
}
} // namespace wowee::rendering
