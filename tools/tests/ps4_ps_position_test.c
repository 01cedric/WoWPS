/* Pinned OpenGNM PS register emission: synthetic metadata matches compiled
 * the implementation marcher, checked independently by the PSBC ABI test. Not a GPU test. */
#include <gnm_drawcommandbuffer.h>
#include <pm4/amdgfxregs.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
extern unsigned gnm_diagnostic_count;
static uint32_t context_value(const uint32_t *p, const uint32_t *end, uint32_t reg) {
    while (p < end) {
        assert((p[0] >> 30) == 3);
        unsigned words = ((p[0] >> 16) & 0x3fff) + 2;
        assert(p + words <= end);
        if (((p[0] >> 8) & 0xff) == 0x69) {
            uint32_t start = 0x28000 + (p[1] & 0xffff) * 4;
            if (reg >= start && reg < start + (words - 2) * 4)
                return p[2 + (reg-start)/4];
        }
        p += words;
    }
    assert(!"register absent from SetPsShader packets"); return 0;
}
int main(void) {
    _Alignas(256) uint32_t pm4[256];
    GnmCommandBuffer cmd = {0};
    cmd.beginptr = cmd.cmdptr = pm4; cmd.endptr = pm4 + 256;
    GnmPsStageRegisters ps = {0};
    sceGnmPsRegsSetAddress(&ps, (void*)(uintptr_t)0x240000000ULL);
    ps.spipsinputena = ps.spipsinputaddr = 0x380;
    ps.spishadercolformat = 4; ps.cbshadermask = 15;
    sceGnmDrawCmdSetPsShader(&cmd, &ps);
    assert(!gnm_diagnostic_count);
    assert(context_value(pm4, cmd.cmdptr, R_0286CC_SPI_PS_INPUT_ENA) == 0x380);
    assert(context_value(pm4, cmd.cmdptr, R_0286D0_SPI_PS_INPUT_ADDR) == 0x380);
    assert(context_value(pm4, cmd.cmdptr, R_028714_SPI_SHADER_COL_FORMAT) == 4);
    assert(context_value(pm4, cmd.cmdptr, R_02823C_CB_SHADER_MASK) == 15);
    puts("PASS pinned SetPsShader PM4 preserves ENA=ADDR=0x380 (position X/Y), FP16_ABGR=4, RGBA mask=15 with zero generic inputs");
}
