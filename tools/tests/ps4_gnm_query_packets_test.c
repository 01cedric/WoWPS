/* Execute only the bundled CPU packet emitters. No GPU, driver submission or
 * hardware timing is involved. Unexpected external calls abort in the runner. */
#include "gnm_drawcommandbuffer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void dump(const char *name, const uint32_t *begin, const uint32_t *end) {
    printf("%s:", name);
    while (begin != end) printf(" %08x", *begin++);
    putchar('\n');
}
int main(void) {
    uint32_t words[64] = {0};
    GnmCommandBuffer cmd = {0};
    cmd.beginptr = cmd.cmdptr = words;
    cmd.endptr = words + 64;
    /* An address encoded in packets, never dereferenced by this test. */
    const uint64_t address = UINT64_C(0x1234567800);
    sceGnmDrawCmdEventWriteEop(&cmd, GNM_BOTTOM_OF_PIPE_TS, address,
                              GNM_DATA_SEL_SEND_GPU_CLOCK, 0);
    assert(cmd.cmdptr - words == 6);
    assert(words[0] == 0xc0044700 && words[1] == 0x528);
    assert(words[2] == (uint32_t)address);
    assert(words[3] == (0x83000000u | (uint32_t)(address >> 32)));
    assert(words[4] == 0 && words[5] == 0);
    dump("SEND_GPU_CLOCK raw EOP encoding (units and width unverified)", words, cmd.cmdptr);
    cmd.cmdptr = words;
    sceGnmDrawCmdResetQuery(&cmd, address);
    assert(cmd.cmdptr - words == 6);
    assert(words[0] == 0xc0044700 && words[1] == 0x115);
    assert((words[3] >> 29) == GNM_DATA_SEL_SEND_DATA32);
    assert(words[4] == 0 && words[5] == 0);
    uint32_t reset[6];
    memcpy(reset, words, sizeof(reset));
    dump("ResetQuery immediate zero encoding", words, cmd.cmdptr);
    cmd.cmdptr = words;
    sceGnmDrawCmdBeginQuery(&cmd, address);
    assert(cmd.cmdptr - words == 3);
    dump("BeginQuery context-register encoding", words, cmd.cmdptr);
    cmd.cmdptr = words;
    sceGnmDrawCmdEndQuery(&cmd, address);
    assert(cmd.cmdptr - words == 9);
    assert(memcmp(reset, words, sizeof(reset)) == 0);
    dump("EndQuery same immediate zero plus context-register encoding", words, cmd.cmdptr);
    puts("PASS: bundled emitters inspected; no measured counter width, tick period or GPU completion claim");
}
