// Exercise the exact packaged PSBC archive and bloom descriptor ABI.
#include "psbc_compile.h"
extern "C" {
#include "gnm_helpers.h"
}
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

int main(int argc, char** argv) {
    std::setbuf(stdout, nullptr);
    if (argc != 2) return 2;
    FILE* file = std::fopen(argv[1], "rb");
    if (!file) return 3;
    std::fseek(file, 0, SEEK_END);
    long bytes = std::ftell(file);
    std::rewind(file);
    if (bytes < 20 || bytes % 4) return 4;
    std::vector<uint32_t> source(static_cast<size_t>(bytes)/4);
    size_t read = std::fread(source.data(), 1, bytes, file);
    std::fclose(file);
    if (read != static_cast<size_t>(bytes)) return 5;
    // Combined source sampler at0 and16-byte parameters UBO at1.
    // Descriptor table stride is48+16 bytes, matching PS4 pipeline lowering.
    PsbcDescriptorBinding bindings[] = {
        {0,1,1,0,48,UINT32_MAX}, {1,6,1,48,16,UINT32_MAX}
    };
    PsbcDescriptorSetLayout set{2,64,0,bindings};
    PsbcCompileOptions options{};
    options.target = PSBC_TARGET_PS4_BASE;
    options.stage = PSBC_STAGE_FRAGMENT;
    options.entrypoint = "main";
    options.optimise = true;
    options.descriptor_set_count = 1;
    options.descriptor_sets = &set;
    options.descriptor_address32_hi = 2;
    PsbcShaderOutput output{};
    psbc_init();
    auto result = psbc_compile_shader(source.data(), bytes, &options, &output);
    std::printf("PSBC GFX7 FS %s: result=%u %s binaryBytes=%zu\n",
                argv[1], result, psbc_result_string(result), output.size);
    if (result != PSBC_RESULT_OK) return 6;
    GnmShaderMetadata metadata{};
    auto rc = sceGnmShaderBinaryGetMetadata(output.data, output.size, &metadata);
    assert(rc == 0 && metadata.type == GNM_SHADER_PIXEL);
    assert(metadata.shadercode && metadata.shadercodesize > 0);
    const auto* ps = static_cast<const GnmPsShader*>(metadata.stage);
    assert(ps);
    const uint32_t colorFormat = ps->registers.spishadercolformat & 0xf;
    // Both a four-component FP32 export (converted by CB) and packed FP16
    // export are legal for the bloom target. UNORM exports would
    // destroy weak radiance; all RGBA channels must be enabled.
    assert(colorFormat == GNM_PS_EXP_FMT_FP16_ABGR ||
           colorFormat == GNM_PS_EXP_FMT_32_ABGR);
    assert((ps->registers.cbshadermask & 0xf) == 0xf);
    std::printf("  export format=0x%x cb-mask=0x%x two-binding bytes=%u\n",
                colorFormat, ps->registers.cbshadermask, 64u);
    bool descriptorTable = false;
    for (uint32_t i = 0; i < metadata.numinputusageslots; ++i) {
        const auto& slot = metadata.inputusageslots[i];
        assert(slot.startregister < 16);
        if (slot.usagetype == GNM_SHINPUTUSAGE_PTR_INDIRECTRESOURCETABLE)
            descriptorTable |= slot.apislot == 0;
        std::printf("  usage type=%u api=%u register=%u\n",
                    slot.usagetype, slot.apislot, slot.startregister);
    }
    assert(descriptorTable);
    std::printf("PASS GFX7 shader codeBytes=%u inputs=%u descriptor table valid\n",
                metadata.shadercodesize, metadata.numinputsemantics);
    psbc_free_output(&output);
    psbc_shutdown();
}
