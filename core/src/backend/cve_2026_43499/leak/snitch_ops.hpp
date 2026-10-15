#pragma once
/** @file backend/cve_2026_43499/leak/snitch_ops.hpp
 *  What the collision search needs from the platform, written as a concept.
 *
 *  The search maps a region its children share, has each child wait on its own futex
 *  and the parent wake them by how much they poked, and runs its scanning threads. All
 *  of that is the thin edge -- no sizes, no order, no decisions about when to stop --
 *  and it is separated from the search itself so the search can be reasoned about and
 *  the platform calls can be proved one at a time.
 *
 *  A concept rather than a base class, so a host fake and the device implementation are
 *  both checked where they are written: a fake that drifts from the device is caught
 *  where the fake lives, not where it is used.
 */
#include <concepts>
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/memory/heap_ops.hpp"

namespace ghostlock::backend::cve_2026_43499::leak {
    using ghostlock::backend::cve_2026_43499::memory::OpsResult;

    /** A mapping, or the errno that explains why there is none. */
    struct MapResult final {
        void *address = nullptr;
        std::int32_t error = 0;
    };

    /** The entry point a search thread runs, and what it is handed. */
    using ThreadEntry = void *(*)(void *);

    /** A thread, or the errno that explains why there is none. */
    struct ThreadResult final {
        unsigned long handle = 0U;
        std::int32_t error = 0;
    };

    /** A word the parent and its children share, and one they don't. */
    template <class Seam>
    concept SnitchOps = requires(std::size_t bytes, void *address, std::uint32_t *word,
                                  std::uint32_t expected, std::int32_t waiters, std::int64_t timeout_us, ThreadEntry entry,
                                  void *argument, unsigned long handle) {
        { Seam::map_shared(bytes) } -> std::same_as<MapResult>;
        { Seam::unmap(address, bytes) } -> std::same_as<OpsResult>;
        { Seam::futex_wait_private(word, expected) } -> std::same_as<OpsResult>;
        /* The same wait with a deadline: the parent has to give its cohort time, and it
         *  wants to wake early the moment the count it watches changes. */
        { Seam::futex_wait_private_until(word, expected, timeout_us) } -> std::same_as<OpsResult>;
        { Seam::futex_wake_private(word, waiters) } -> std::same_as<OpsResult>;
        { Seam::start_thread(entry, argument) } -> std::same_as<ThreadResult>;
        { Seam::join_thread(handle) } -> std::same_as<OpsResult>;
    };
}  // namespace ghostlock::backend::cve_2026_43499::leak
