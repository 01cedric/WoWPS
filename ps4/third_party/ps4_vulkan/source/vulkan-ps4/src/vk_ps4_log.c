/*
 * vk_ps4_log.c — Breadcrumb logging for PS4 hardware debugging.
 *
 * Writes timestamped breadcrumbs to a log file so crashes can be traced
 * after the fact.  The log file path is configurable via vk_ps4_log_open();
 * on PS4 the default is "/data/vk_ps4_breadcrumb.log".
 *
 * The log is flushed after every write so breadcrumbs survive crashes.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#ifdef __ORBIS__
#include <orbis/libkernel.h>
#endif

#include "vk_ps4_internal.h"

static FILE *g_log_file = NULL;
static uint64_t g_log_counter = 0;
static uint64_t g_start_ticks = 0;

/* Get monotonic time in microseconds */
static uint64_t get_time_us(void) {
#ifdef __ORBIS__
    return (uint64_t)sceKernelGetProcessTime();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
#endif
}

void vk_ps4_log_open(const char *path) {
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    if (!path) {
#ifdef __ORBIS__
        path = "/data/wow_ps/wowps/logs/vulkan_icd.log";
        remove("/data/wow_ps/wowps/logs/vulkan_icd_previous.log");
        rename(path, "/data/wow_ps/wowps/logs/vulkan_icd_previous.log");
#else
        path = "/tmp/vk_ps4_breadcrumb.log";
#endif
    }
    g_log_file = fopen(path, "w");
#ifdef __ORBIS__
    if (!g_log_file) g_log_file = fopen("/data/vk_ps4_breadcrumb.log", "w");
#endif
    g_start_ticks = get_time_us();
    g_log_counter = 0;
    if (g_log_file) {
        fprintf(g_log_file, "=== vulkan-ps4 breadcrumb log ===\n");
        fprintf(g_log_file, "opened: %s\n", path);
        fflush(g_log_file);
    }
}

void vk_ps4_log_close(void) {
    if (g_log_file) {
        fprintf(g_log_file, "[%06llu] === log closed (counter=%llu) ===\n",
                (unsigned long long)(get_time_us() - g_start_ticks),
                (unsigned long long)g_log_counter);
        fflush(g_log_file);
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

void vk_ps4_log(const char *fmt, ...) {
    if (!g_log_file) return;
    uint64_t elapsed = get_time_us() - g_start_ticks;
    g_log_counter++;
    fprintf(g_log_file, "[%06llu] #%llu ",
            (unsigned long long)elapsed,
            (unsigned long long)g_log_counter);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log_file, fmt, ap);
    va_end(ap);
    fprintf(g_log_file, "\n");
    fflush(g_log_file);
}

void vk_ps4_log_raw(const char *msg) {
    if (!g_log_file) return;
    uint64_t elapsed = get_time_us() - g_start_ticks;
    g_log_counter++;
    fprintf(g_log_file, "[%06llu] #%llu %s\n",
            (unsigned long long)elapsed,
            (unsigned long long)g_log_counter, msg);
    fflush(g_log_file);
}

bool vk_ps4_log_is_open(void) {
    return g_log_file != NULL;
}
