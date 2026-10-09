#pragma once
/** @file profile/queue_keys.hpp
 *  Where a backend's step queue lives and what its entries are called.
 *
 *  The document keeps a backend's selected steps as an array of one-key maps
     *  under its own section, and the key inside each entry names the step. Both
     *  spellings live here so the reader and anything that writes a queue cannot
     *  drift apart.
 */

namespace ghostlock::profile {
    /** The key under a backend section whose value is the queue. */
    inline constexpr const char *kQueueKey = "queue";
    /** The key inside one queue entry whose value is the step token. */
    inline constexpr const char *kQueueStepKey = "step";
}  // namespace ghostlock::profile
