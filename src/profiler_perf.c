#define _GNU_SOURCE

#include "profiler.h"

#include <asm/unistd.h>
#include <errno.h>
#include <linux/perf_event.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

struct cache_profiler_counter_pair {
    int access_fd;
    int miss_fd;
    int supported;
};

struct cache_profiler_sampler {
    pid_t pid;
    struct cache_profiler_counter_pair l1;
    struct cache_profiler_counter_pair l2;
    struct cache_profiler_counter_pair llc;
};

struct cache_profiler_capture_context {
    struct cache_profiler_stats *p_stats_array;
};

static int cache_profiler_perf_capture(pid_t pid, uint32_t interval_ms, uint32_t sample_count, struct cache_profiler_stats *p_stats_array);
static int cache_profiler_perf_iterate(pid_t pid, uint32_t interval_ms, uint32_t sample_count, cache_profiler_sample_callback p_on_sample, void *p_user_data);
static int cache_profiler_perf_sampler_create(pid_t pid, struct cache_profiler_sampler **pp_sampler);
static int cache_profiler_perf_sampler_read(struct cache_profiler_sampler *p_sampler, struct cache_profiler_stats *p_stats);
static void cache_profiler_perf_sampler_destroy(struct cache_profiler_sampler *p_sampler);

/**
 * @brief Opens one perf event counter for a target PID.
 *
 * @param p_attr Perf event attributes.
 * @param pid Target process ID.
 *
 * @return File descriptor.
 * @retval >=0 Success.
 * @retval Negative errno code Failure.
 */
static int cache_profiler_perf_open_counter(const struct perf_event_attr *p_attr, pid_t pid) {
    int fd;

    fd = (int)syscall(__NR_perf_event_open, p_attr, pid, -1, -1, 0);
    if (fd < 0) {
        return -errno;
    }

    return fd;
}

/**
 * @brief Builds and opens a cache counter pair (access and miss).
 *
 * @param cache_id Linux cache identifier from PERF_COUNT_HW_CACHE_*.
 * @param pid Target process ID.
 * @param p_pair Output counter pair.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval Negative errno code Failure.
 */
static int cache_profiler_perf_open_hw_cache_pair(__u64 cache_id, pid_t pid, struct cache_profiler_counter_pair *p_pair) {
    struct perf_event_attr attr;
    int fd;

    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_HW_CACHE;
    attr.size = sizeof(attr);
    attr.disabled = 0;
    attr.exclude_kernel = 0;
    attr.exclude_hv = 1;
    attr.inherit = 1;

    // Counter 1: total cache read accesses.
    attr.config = cache_id | ((__u64)PERF_COUNT_HW_CACHE_OP_READ << 8U) |
                  ((__u64)PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16U);
    fd = cache_profiler_perf_open_counter(&attr, pid);
    if (fd < 0) {
        return fd;
    }
    p_pair->access_fd = fd;

    // Counter 2: cache read misses for the same cache level.
    attr.config = cache_id | ((__u64)PERF_COUNT_HW_CACHE_OP_READ << 8U) |
                  ((__u64)PERF_COUNT_HW_CACHE_RESULT_MISS << 16U);
    fd = cache_profiler_perf_open_counter(&attr, pid);
    if (fd < 0) {
        (void)close(p_pair->access_fd);
        p_pair->access_fd = -1;
        return fd;
    }
    p_pair->miss_fd = fd;
    p_pair->supported = 1;

    return 0;
}

/**
 * @brief Closes both descriptors in a counter pair.
 *
 * @param p_pair Counter pair to close.
 */
static void cache_profiler_perf_close_pair(struct cache_profiler_counter_pair *p_pair) {
    if (p_pair->access_fd >= 0) {
        (void)close(p_pair->access_fd);
        p_pair->access_fd = -1;
    }
    if (p_pair->miss_fd >= 0) {
        (void)close(p_pair->miss_fd);
        p_pair->miss_fd = -1;
    }
    p_pair->supported = 0;
}

/**
 * @brief Reads one 64-bit counter value from a perf event FD.
 *
 * @param fd Counter file descriptor.
 * @param p_value Output value.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval Negative errno code Failure.
 */
