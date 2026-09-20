#include "rendering/m2_shadow_slice.hpp"
#include "rendering/m2_shadow_lod.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <limits>
#include <vector>
using namespace wowee::rendering;
using Matrix = std::array<float, 16>;
int main() {
    // Near pass must never LOD away any caster, including zero-radius models.
    for (float r : {0.0f, 0.01f, 1.0f, 100.0f})
        assert(!m2FarShadowSubtexel(0, 1000.0f, r, 3.0f, false));
    assert(m2FarShadowSubtexel(1, 0.5f, 0.01f, 3.0f, false));
    assert(!m2FarShadowSubtexel(1, 0.5f, 0.01f, 3.0f, true));
    // Matrix-import scale is not the scalar instance.scale (which can be 1).
    assert(!m2FarShadowSubtexel(1, 0.5f, 0.01f, 30000.0f, false));
    assert(!m2FarShadowSubtexel(1, 0.5f, -1.0f, 3.0f, false));
    assert(!m2FarShadowSubtexel(1, 0.0f, 0.01f, 3.0f, false));
    assert(!m2FarShadowSubtexel(1, 0.5f, 0.01f,
                              std::numeric_limits<float>::infinity(), false));
    assert(!m2FarShadowSubtexel(1, 0.5f,
                              std::numeric_limits<float>::quiet_NaN(), 3.0f, false));
    static_assert(sizeof(Matrix) == M2ShadowSlice::matrixBytes);
    const size_t count = M2ShadowSlice::bufferBytes / sizeof(Matrix);
    std::array<std::vector<Matrix>, 2> frames{std::vector<Matrix>(count), std::vector<Matrix>(count)};
    for (unsigned frame = 0; frame < 2; ++frame) {
        // Different caster count and placement per pass, intentionally overlap
        // local indices: old base-zero upload would clobber near transforms.
        for (unsigned pass = 0; pass < 2; ++pass) {
            const auto slice = m2ShadowSlice(pass, pass ? 4093 : 173);
            assert(slice.valid && slice.byteOffset == size_t(slice.matrixOffset) * sizeof(Matrix));
            assert(slice.byteOffset + slice.byteSize <= M2ShadowSlice::bufferBytes);
            for (size_t i = 0; i < slice.byteSize / sizeof(Matrix); ++i)
                frames[frame][slice.matrixOffset + i].fill(float(100000 * frame + 10000 * pass + i));
        }
    }
    for (unsigned frame = 0; frame < 2; ++frame)
        for (unsigned pass = 0; pass < 2; ++pass) {
            const auto slice = m2ShadowSlice(pass, pass ? 4093 : 173);
            const size_t n = slice.byteSize / sizeof(Matrix);
            // Match shader lookup: offset = slice + UV-run start, plus
            // gl_InstanceIndex from zero-firstInstance draws.
            for (size_t run = 0; run < n; run += 7)
                for (size_t instance = 0; instance < 7 && run + instance < n; ++instance)
                    for (float v : frames[frame][slice.matrixOffset + run + instance])
                        assert(v == float(100000 * frame + 10000 * pass + run + instance));
        }
    const auto nearFull = m2ShadowSlice(0, M2ShadowSlice::capacity);
    const auto farFull = m2ShadowSlice(1, M2ShadowSlice::capacity);
    assert(nearFull.byteOffset + nearFull.byteSize == farFull.byteOffset);
    assert(farFull.byteOffset + farFull.byteSize == M2ShadowSlice::bufferBytes);
    assert(m2ShadowSlice(0, 0).valid && m2ShadowSlice(1, 0).byteSize == 0);
    assert(!m2ShadowSlice(2, 1).valid);
    assert(!m2ShadowSlice(UINT32_MAX, 1).valid);
    assert(!m2ShadowSlice(0, size_t(M2ShadowSlice::capacity) + 1).valid);
    assert(!m2ShadowSlice(1, std::numeric_limits<size_t>::max()).valid);
    std::puts("PASS M2 near/far slices and LOD: near never culled, far wind/scale/invalid safeguards, frame isolation, immutable near transforms, shader UV-run indexing, flush byte ranges, empty/full/overflow");
}
