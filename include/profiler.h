#ifndef PROFILER_H
#define PROFILER_H

#include "printmsg.h"

#ifdef __cplusplus
extern "C" {
#endif

int printmsg_profiler_capture(pid_t pid, uint32_t interval_ms, uint32_t sample_count, struct printmsg_cache_stats *p_stats_array);
int printmsg_profiler_iterate(pid_t pid, uint32_t interval_ms, uint32_t sample_count, printmsg_cache_sample_callback p_on_sample, void *p_user_data);
int printmsg_profiler_sampler_create(pid_t pid, struct printmsg_cache_sampler **pp_sampler);
int printmsg_profiler_sampler_read(struct printmsg_cache_sampler *p_sampler, struct printmsg_cache_stats *p_stats);
void printmsg_profiler_sampler_destroy(struct printmsg_cache_sampler *p_sampler);

#ifdef __cplusplus
}
#endif

#endif
