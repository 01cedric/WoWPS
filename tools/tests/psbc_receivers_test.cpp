// Compile all four production receivers with their actual descriptor layouts.
#include "psbc_compile.h"
extern "C" {
#include "gnm_helpers.h"
}
#include <cassert>
#include <cstdio>
#include <vector>
#include <string>

int main(int argc, char** argv) {
    std::setbuf(stdout, nullptr);
    if (argc != 3) return 2;
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
    const bool vertex = std::string(argv[2]) == "celestial";
    PsbcDescriptorBinding frame[] = {{0,6,1,0,16,UINT32_MAX},{1,1,1,16,48,UINT32_MAX}};
    PsbcDescriptorBinding material[] = {{0,1,1,0,48,UINT32_MAX},{1,6,1,48,16,UINT32_MAX},{2,1,1,64,48,UINT32_MAX}};
    PsbcDescriptorBinding m2[] = {{0,1,1,0,48,UINT32_MAX},{2,6,1,48,16,UINT32_MAX}};
    PsbcDescriptorBinding terrain[8]{};
    for (uint32_t i=0;i<7;++i) terrain[i]={i,1,1,i*48,48,UINT32_MAX};
    terrain[7]={7,6,1,336,16,UINT32_MAX};
    PsbcDescriptorSetLayout sets[] = {{2,64,0,frame},{3,112,0,material}};
    if (std::string(argv[2]) == "m2") sets[1]={2,64,0,m2};
    if (std::string(argv[2]) == "terrain") sets[1]={8,352,0,terrain};
    PsbcCompileOptions options{};
    options.target = PSBC_TARGET_PS4_BASE;
    options.stage = vertex ? PSBC_STAGE_VERTEX : PSBC_STAGE_FRAGMENT;
    options.entrypoint = "main";
    options.optimise = true;
    options.descriptor_set_count = vertex ? 1 : 2;
    options.descriptor_sets = sets;
    options.descriptor_address32_hi = 2;
    PsbcShaderOutput output{};
    psbc_init();
    auto result = psbc_compile_shader(source.data(), bytes, &options, &output);
    std::printf("PSBC GFX7 lighting %s: result=%u %s binaryBytes=%zu\n",
                argv[1], result, psbc_result_string(result), output.size);
    if (result != PSBC_RESULT_OK) return 6;
    GnmShaderMetadata metadata{};
    auto rc = sceGnmShaderBinaryGetMetadata(output.data, output.size, &metadata);
    assert(rc == 0 && metadata.type == (vertex ? GNM_SHADER_VERTEX : GNM_SHADER_PIXEL));
    assert(metadata.shadercode && metadata.shadercodesize > 0);
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
