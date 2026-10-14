#pragma once
/** @file jni/race_driver.hpp
 *  The driver, parameterised from the pure-C original.
 *
 *  Source: bddfea4:src/core/main.c:337-359 (run_main_route_threads), with its reset at :331-334.
 *
 *  Two things about this shape are worth stating, because both differ from the C++ tree and
 *  a later reader comparing outputs would otherwise take them for unexplained drift:
 *
 *    1 There is no race object. The C driver creates the three threads directly and joins
 *      them; the C++ tree wrapped that in a PiRace class with a start_threads that took the
 *      three entries as function pointers. Here the threads are created in order and the
 *      entries are the ones cmp_disasm resolves.
 *
 *    2 The trigger lives in the driver, not in the waiter. After the waiter is parked and a
 *      short settle has elapsed, this thread issues FUTEX_CMP_REQUEUE_PI with nr_wake 1 and
 *      the target word as uaddr2. The C++ tree moved that call inside the waiter itself.
 *      Winning is judged the same way the C file judged it: the consumer must have been
 *      called at least once, succeeded at least once, and the route must not have recorded a
 *      failing step.
 *
 *  Every wait the original spelled as a literal (1000, 50000, 5000) and the delay constant it
 *  stored (PSELECT_ENTER_DELAY_USEC) arrive in RaceTuning; every flag it touched is a field of
 *  the context. Nothing here reads a global.
 */
#include <cstdint>

#include "jni/race_threads.hpp"

namespace ghostlock::jni {
    /**
     *  Run one race attempt: create, trigger, wait, stop, join, judge.
     *
     *  @param context  the actors' state; reset here, read and written by the entries.
     *  @return true when the consumer was called, succeeded, and the route recorded no
     *          failing step -- the same three conditions the C file returned.
     *  @note Refusals are named: a missing word or a zero wait is refused before any thread
     *        exists, never defaulted.
     */
    [[nodiscard]] bool run_main_route_threads(race::RaceContext &context) noexcept;
}  // namespace ghostlock::jni
