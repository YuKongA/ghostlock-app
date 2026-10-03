#ifndef GHOSTLOCK_SUPPORT_TIMING_HPP
#define GHOSTLOCK_SUPPORT_TIMING_HPP

#include "kernelsnitch/utils.h"
#include "support/decls.hpp"
#include "support/time.h"

#include <ctime>

namespace ghostlock::support {
    /* The single process-wide exploit timeline. Inline so the hot attack paths
     * keep the exact code shape they had when this lived in attack/ops.hpp. */
    inline struct timespec &exploit_t0(void) {
        static struct timespec t0;
        return t0;
    }

    inline void timer_reset(void) {
        clock_gettime(CLOCK_MONOTONIC, &exploit_t0());
    }

    inline double timer_ms(void) {
        return ghostlock::runtime_time::runtime_elapsed_ms(&exploit_t0());
    }

    inline void timer_mark(const char *label) {
        pr_info("[T+%.0fms] %s\n", timer_ms(), label);
        ghostlock::support::log_sync();
    }
} // namespace ghostlock::support

#endif
