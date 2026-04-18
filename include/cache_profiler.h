#ifndef CACHE_PROFILER_H
#define CACHE_PROFILER_H

#include <stdint.h>
#include <sys/types.h>

// Preserve C ABI when included by C++ translation units.
#ifdef __cplusplus
extern "C" {
#endif

struct cache_profiler_sampler;

/**
 * @brief Aggregated cache counters for one cache level.
 */
struct cache_profiler_level_stats {
    uint64_t accesses;
    uint64_t misses;
    int supported;
};

/**
 * @brief Cache counters for L1, L2, and LLC.
 */
struct cache_profiler_stats {
    struct cache_profiler_level_stats l1;
    struct cache_profiler_level_stats l2;
    struct cache_profiler_level_stats llc;
};

typedef int (*cache_profiler_sample_callback)(uint32_t sample_index, uint32_t sample_count, const struct cache_profiler_stats *p_stats, void *p_user_data);

/**
 * @brief Captures multiple cache samples for a PID at a fixed interval.
 *
 * Sampling is cumulative from profiler start. The output array receives
 * `sample_count` snapshots in order.
 *
 * @param pid Target process ID.
 * @param interval_ms Delay between samples in milliseconds. Must be > 0.
 * @param sample_count Number of samples to capture. 0 means run until target
 *        process exits.
 * @param p_stats_array Caller-allocated output array of `sample_count` entries.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval Negative errno code Failure while creating sampler, reading counters,
 *         or waiting between samples.
 */
int cache_profiler_capture(pid_t pid, uint32_t interval_ms, uint32_t sample_count, struct cache_profiler_stats *p_stats_array);

/**
 * @brief Iterates cache samples and dispatches each sample to a callback.
 *
 * This is the profiler/logging separation point. The profiler gathers stats,
 * while callers decide how to consume them (stdout, CSV, network, etc.).
 *
 * @param pid Target process ID.
 * @param interval_ms Delay between samples in milliseconds. Must be > 0.
 * @param sample_count Number of samples to capture. 0 means run until target
 *        process exits.
 * @param p_on_sample Callback invoked for each gathered sample.
 * @param p_user_data Opaque callback context pointer.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval Negative errno code Failure while creating sampler, reading counters,
 *         waiting between samples, or from callback return.
 */
int cache_profiler_iterate(pid_t pid, uint32_t interval_ms, uint32_t sample_count, cache_profiler_sample_callback p_on_sample, void *p_user_data);

/**
 * @brief Prints a textual cache profiling report.
 *
 * @param pid Target process ID that was profiled.
 * @param interval_ms Sample interval in milliseconds.
 * @param sample_count Number of samples in p_stats_array.
 * @param p_stats_array Sample array produced by capture.
 */
void cache_profiler_report(pid_t pid, uint32_t interval_ms, uint32_t sample_count, const struct cache_profiler_stats *p_stats_array);

/**
 * @brief Captures and prints cache samples as they are gathered.
 *
 * This is intended for interactive CLI usage where each sample should be
 * visible immediately instead of after the full run completes.
 *
 * @param pid Target process ID.
 * @param interval_ms Delay between samples in milliseconds. Must be > 0.
 * @param sample_count Number of samples to capture. Must be > 0.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval Negative errno code Failure while creating sampler, reading counters,
 *         or waiting between samples.
 */
int cache_profiler_stream(pid_t pid, uint32_t interval_ms, uint32_t sample_count);

/**
 * @brief Creates a cache sampler scoped to a target process ID.
 *
 * The sampler opens kernel perf counters that count cache accesses and misses
 * attributable to the target PID.
 *
 * @param pid Target process ID.
 * @param pp_sampler Output pointer to the created sampler object.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval Negative errno code Failure while creating counters.
 */
int cache_profiler_sampler_create(pid_t pid, struct cache_profiler_sampler **pp_sampler);

/**
 * @brief Reads current cache counters from a sampler.
 *
 * Counts are cumulative from sampler creation time and are not reset by read.
 *
 * @param p_sampler Sampler handle returned by create.
 * @param p_stats Output cache statistics.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval Negative errno code Failure while reading counters.
 */
int cache_profiler_sampler_read(struct cache_profiler_sampler *p_sampler, struct cache_profiler_stats *p_stats);

/**
 * @brief Releases resources owned by a sampler.
 *
 * @param p_sampler Sampler handle. NULL is allowed.
 */
void cache_profiler_sampler_destroy(struct cache_profiler_sampler *p_sampler);

#ifdef __cplusplus
// End C ABI block.
}
#endif

#endif
