/*
 * vk_ps4_log.h — Breadcrumb logging API for PS4 hardware debugging.
 *
 * This header is safe to include from both the ICD internals and external
 * test programs.  It only depends on <stdint.h> and <stdbool.h>.
 */
#ifndef VK_PS4_LOG_H
#define VK_PS4_LOG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Open a breadcrumb log file.  NULL path uses default:
 *   PS4:  /data/vk_ps4_breadcrumb.log
 *   host: /tmp/vk_ps4_breadcrumb.log
 * Call early in main() so all subsequent ICD calls are traced. */
void vk_ps4_log_open(const char *path);

/* Close the log file and flush. */
void vk_ps4_log_close(void);

/* Printf-style breadcrumb.  Flushes after every write so logs survive
 * crashes.  No-op if the log is not open. */
void vk_ps4_log(const char *fmt, ...);

/* Single-string breadcrumb (no varargs overhead). */
void vk_ps4_log_raw(const char *msg);

/* Returns true if the log file is currently open. */
bool vk_ps4_log_is_open(void);

#ifdef __cplusplus
}
#endif

#endif /* VK_PS4_LOG_H */
