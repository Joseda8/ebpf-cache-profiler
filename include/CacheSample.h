#ifndef CACHESAMPLE_H
#define CACHESAMPLE_H

#include <stdint.h>

/**
 * @brief Cumulative read cache counters for a target PID since profiling start.
 */
struct CacheSample {
    uint64_t l1ReadAccessTotal;
    uint64_t l1ReadMissTotal;
    uint64_t l2ReadAccessTotal;
    uint64_t l2ReadMissTotal;
    uint64_t llcReadAccessTotal;
    uint64_t llcReadMissTotal;
};

#endif
