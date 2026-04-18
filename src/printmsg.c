#define _GNU_SOURCE

#include "printmsg.h"

#include <asm/unistd.h>
#include <errno.h>
#include <linux/perf_event.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

struct printmsg_counter_pair {
    int access_fd;
    int miss_fd;
    int supported;
};

struct printmsg_cache_sampler {
    pid_t pid;
    struct printmsg_counter_pair l1;
    struct printmsg_counter_pair l2;
    struct printmsg_counter_pair llc;
};

/**
 * @brief Prints one cache level from one sample.
 *
 * @param p_name Cache level label.
 * @param p_level Cache level counters.
 */
static void printmsg_print_level(const char *p_name, const struct printmsg_cache_level_stats *p_level) {
    if (!p_level->supported) {
        printf("%s: unsupported on this system\n", p_name);
        return;
    }

    printf("%s: accesses=%" PRIu64 " misses=%" PRIu64 "\n", p_name, p_level->accesses, p_level->misses);
}

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
static int printmsg_open_counter(const struct perf_event_attr *p_attr, pid_t pid) {
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
static int printmsg_open_hw_cache_pair(__u64 cache_id, pid_t pid, struct printmsg_counter_pair *p_pair) {
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
    fd = printmsg_open_counter(&attr, pid);
    if (fd < 0) {
        return fd;
    }
    p_pair->access_fd = fd;

    // Counter 2: cache read misses for the same cache level.
    attr.config = cache_id | ((__u64)PERF_COUNT_HW_CACHE_OP_READ << 8U) |
                  ((__u64)PERF_COUNT_HW_CACHE_RESULT_MISS << 16U);
    fd = printmsg_open_counter(&attr, pid);
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
static void printmsg_close_pair(struct printmsg_counter_pair *p_pair) {
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
static int printmsg_read_counter(int fd, uint64_t *p_value) {
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
static int printmsg_read_pair(const struct printmsg_counter_pair *p_pair, struct printmsg_cache_level_stats *p_level) {
    int rc;

    p_level->accesses = 0;
    p_level->misses = 0;
    p_level->supported = p_pair->supported;

    if (!p_pair->supported) {
        return 0;
    }

    rc = printmsg_read_counter(p_pair->access_fd, &p_level->accesses);
    if (rc != 0) {
        return rc;
    }
    rc = printmsg_read_counter(p_pair->miss_fd, &p_level->misses);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

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
 *         or waiting between samples.
 */
int printmsg_cache_profile_capture(pid_t pid, uint32_t interval_ms, uint32_t sample_count, struct printmsg_cache_stats *p_stats_array) {
    struct printmsg_cache_sampler *p_sampler = NULL;
    struct timespec delay;
    int rc;

    if (pid <= 0 || interval_ms == 0 || sample_count == 0 || p_stats_array == NULL) {
        return -EINVAL;
    }

    rc = printmsg_cache_sampler_create(pid, &p_sampler);
    if (rc != 0) {
        return rc;
    }

    // Reuse one timespec across the loop to avoid per-sample recalculation.
    delay.tv_sec = interval_ms / 1000U;
    delay.tv_nsec = (long)(interval_ms % 1000U) * 1000000L;

    for (uint32_t i = 0; i < sample_count; ++i) {
        // Samples are cumulative snapshots since sampler creation.
        rc = printmsg_cache_sampler_read(p_sampler, &p_stats_array[i]);
        if (rc != 0) {
            printmsg_cache_sampler_destroy(p_sampler);
            return rc;
        }

        if (i + 1U < sample_count) {
            if (nanosleep(&delay, NULL) != 0) {
                rc = -errno;
                printmsg_cache_sampler_destroy(p_sampler);
                return rc;
            }
        }
    }

    printmsg_cache_sampler_destroy(p_sampler);
    return 0;
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
    if (sample_count == 0 || p_stats_array == NULL) {
        return;
    }

    printf("Cache profile for PID %d every %" PRIu32 " ms (%" PRIu32 " samples)\n", pid, interval_ms, sample_count);

    for (uint32_t i = 0; i < sample_count; ++i) {
        printf("Sample %" PRIu32 ":\n", i + 1U);
        printmsg_print_level("  L1", &p_stats_array[i].l1);
        printmsg_print_level("  L2", &p_stats_array[i].l2);
        printmsg_print_level("  LLC", &p_stats_array[i].llc);
    }
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
    struct printmsg_cache_sampler *p_sampler;
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
    rc = printmsg_open_hw_cache_pair(PERF_COUNT_HW_CACHE_L1D, pid, &p_sampler->l1);
    if (rc != 0) {
        free(p_sampler);
        return rc;
    }

    // Linux generic HW cache API does not expose L2 in PERF_COUNT_HW_CACHE_*.
    p_sampler->l2.supported = 0;

    rc = printmsg_open_hw_cache_pair(PERF_COUNT_HW_CACHE_LL, pid, &p_sampler->llc);
    if (rc != 0) {
        printmsg_close_pair(&p_sampler->l1);
        free(p_sampler);
        return rc;
    }

    *pp_sampler = p_sampler;
    return 0;
}

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
                                struct printmsg_cache_stats *p_stats) {
    int rc;

    if (p_sampler == NULL || p_stats == NULL) {
        return -EINVAL;
    }

    memset(p_stats, 0, sizeof(*p_stats));

    rc = printmsg_read_pair(&p_sampler->l1, &p_stats->l1);
    if (rc != 0) {
        return rc;
    }
    rc = printmsg_read_pair(&p_sampler->l2, &p_stats->l2);
    if (rc != 0) {
        return rc;
    }
    rc = printmsg_read_pair(&p_sampler->llc, &p_stats->llc);
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
void printmsg_cache_sampler_destroy(struct printmsg_cache_sampler *p_sampler) {
    if (p_sampler == NULL) {
        return;
    }

    printmsg_close_pair(&p_sampler->l1);
    printmsg_close_pair(&p_sampler->l2);
    printmsg_close_pair(&p_sampler->llc);
    free(p_sampler);
}
