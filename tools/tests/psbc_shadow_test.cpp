// Validate the packaged SPIR-V using the same PSBC archive and descriptor
// layout as PS4 pipeline creation. This is a host compiler test, not a GPU test.
#include "psbc_compile.h"
extern "C" {
#include "gnm_helpers.h"
}
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cassert>

int main(int argc, char** argv) {
    std::setbuf(stdout, nullptr);
    if (argc < 2) return 2;
    FILE* file = std::fopen(argv[1], "rb");
    if (!file) return 3;
    std::fseek(file, 0, SEEK_END);
    const long bytes = std::ftell(file);
    std::rewind(file);
    if (bytes < 20 || bytes % 4 != 0) return 4;
    std::vector<uint32_t> source(static_cast<size_t>(bytes) / 4);
    const size_t read = std::fread(source.data(), 1, bytes, file);
    std::fclose(file);
    if (read != static_cast<size_t>(bytes)) return 5;
    const bool fragment = argc > 2;
    PsbcCompileOptions options{};
    options.target = PSBC_TARGET_PS4_BASE;
    options.stage = fragment ? PSBC_STAGE_FRAGMENT : PSBC_STAGE_VERTEX;
    options.entrypoint = "main";
    options.optimise = true;
    // vk_ps4_pipeline.c: image+sampler=48B, buffer=16B, all 16B-aligned.
    PsbcDescriptorBinding params[] = {{0,1,1,0,48,UINT32_MAX}, {1,6,1,48,16,UINT32_MAX}};
    PsbcDescriptorBinding matrices[] = {{0,7,1,0,16,UINT32_MAX}};
    PsbcDescriptorSetLayout sets[] = {{2,64,0,params}, {1,16,0,matrices}};
    options.descriptor_set_count = 2;
    options.descriptor_sets = sets;
    options.descriptor_address32_hi = 2;
    PsbcShaderOutput output{};
    psbc_init();
    const auto result = psbc_compile_shader(source.data(), bytes, &options, &output);
    std::printf("PSBC GFX7 %s result=%u %s binaryBytes=%zu\n",
                fragment ? "FS" : "VS", result, psbc_result_string(result), output.size);
    if (result != PSBC_RESULT_OK) return 6;
    GnmShaderMetadata metadata{};
    const auto rc = sceGnmShaderBinaryGetMetadata(output.data, output.size, &metadata);
    assert(rc == 0 && metadata.shadercode && metadata.shadercodesize > 0);
    assert(metadata.numinputsemantics == (fragment ? 1u : 2u));
    bool paramsFound = false, matricesFound = false, fetchFound = false, verticesFound = false;
    for (uint32_t i = 0; i < metadata.numinputusageslots; ++i) {
        const auto& slot = metadata.inputusageslots[i];
        assert(slot.startregister < 16);
        if (slot.usagetype == GNM_SHINPUTUSAGE_PTR_INDIRECTRESOURCETABLE) {
            paramsFound |= slot.apislot == 0;
            matricesFound |= slot.apislot == 1;
        }
        fetchFound |= slot.usagetype == GNM_SHINPUTUSAGE_SUBPTR_FETCHSHADER;
        verticesFound |= slot.usagetype == GNM_SHINPUTUSAGE_PTR_VERTEXBUFFERTABLE;
        std::printf("  usage type=%u api=%u register=%u\n",
                    slot.usagetype, slot.apislot, slot.startregister);
    }
    assert(paramsFound);
    if (!fragment) {
        assert(metadata.type == GNM_SHADER_VERTEX && metadata.numexportsemantics == 2);
        assert(matricesFound && fetchFound && verticesFound);
        const auto* shader = static_cast<const GnmVsShader*>(metadata.stage);
        const auto* inputs = sceGnmVsShaderInputSemanticTable(shader);
        assert(inputs[0].semantic == 0 && inputs[1].semantic == 1);
        const auto* exports = sceGnmVsShaderExportSemanticTable(shader);
        // PSBC encodes Vulkan user location 0 as GNM generic semantic 32.
        assert(exports[0].semantic == 32 && exports[1].semantic == 33);
    } else {
        assert(metadata.type == GNM_SHADER_PIXEL);
        const auto* shader = static_cast<const GnmPsShader*>(metadata.stage);
        const auto* inputs = sceGnmPsShaderInputSemanticTable(shader);
        // WorldPos is unused by the fragment shader, leaving only alpha UVs.
        assert(inputs[0].semantic == 32);
    }
    std::printf("PASS GNM metadata codeBytes=%u inputs=%u exports=%u descriptors/fetch valid\n",
                metadata.shadercodesize, metadata.numinputsemantics, metadata.numexportsemantics);
    psbc_free_output(&output);
    psbc_shutdown();
}
