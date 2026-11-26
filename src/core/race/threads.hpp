#ifndef LSPRIVILEGE_ROUTE_THREADS_HPP
#define LSPRIVILEGE_ROUTE_THREADS_HPP

#include "common.h"
#include "support/status.hpp"

namespace lsprivilege::race {
    /* Thread entry points registered with PiRace::start_threads(). */
    void *waiter_thread(void *arg);

    void *owner_thread(void *arg);

    void *consumer_thread(void *arg);

    /* Reset one PI race attempt; preserves the caller-owned fast-repair latch. */
    void reset_main_route_state(void);

    /* Create, synchronize, stop and join one explicitly owned PI race. */
    Status run_main_route_threads(const lsprivilege::memory::WriteRequest &request);
} // namespace lsprivilege::race

#endif
