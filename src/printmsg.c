#define _GNU_SOURCE

#include "printmsg.h"
#include "logger.h"
#include "profiler.h"

/**
 * @brief Captures multiple cache samples for a PID at a fixed interval.
 *
 * @param pid Target process ID.
 * @param interval_ms Delay between samples in milliseconds. Must be > 0.
 * @param sample_count Number of samples to capture. Must be > 0.
 * @param p_stats_array Caller-allocated output array of sample_count entries.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval Negative errno code Failure while creating sampler, reading counters,
 *         waiting between samples, or from callback return.
 */
int printmsg_cache_profile_capture(pid_t pid, uint32_t interval_ms, uint32_t sample_count, struct printmsg_cache_stats *p_stats_array) {
    return printmsg_profiler_capture(pid, interval_ms, sample_count, p_stats_array);
}

/**
 * @brief Iterates cache samples and dispatches each sample to a callback.
 *
 * @param pid Target process ID.
 * @param interval_ms Delay between samples in milliseconds. Must be > 0.
 * @param sample_count Number of samples to capture. Must be > 0.
 * @param p_on_sample Callback invoked for each gathered sample.
 * @param p_user_data Opaque callback context pointer.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval Negative errno code Failure while creating sampler, reading counters,
 *         waiting between samples, or from callback return.
 */
int printmsg_cache_profile_iterate(pid_t pid, uint32_t interval_ms, uint32_t sample_count, printmsg_cache_sample_callback p_on_sample, void *p_user_data) {
    return printmsg_profiler_iterate(pid, interval_ms, sample_count, p_on_sample, p_user_data);
}

/**
 * @brief Prints a textual cache profiling report.
 *
 * @param pid Target process ID that was profiled.
 * @param interval_ms Sample interval in milliseconds.
 * @param sample_count Number of samples in p_stats_array.
 * @param p_stats_array Sample array produced by capture.
 */
void printmsg_cache_profile_report(pid_t pid, uint32_t interval_ms, uint32_t sample_count, const struct printmsg_cache_stats *p_stats_array) {
    printmsg_logger_profile_report(pid, interval_ms, sample_count, p_stats_array);
}

/**
 * @brief Captures and prints cache samples as they are gathered.
 *
 * @param pid Target process ID.
 * @param interval_ms Delay between samples in milliseconds. Must be > 0.
 * @param sample_count Number of samples to capture. Must be > 0.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval Negative errno code Failure while creating sampler, reading counters,
 *         waiting between samples, or from callback return.
 */
int printmsg_cache_profile_stream(pid_t pid, uint32_t interval_ms, uint32_t sample_count) {
    return printmsg_logger_profile_stream(pid, interval_ms, sample_count);
}

/**
 * @brief Creates a cache sampler scoped to a target process ID.
 *
 * @param pid Target process ID.
 * @param pp_sampler Output pointer to the created sampler object.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval Negative errno code Failure while creating counters.
 */
int printmsg_cache_sampler_create(pid_t pid, struct printmsg_cache_sampler **pp_sampler) {
    return printmsg_profiler_sampler_create(pid, pp_sampler);
}

/**
 * @brief Reads current cache counters from a sampler.
 *
 * @param p_sampler Sampler handle returned by create.
 * @param p_stats Output cache statistics.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval Negative errno code Failure while reading counters.
 */
int printmsg_cache_sampler_read(struct printmsg_cache_sampler *p_sampler, struct printmsg_cache_stats *p_stats) {
    return printmsg_profiler_sampler_read(p_sampler, p_stats);
}

/**
 * @brief Releases resources owned by a sampler.
 *
 * @param p_sampler Sampler handle. NULL is allowed.
 */
void printmsg_cache_sampler_destroy(struct printmsg_cache_sampler *p_sampler) {
    printmsg_profiler_sampler_destroy(p_sampler);
}
