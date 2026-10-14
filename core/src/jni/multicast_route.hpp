#pragma once
/** @file jni/multicast_route.hpp
 *  SUPERSEDED. This file was written from the C++ tree before the targets were re-sourced
 *  from the pure-C original (bddfea4:src/core/fops.c). The route it describes now lives in
 *  two files: pselect_route.cpp carries the pselect body as the C file wrote it, and
 *  pselect_fdsets.hpp carries the word tables it fills. The name this file declared,
 *  do_kernel5_fake_lock_route, is that function in pselect_route.hpp.
 *
 *  Kept rather than deleted: the sequence of edits that produced the C-version files is
 *  recorded in their own headers, and this file is the other side of that comparison.
 *
 *  The multicast-waiter route, parameterised.
 *
 *  Body from the retired tree's do_kernel5_fake_lock_route (route/multicast_waiter_route.cpp:18-).
 *  Three things changed, and they are the whole point of this experiment:
 *
 *    1 the geometry no longer comes from a profile read inside the route. The six values it
 *      needs (buffer size, three offsets, and the two addresses it injects) arrive in
 *      MulticastLayout, filled by the caller;
 *    2 the literal 8 in memcpy(stamp + 8, &family, 2) became layout.family_offset, because a
 *      hardcoded offset inside a core function is exactly the kind of decision that belongs
 *      to the caller;
 *    3 the entry no longer takes a WriteRequest it ignores. The retired signature carried one
 *      and immediately wrote (void) request; -- see the note in seam/multicast_window.hpp,
 *      "The route body receives that request and ignores it". A parameter that is always
 *      discarded is not a parameter, so it is gone.
 *
 *  Kept as mechanism, not moved: AF_INET, SOCK_DGRAM and the socket() call itself. The tree
 *  draws that line consistently (futex ABI constants stay in C for the same reason).
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::jni {
    /** The geometry and the two addresses one multicast attempt lands on. */
    struct MulticastLayout final {
        /** Size of the stamp buffer the kernel is handed. Zero is a hard refusal. */
        std::uint32_t buffer_size = 0U;
        /** Where the fake waiter sits inside the buffer. Zero is a hard refusal. */
        std::uint32_t waiter_offset = 0U;
        /** Where the fake task sits inside the buffer. Zero is a hard refusal. */
        std::uint32_t task_offset = 0U;
        /** Where the fake lock sits inside the buffer. Zero is a hard refusal. */
        std::uint32_t lock_offset = 0U;
        /** The address the walk is aimed at, and the lock it must find there. */
        std::uintptr_t fake_task = 0U;
        std::uintptr_t fake_lock = 0U;
        /** Where the address-family word goes inside the buffer, in bytes. */
        std::uint32_t family_offset = 0U;
        /** How many yield spins the route waits for the consumer; 0 is a refusal. */
        std::uint32_t spin_limit = 0U;
    };

    /** What one attempt reports, mirroring the retired RouteStatus fields. */
    struct RouteOutcome final {
        std::int32_t code = 0;
        std::int32_t step = 0;
        std::int32_t error_number = 0;
        bool userspace_clean = false;
        bool kernel_disarmed = false;
    };

    /**
     *  Run one multicast-waiter attempt.
     *
     *  @param layout  the geometry and addresses; every zero field is refused by name.
     *  @param out_fd  receives the socket the attempt built, or -1 on refusal.
     *  @return the outcome; code == 0 only on success.
     *  @note Absent geometry is a hard refusal, never a guessed landing.
     */
    [[nodiscard]] RouteOutcome do_kernel5_fake_lock_route(const MulticastLayout &layout,
                                                          int &out_fd) noexcept;
}  // namespace ghostlock::jni
