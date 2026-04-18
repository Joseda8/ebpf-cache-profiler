#ifndef LOGGER_H
#define LOGGER_H

#include "printmsg.h"

#ifdef __cplusplus
extern "C" {
#endif

void printmsg_logger_profile_report(pid_t pid, uint32_t interval_ms, uint32_t sample_count, const struct printmsg_cache_stats *p_stats_array);
int printmsg_logger_profile_stream(pid_t pid, uint32_t interval_ms, uint32_t sample_count);

#ifdef __cplusplus
}
#endif

#endif
