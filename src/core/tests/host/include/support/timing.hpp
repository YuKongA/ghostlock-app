#ifndef GHOSTLOCK_HOST_SUPPORT_TIMING_HPP
#define GHOSTLOCK_HOST_SUPPORT_TIMING_HPP

/* Host shadow of support/timing.hpp: the timeline is a no-op and timer_mark is
 * silent so the data-flow test does not need the logging stack. */

#include <ctime>

namespace ghostlock::support {
    inline struct timespec &exploit_t0(void) {
        static struct timespec t0;
        return t0;
    }

    inline void timer_reset(void) {}

    inline double timer_ms(void) { return 0.0; }

    inline void timer_mark(const char *label) { (void) label; }
} // namespace ghostlock::support

#endif
