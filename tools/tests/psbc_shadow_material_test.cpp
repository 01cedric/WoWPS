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
    const bool wmo = kind.rfind("wmo", 0) == 0;
    const bool shared = kind == "shared_frag" || kind == "character_frag";
    const bool fragment = kind.find("frag") != std::string::npos;
    const bool instanced = kind.find("instanced") != std::string::npos;
    assert(shared || kind == "wmo_vert" || kind == "wmo_frag" || kind == "m2_vert" ||
           kind == "m2_instanced_vert" || kind == "m2_frag");
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
    PsbcDescriptorBinding params[] = {{0,1,1,0,48,UINT32_MAX}, {1,6,1,48,16,UINT32_MAX}};
    PsbcDescriptorBinding m2Material[] = {{0,1,1,0,48,UINT32_MAX}, {2,6,1,48,16,UINT32_MAX}};
    PsbcDescriptorBinding matrices[] = {{0,7,1,0,16,UINT32_MAX}};
    PsbcDescriptorBinding wmoMaterial[] = {{0,1,1,0,48,UINT32_MAX}, {1,6,1,48,16,UINT32_MAX}, {2,1,1,64,48,UINT32_MAX}};
    PsbcDescriptorSetLayout sets[] = {{2,64,0,params}, {2,64,0,m2Material}, {1,16,0,matrices}};
    if (wmo) sets[0] = {3,112,0,wmoMaterial};
    PsbcCompileOptions options{};
    options.target = PSBC_TARGET_PS4_BASE;
    options.stage = fragment ? PSBC_STAGE_FRAGMENT : PSBC_STAGE_VERTEX;
    options.entrypoint = "main";
    options.optimise = true;
    options.descriptor_set_count = (wmo || shared) ? 1 : instanced ? 3 : 2;
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
    bool pushPointer = false, maskDword = false, pushAny = false;
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
            maskDword |= slot.apislot == 31; // fragment mask at byte 124
        }
        fetch |= slot.usagetype == GNM_SHINPUTUSAGE_SUBPTR_FETCHSHADER;
        vertices |= slot.usagetype == GNM_SHINPUTUSAGE_PTR_VERTEXBUFFERTABLE;
        std::printf(" usage type=%u api=%u reg=%u\n", slot.usagetype, slot.apislot, slot.startregister);
    }
    const uint32_t expectedSets = fragment ? ((wmo || shared) ? 1 : 2) : (wmo ? 0 : instanced ? 5 : 1);
    assert(setMask == expectedSets); // unused layout sets must not become shader requirements
    if (fragment) {
        const auto* shader = static_cast<const GnmPsShader*>(metadata.stage);
        assert(metadata.numinputsemantics == 1 && sceGnmPsShaderInputSemanticTable(shader)[0].semantic == 32);
        assert(G_02880C_KILL_ENABLE(shader->registers.dbshadercontrol) == 1);
        assert((wmo || shared) ? (!pushPointer && !pushAny) : (pushPointer || maskDword));
        assert(G_02880C_Z_EXPORT_ENABLE(shader->registers.dbshadercontrol) == 1);
        assert(shader->registers.spishaderzformat == 1);
        assert((shader->registers.spipsinputena & 0x400u) != 0); // POS_Z_FLOAT_ENA
        assert((shader->registers.spipsinputaddr & 0x400u) != 0);
        const auto* words = static_cast<const uint32_t*>(metadata.shadercode);
        bool depthExport = false;
        for (uint32_t i = 0; i + 1 < metadata.shadercodesize / 4; ++i) {
            // GFX7 EXP: target MRTZ=8, enabled X, DONE + VM. An executed
            // discard removes the lane from EXEC before this export.
            if (words[i] == 0xf8001881u) depthExport = true;
        }
        assert(depthExport);
        std::printf(" fragment discard/depth export enabled; dbshadercontrol=0x%x zformat=%u posZ=1 MRTZ=1\n",
                    shader->registers.dbshadercontrol, shader->registers.spishaderzformat);
    } else {
        assert(fetch && vertices && (pushPointer || pushAny));
        assert(metadata.numinputsemantics == (wmo ? 2u : 3u));
        const auto* shader = static_cast<const GnmVsShader*>(metadata.stage);
        assert(metadata.numexportsemantics == 1 && sceGnmVsShaderExportSemanticTable(shader)[0].semantic == 32);
    }
    std::printf("PASS %s codeBytes=%u descriptorMask=0x%x, register/UV/push/discard ABI valid\n",
                kind.c_str(), metadata.shadercodesize, setMask);
    psbc_free_output(&output);
    psbc_shutdown();
}
