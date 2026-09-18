#ifndef GHOSTLOCK_ROUTE_THREADS_HPP
#define GHOSTLOCK_ROUTE_THREADS_HPP

#include "common.h"

/* Thread entry points registered with PiRace::start_threads(). */
void *waiter_thread(void *arg);
void *owner_thread(void *arg);
void *consumer_thread(void *arg);

#endif
