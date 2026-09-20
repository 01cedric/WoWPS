// Compile the packaged shadow shaders with the production descriptor ABI.
// Checks compiler/register metadata, not GPU pixels or performance.
#include "psbc_compile.h"
extern "C" {
#include "gnm_helpers.h"
#include "pm4/amdgfxregs.h"
}
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::setbuf(stdout, nullptr);
    if (argc != 3) return 2;
    const std::string kind(argv[2]);
    const bool fragment = kind == "frag";
    assert(fragment || kind == "vert");
    FILE* file = std::fopen(argv[1], "rb");
    if (!file) return 3;
    std::fseek(file, 0, SEEK_END);
    const long bytes = std::ftell(file);
    std::rewind(file);
    if (bytes < 20 || bytes % 4) return 4;
    std::vector<uint32_t> source(static_cast<size_t>(bytes) / 4);
    const size_t read = std::fread(source.data(), 1, bytes, file);
    std::fclose(file);
    if (read != static_cast<size_t>(bytes)) return 5;

    // Identical binding order/offsets to vk_ps4_CreateDescriptorSetLayout.
    PsbcDescriptorBinding frame[] = {{0,6,1,0,16,UINT32_MAX}, {1,1,1,16,48,UINT32_MAX}};
    PsbcDescriptorBinding material[] = {{0,6,1,0,16,UINT32_MAX}};
    PsbcDescriptorBinding captures[] = {{0,1,1,0,48,UINT32_MAX},{1,1,1,48,48,UINT32_MAX},
                                     {2,1,1,96,48,UINT32_MAX},{3,6,1,144,16,UINT32_MAX}};
    PsbcDescriptorSetLayout sets[] = {{2,64,0,frame},{1,16,0,material},{4,160,0,captures}};
    PsbcCompileOptions options{};
    options.target = PSBC_TARGET_PS4_BASE;
    options.stage = fragment ? PSBC_STAGE_FRAGMENT : PSBC_STAGE_VERTEX;
    options.entrypoint = "main";
    options.optimise = true;
    options.descriptor_set_count = 3;
    options.descriptor_sets = sets;
    options.descriptor_address32_hi = 2;
    PsbcShaderOutput output{};
    psbc_init();
    const auto result = psbc_compile_shader(source.data(), bytes, &options, &output);
    std::printf("PSBC GFX7 %s result=%u %s binaryBytes=%zu\n", kind.c_str(), result,
                psbc_result_string(result), output.size);
    if (result != PSBC_RESULT_OK) return 6;
    GnmShaderMetadata metadata{};
    assert(sceGnmShaderBinaryGetMetadata(output.data, output.size, &metadata) == 0);
    assert(metadata.type == (fragment ? GNM_SHADER_PIXEL : GNM_SHADER_VERTEX));
    assert(metadata.shadercode && metadata.shadercodesize > 0);
    uint32_t setMask = 0, registers = 0;
    bool pushPointer = false, validityDword = false, pushAny = false;
    bool fetch = false, vertices = false;
    for (uint32_t i = 0; i < metadata.numinputusageslots; ++i) {
        const auto& slot = metadata.inputusageslots[i];
        const bool wide = slot.usagetype == GNM_SHINPUTUSAGE_SUBPTR_FETCHSHADER ||
                          slot.usagetype == GNM_SHINPUTUSAGE_PTR_VERTEXBUFFERTABLE;
        const uint32_t width = wide ? 2 : 1;
        assert(slot.startregister + width <= 16);
        const uint32_t bits = ((1u << width) - 1u) << slot.startregister;
        assert((registers & bits) == 0); // compact descriptor/push pointers never clobber neighbors
        registers |= bits;
        if (slot.usagetype == GNM_SHINPUTUSAGE_PTR_INDIRECTRESOURCETABLE) {
            assert(slot.apislot < options.descriptor_set_count);
            setMask |= 1u << slot.apislot;
        }
        if (slot.usagetype == GNM_SHINPUTUSAGE_PTR_CONSTBUFFERTABLE && slot.apislot == 254)
            pushPointer = true;
        if (slot.usagetype == GNM_SHINPUTUSAGE_IMM_ALUFLOATCONST && slot.apislot < 254) {
            assert(slot.apislot < 32);
            pushAny = true;
            validityDword |= slot.apislot == 24 || slot.apislot == 25; // captureValid offset96
        }
        fetch |= slot.usagetype == GNM_SHINPUTUSAGE_SUBPTR_FETCHSHADER;
        vertices |= slot.usagetype == GNM_SHINPUTUSAGE_PTR_VERTEXBUFFERTABLE;
        std::printf(" usage type=%u api=%u reg=%u\n", slot.usagetype, slot.apislot, slot.startregister);
    }
    assert(setMask == (fragment ? 7u : 1u));
    assert(pushPointer || pushAny);
    if (fragment) {
        const auto* shader = static_cast<const GnmPsShader*>(metadata.stage);
        assert((shader->registers.cbshadermask & 15) == 15);
        assert(pushPointer || validityDword);
        constexpr uint32_t positionXYZ = (1u<<8)|(1u<<9)|(1u<<10);
        assert((shader->registers.spipsinputena & positionXYZ) == positionXYZ);
    } else {
        assert(fetch && vertices);
        assert(metadata.numinputsemantics == 2);
    }
    std::printf("PASS water %s codeBytes=%u descriptorMask=0x%x push104 bytes\n",
                kind.c_str(), metadata.shadercodesize, setMask);
    psbc_free_output(&output);
    psbc_shutdown();
}
