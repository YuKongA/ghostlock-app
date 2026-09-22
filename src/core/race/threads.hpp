#ifndef GHOSTLOCK_ROUTE_THREADS_HPP
#define GHOSTLOCK_ROUTE_THREADS_HPP

#include "common.h"
#include "support/status.hpp"

namespace ghostlock::race {
    /* Thread entry points registered with PiRace::start_threads(). */
    void *waiter_thread(void *arg);

    void *owner_thread(void *arg);

    void *consumer_thread(void *arg);

    /* Reset one PI race attempt; preserves the caller-owned fast-repair latch. */
    void reset_main_route_state(void);

    /* Create, synchronize, stop and join one explicitly owned PI race. */
    Status run_main_route_threads(const ghostlock::memory::WriteRequest &request);
} // namespace ghostlock::race

#endif
