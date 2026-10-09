#pragma once
/** @file race/futex_seam_linux.hpp
 *  The real futex calls, behind the seam the race policy is written against.
 *
 *  pi_race.cpp is a shape and takes its syscalls from a policy type; this is the
 *  policy for the target: the four priority-inheritance operations the race uses,
 *  issued through the futex syscall with the operation numbers the interface
 *  defines. Nothing here decides anything -- it is the thin edge where the race
 *  meets the kernel, kept separate so the shape can be read and tested without
 *  one and the seam exercised on a device without the other.
 *
 *  Linux only: the syscall number and the structure of the calls are the
 *  platform's, which is why this file is not part of the host build.
 */
#include <cerrno>
#include <cstdint>

#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "race/pi_race.hpp"

namespace ghostlock::race {
    struct LinuxFutexSeam final {
        /** Take the lock for this thread, waiting for it if another holds it. */
        static int lock_pi(std::uint32_t *word) noexcept {
            return static_cast<int>(::syscall(SYS_futex, word, FUTEX_LOCK_PI, 0, nullptr, nullptr,
                                               0));
        }

        /** Release the lock, handing it to the highest-priority waiter. */
        static int unlock_pi(std::uint32_t *word) noexcept {
            return static_cast<int>(::syscall(SYS_futex, word, FUTEX_UNLOCK_PI, 0, nullptr, nullptr,
                                               0));
        }

        /** Park on the chain word and ask the kernel to requeue onto the target
         *  when the target's lock is released: the operation the race is built on. */
        static int wait_requeue_pi(std::uint32_t *chain, std::uint32_t *target) noexcept {
            return static_cast<int>(::syscall(SYS_futex, chain, FUTEX_WAIT_REQUEUE_PI, 0, nullptr,
                                               target, 0));
        }

        /** The other side of that operation: requeue the chain's waiters here. */
        static int cmp_requeue_pi(std::uint32_t *chain, std::uint32_t *target) noexcept {
            return static_cast<int>(::syscall(SYS_futex, chain, FUTEX_CMP_REQUEUE_PI, 1, nullptr,
                                               target, 0));
        }
    };
}  // namespace ghostlock::race
