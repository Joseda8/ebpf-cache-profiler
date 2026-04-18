#ifndef LOGGER_H
#define LOGGER_H

#include "cache_profiler.h"

#ifdef __cplusplus
extern "C" {
#endif

void cache_profiler_logger_profile_report(pid_t pid, uint32_t interval_ms, uint32_t sample_count, const struct cache_profiler_stats *p_stats_array);
int cache_profiler_logger_profile_stream(pid_t pid, uint32_t interval_ms, uint32_t sample_count);

#ifdef __cplusplus
}
#endif

#endif
