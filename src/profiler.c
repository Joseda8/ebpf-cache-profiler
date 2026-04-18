#define _GNU_SOURCE

#include "profiler.h"

#include <errno.h>
#include <stddef.h>

static const struct cache_profiler_interface *g_p_active_interface = NULL;

/**
 * @brief Returns the currently active profiler implementation.
 *
 * Defaults to the built-in perf implementation when no override was set.
 *
 * @return Active profiler interface.
 */
const struct cache_profiler_interface *cache_profiler_get_active_interface(void) {
    if (g_p_active_interface == NULL) {
        g_p_active_interface = cache_profiler_get_perf_interface();
    }

    return g_p_active_interface;
}

/**
 * @brief Sets the active profiler implementation.
 *
 * @param p_interface Interface to activate.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval -EINVAL Invalid interface.
 */
int cache_profiler_set_active_interface(const struct cache_profiler_interface *p_interface) {
    if (p_interface == NULL || p_interface->p_capture == NULL || p_interface->p_iterate == NULL ||
        p_interface->p_sampler_create == NULL || p_interface->p_sampler_read == NULL ||
        p_interface->p_sampler_destroy == NULL) {
        return -EINVAL;
    }

    g_p_active_interface = p_interface;
    return 0;
}

/**
 * @brief Captures multiple cache samples for a PID at a fixed interval.
 *
 * @param pid Target process ID.
 * @param interval_ms Delay between samples in milliseconds. Must be > 0.
 * @param sample_count Number of samples to capture. 0 means run until target
 *        process exits.
 * @param p_stats_array Caller-allocated output array of sample_count entries.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval Negative errno code Failure.
 */
int cache_profiler_core_capture(pid_t pid, uint32_t interval_ms, uint32_t sample_count, struct cache_profiler_stats *p_stats_array) {
    return cache_profiler_get_active_interface()->p_capture(pid, interval_ms, sample_count, p_stats_array);
}

/**
 * @brief Iterates cache samples and dispatches each sample to a callback.
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
 * @retval Negative errno code Failure.
 */
int cache_profiler_core_iterate(pid_t pid, uint32_t interval_ms, uint32_t sample_count, cache_profiler_sample_callback p_on_sample, void *p_user_data) {
    return cache_profiler_get_active_interface()->p_iterate(pid, interval_ms, sample_count, p_on_sample, p_user_data);
}

/**
 * @brief Creates a cache sampler scoped to a target process ID.
 *
 * @param pid Target process ID.
 * @param pp_sampler Output pointer to the created sampler object.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval Negative errno code Failure.
 */
int cache_profiler_core_sampler_create(pid_t pid, struct cache_profiler_sampler **pp_sampler) {
    return cache_profiler_get_active_interface()->p_sampler_create(pid, pp_sampler);
}

/**
 * @brief Reads current cache counters from a sampler.
 *
 * @param p_sampler Sampler handle returned by create.
 * @param p_stats Output cache statistics.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval Negative errno code Failure.
 */
int cache_profiler_core_sampler_read(struct cache_profiler_sampler *p_sampler, struct cache_profiler_stats *p_stats) {
    return cache_profiler_get_active_interface()->p_sampler_read(p_sampler, p_stats);
}

/**
 * @brief Releases resources owned by a sampler.
 *
 * @param p_sampler Sampler handle. NULL is allowed.
 */
void cache_profiler_core_sampler_destroy(struct cache_profiler_sampler *p_sampler) {
    cache_profiler_get_active_interface()->p_sampler_destroy(p_sampler);
}