static int cache_profiler_perf_read_counter(int fd, uint64_t *p_value) {
    ssize_t rc;

    rc = read(fd, p_value, sizeof(*p_value));
    if (rc != (ssize_t)sizeof(*p_value)) {
        if (rc < 0) {
            return -errno;
        }
        return -EIO;
    }
    return 0;
}

/**
 * @brief Reads both access/miss counters from a pair into a stats struct.
 *
 * @param p_pair Counter pair.
 * @param p_level Output stats for one cache level.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval Negative errno code Failure.
 */
static int cache_profiler_perf_read_pair(const struct cache_profiler_counter_pair *p_pair, struct cache_profiler_level_stats *p_level) {
    int rc;

    p_level->accesses = 0;
    p_level->misses = 0;
    p_level->supported = p_pair->supported;

    if (!p_pair->supported) {
        return 0;
    }

    rc = cache_profiler_perf_read_counter(p_pair->access_fd, &p_level->accesses);
    if (rc != 0) {
        return rc;
    }
    rc = cache_profiler_perf_read_counter(p_pair->miss_fd, &p_level->misses);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * @brief Checks whether a process is still alive.
 *
 * @param pid Target process ID.
 *
 * @return Liveness state.
 * @retval 1 Process exists or is permission-protected.
 * @retval 0 Process does not exist.
 * @retval Negative errno code Unexpected failure while checking.
 */
static int cache_profiler_perf_is_process_alive(pid_t pid) {
    if (kill(pid, 0) == 0) {
        return 1;
    }
    if (errno == EPERM) {
        return 1;
    }
    if (errno == ESRCH) {
        return 0;
    }
    return -errno;
}

/**
 * @brief Callback adapter for capture API.
 *
 * @param sample_index Zero-based sample index.
 * @param sample_count Total requested sample count.
 * @param p_stats Sample stats payload.
 * @param p_user_data Capture context.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval Negative errno code Failure.
 */
static int cache_profiler_perf_capture_sample_callback(uint32_t sample_index, uint32_t sample_count, const struct cache_profiler_stats *p_stats, void *p_user_data) {
    struct cache_profiler_capture_context *p_context = (struct cache_profiler_capture_context *)p_user_data;

    (void)sample_count;
    p_context->p_stats_array[sample_index] = *p_stats;
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
 * @retval Negative errno code Failure while creating sampler, reading counters,
 *         waiting between samples, or from callback return.
 */
static int cache_profiler_perf_capture(pid_t pid, uint32_t interval_ms, uint32_t sample_count, struct cache_profiler_stats *p_stats_array) {
    struct cache_profiler_capture_context context;

    if (p_stats_array == NULL) {
        return -EINVAL;
    }

    context.p_stats_array = p_stats_array;
    return cache_profiler_perf_iterate(pid, interval_ms, sample_count, cache_profiler_perf_capture_sample_callback, &context);
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
 * @retval Negative errno code Failure while creating sampler, reading counters,
 *         waiting between samples, or from callback return.
 */
static int cache_profiler_perf_iterate(pid_t pid, uint32_t interval_ms, uint32_t sample_count, cache_profiler_sample_callback p_on_sample, void *p_user_data) {
    struct cache_profiler_sampler *p_sampler = NULL;
    struct cache_profiler_stats stats;
    struct timespec delay;
    int rc;

    if (pid <= 0 || interval_ms == 0 || p_on_sample == NULL) {
        return -EINVAL;
    }

    rc = cache_profiler_perf_sampler_create(pid, &p_sampler);
    if (rc != 0) {
        return rc;
    }

    // Reuse one timespec across the loop to avoid per-sample recalculation.
    delay.tv_sec = interval_ms / 1000U;
    delay.tv_nsec = (long)(interval_ms % 1000U) * 1000000L;

    for (uint32_t idx = 0;; ++idx) {
        // In unbounded mode, stop cleanly once target process exits.
        if (sample_count == 0U) {
            int alive = cache_profiler_perf_is_process_alive(pid);

            if (alive < 0) {
                cache_profiler_perf_sampler_destroy(p_sampler);
                return alive;
            }
            if (alive == 0) {
                break;
            }
        }

        // Samples are cumulative snapshots since sampler creation.
        rc = cache_profiler_perf_sampler_read(p_sampler, &stats);
        if (rc != 0) {
            // If target died between liveness check and read, end gracefully.
            if (sample_count == 0U) {
                int alive = cache_profiler_perf_is_process_alive(pid);

                if (alive == 0) {
                    break;
                }
            }
            cache_profiler_perf_sampler_destroy(p_sampler);
            return rc;
        }

        rc = p_on_sample(idx, sample_count, &stats, p_user_data);
        if (rc != 0) {
            cache_profiler_perf_sampler_destroy(p_sampler);
            return rc;
        }

        if (sample_count != 0U && idx + 1U >= sample_count) {
            break;
        }

        if (nanosleep(&delay, NULL) != 0) {
            rc = -errno;
            cache_profiler_perf_sampler_destroy(p_sampler);
            return rc;
        }
    }

    cache_profiler_perf_sampler_destroy(p_sampler);
    return 0;
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
static int cache_profiler_perf_sampler_create(pid_t pid, struct cache_profiler_sampler **pp_sampler) {
    struct cache_profiler_sampler *p_sampler;
    int rc;

    if (pp_sampler == NULL) {
        return -EINVAL;
    }
    *pp_sampler = NULL;

    if (pid <= 0) {
        return -EINVAL;
    }

    p_sampler = calloc(1, sizeof(*p_sampler));
    if (p_sampler == NULL) {
        return -ENOMEM;
    }

    p_sampler->pid = pid;
    p_sampler->l1.access_fd = -1;
    p_sampler->l1.miss_fd = -1;
    p_sampler->l2.access_fd = -1;
    p_sampler->l2.miss_fd = -1;
    p_sampler->llc.access_fd = -1;
    p_sampler->llc.miss_fd = -1;

    // L1D/LLC are available through generic PERF_TYPE_HW_CACHE IDs.
    rc = cache_profiler_perf_open_hw_cache_pair(PERF_COUNT_HW_CACHE_L1D, pid, &p_sampler->l1);
    if (rc != 0) {
        free(p_sampler);
        return rc;
    }

    // Linux generic HW cache API does not expose L2 in PERF_COUNT_HW_CACHE_*.
    p_sampler->l2.supported = 0;

    rc = cache_profiler_perf_open_hw_cache_pair(PERF_COUNT_HW_CACHE_LL, pid, &p_sampler->llc);
    if (rc != 0) {
        cache_profiler_perf_close_pair(&p_sampler->l1);
        free(p_sampler);
        return rc;
    }

    *pp_sampler = p_sampler;
    return 0;
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
static int cache_profiler_perf_sampler_read(struct cache_profiler_sampler *p_sampler, struct cache_profiler_stats *p_stats) {
    int rc;

    if (p_sampler == NULL || p_stats == NULL) {
        return -EINVAL;
    }

    memset(p_stats, 0, sizeof(*p_stats));

    rc = cache_profiler_perf_read_pair(&p_sampler->l1, &p_stats->l1);
    if (rc != 0) {
        return rc;
    }
    rc = cache_profiler_perf_read_pair(&p_sampler->l2, &p_stats->l2);
    if (rc != 0) {
        return rc;
    }
    rc = cache_profiler_perf_read_pair(&p_sampler->llc, &p_stats->llc);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * @brief Releases resources owned by a sampler.
 *
 * @param p_sampler Sampler handle. NULL is allowed.
 */
static void cache_profiler_perf_sampler_destroy(struct cache_profiler_sampler *p_sampler) {
    if (p_sampler == NULL) {
        return;
    }

    cache_profiler_perf_close_pair(&p_sampler->l1);
    cache_profiler_perf_close_pair(&p_sampler->l2);
    cache_profiler_perf_close_pair(&p_sampler->llc);
    free(p_sampler);
}

static const struct cache_profiler_interface g_cache_profiler_perf_interface = {
    .p_name = "linux-perf",
    .p_capture = cache_profiler_perf_capture,
    .p_iterate = cache_profiler_perf_iterate,
    .p_sampler_create = cache_profiler_perf_sampler_create,
    .p_sampler_read = cache_profiler_perf_sampler_read,
    .p_sampler_destroy = cache_profiler_perf_sampler_destroy,
};

/**
 * @brief Returns the built-in Linux perf profiler implementation.
 *
 * @return Perf profiler interface descriptor.
 */
const struct cache_profiler_interface *cache_profiler_get_perf_interface(void) {
    return &g_cache_profiler_perf_interface;
}
