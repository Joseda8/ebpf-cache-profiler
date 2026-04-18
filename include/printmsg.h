#ifndef PRINTMSG_H
#define PRINTMSG_H

#include <stdint.h>
#include <sys/types.h>

// Preserve C ABI when included by C++ translation units.
#ifdef __cplusplus
extern "C" {
#endif

struct printmsg_cache_sampler;

/**
 * @brief Aggregated cache counters for one cache level.
 */
struct printmsg_cache_level_stats {
    uint64_t accesses;
    uint64_t misses;
    int supported;
};

/**
 * @brief Cache counters for L1, L2, and LLC.
 */
struct printmsg_cache_stats {
    struct printmsg_cache_level_stats l1;
    struct printmsg_cache_level_stats l2;
    struct printmsg_cache_level_stats llc;
};

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
int printmsg_cache_sampler_create(pid_t pid, struct printmsg_cache_sampler **pp_sampler);

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
int printmsg_cache_sampler_read(struct printmsg_cache_sampler *p_sampler,
                                struct printmsg_cache_stats *p_stats);

/**
 * @brief Releases resources owned by a sampler.
 *
 * @param p_sampler Sampler handle. NULL is allowed.
 */
void printmsg_cache_sampler_destroy(struct printmsg_cache_sampler *p_sampler);

#ifdef __cplusplus
// End C ABI block.
}
#endif

#endif
