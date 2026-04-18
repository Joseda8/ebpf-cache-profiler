#ifndef PROFILER_H
#define PROFILER_H

#include "cache_profiler.h"

#ifdef __cplusplus
extern "C" {
#endif

int cache_profiler_core_capture(pid_t pid, uint32_t interval_ms, uint32_t sample_count, struct cache_profiler_stats *p_stats_array);
int cache_profiler_core_iterate(pid_t pid, uint32_t interval_ms, uint32_t sample_count, cache_profiler_sample_callback p_on_sample, void *p_user_data);
int cache_profiler_core_sampler_create(pid_t pid, struct cache_profiler_sampler **pp_sampler);
int cache_profiler_core_sampler_read(struct cache_profiler_sampler *p_sampler, struct cache_profiler_stats *p_stats);
void cache_profiler_core_sampler_destroy(struct cache_profiler_sampler *p_sampler);

#ifdef __cplusplus
}
#endif

#endif
