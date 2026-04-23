#ifndef CACHESAMPLE_H
#define CACHESAMPLE_H

#include <stdint.h>

/**
 * @brief Snapshot of read cache counters for a target PID.
 */
struct CacheSample {
    uint64_t l1ReadAccesses;
    uint64_t l1ReadMisses;
    uint64_t l2ReadAccesses;
    uint64_t l2ReadMisses;
    uint64_t llcReadAccesses;
    uint64_t llcReadMisses;
};

#endif
