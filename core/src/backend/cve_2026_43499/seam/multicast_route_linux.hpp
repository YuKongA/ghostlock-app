#pragma once
/** @file backend/cve_2026_43499/seam/multicast_route_linux.hpp
 *  The multicast route's platform edge on the target device.
 *
 *  Every call the route makes, as real calls: the five priority-inheritance operations of the futex
 *  interface (two of them with a deadline), the scheduling call the consumer makes, the loop
 *  primitives, the datagram socket the stamp travels on, and the errno the shape rule reads. Nothing
 *  here decides anything -- the order, the budgets and the roles all live in the route, so the route
 *  can be read and driven on a host while this file is the only one that has to be on a device.
 *
 *  Linux only: the syscall numbers, the futex operation values and the structure of the socket option
 *  are the platform's. The fakes a host test uses are its own, beside the test.
 */
#if defined(__linux__)
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <ctime>

#include <linux/futex.h>
#include <netinet/in.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "backend/cve_2026_43499/seam/multicast_consumer.hpp"

namespace ghostlock::backend::cve_2026_43499::seam {
    struct LinuxRouteOps final {
        /** The futex call, in the one shape all five operations share. */
        static int futex(std::uint32_t *word, int operation, std::uint32_t value,
                         const struct timespec *deadline, std::uint32_t *other) noexcept {
            return static_cast<int>(
                ::syscall(SYS_futex, word, operation, value, deadline, other, 0));
        }

        /** 同上，但带 val3：CMP_REQUEUE_PI 的 val3 必须等于目标字此刻的值，写死 0 会被判 EAGAIN。 */
        static int futex_val3(std::uint32_t *word, int operation, std::uint32_t value,
                             const struct timespec *deadline, std::uint32_t *other,
                             std::uint32_t val3) noexcept {
            return static_cast<int>(
                ::syscall(SYS_futex, word, operation, value, deadline, other, val3));
        }

        /** Take the lock, waiting for it as long as it takes. */
        static int lock_pi(std::uint32_t *word) noexcept {
            return futex(word, FUTEX_LOCK_PI, 0U, nullptr, nullptr);
        }

        /** Take the lock, or give up at the deadline. A deadline of zero is already past, which is
         *  what the post-trigger removal wants: it goes through the slow path and returns. */
        static int lock_pi_until(std::uint32_t *word, std::int64_t micros) noexcept {
            struct timespec deadline{};
            if (micros > 0) {
                deadline.tv_sec = static_cast<time_t>(micros / 1000000);
                deadline.tv_nsec = static_cast<long>((micros % 1000000) * 1000);
            } else {
                deadline.tv_sec = 0;
                deadline.tv_nsec = 1L;
            }
            return futex(word, FUTEX_LOCK_PI, 0U, &deadline, nullptr);
        }

        /** Park on this word asking to be moved onto the other one; the deadline bounds the park. */
        static int wait_requeue_pi_until(std::uint32_t *word, std::uint32_t *other,
                                         std::int64_t micros) noexcept {
            struct timespec span{};
            if (micros > 0) {
                span.tv_sec = static_cast<time_t>(micros / 1000000);
                span.tv_nsec = static_cast<long>((micros % 1000000) * 1000);
            } else {
                span.tv_sec = 0;
                span.tv_nsec = 1L;
            }
            /* 睡的条件是 word 此刻的值：写死 0 的话，被锤过的字一读不等就立刻 EAGAIN 返回 ——
             *  等待者根本没睡，requeue 那边自然搬 0 个（真机量到过）。 */
            const std::uint32_t expected = (word != nullptr) ? *word : 0U;
            return futex(word, FUTEX_WAIT_REQUEUE_PI, expected, &span, other);
        }

        /** The other side of the park: move this word's waiters onto the target. */
        static int cmp_requeue_pi(std::uint32_t *word, std::uint32_t *other) noexcept {
            /* 两条都必须对（手工实验留下的结论，现按此移植）：
             *  ① nr_requeue 走 timeout 这个参数（该调用的约定），传 nullptr 就是 0 ⇒ 内核只唤醒、不搬、
             *     返回 0（设备上量的正是 0 ✗）；② val3 必须是 other 字此刻的值，写死 0 会被判 EAGAIN。 */
            /* ★★ 第 257 轮 ✓（按 ref 指示：`ghostlock-app-ref/.../race/threads.cpp:182` 逐字调用
             *  `futex_op(&wait_futex, FUTEX_CMP_REQUEUE_PI, 1, (void*)1, &target_futex, **0**)` ✓）——
             *  ⇒ **`val3` 写死 0** ✓（⇒ 第 177 轮的"从 other 读"与此**相反** ✗ ⇒ 回退 ✓）。 */
            const std::uint32_t expected = 0U;   /* ref 逐字：val3 = 0 ✓ */
            /* ★ 第 177 轮 ✓：`val` 必须等于 **`*word`**（内核判据 ✗）—— 旧码是在 requeue 前把该字**置 1** ✓，
             *  而本树的 `chain_word` 此刻是 **waiter 的 TID** ✗ ⇒ 写死 `1U` 必然 EAGAIN(11) ✗（真机 32/32 ✓）⇒
             *  ⇒ 改为一并**从字里读** ✓（⇒ 与 `val3` 同法 ✓）。 */
            /* ★★ 第 257 轮 ✓（ref 逐字：`val = 1` ✓）⇒ 写死 1 ✓（⇒ 而 `requeue()` 侧仍在 requeue 前把该字置 1 ✓ ⇒ 两者一致 ✓）。 */
            const std::uint32_t want_val = 1U;   /* ref 逐字：val = 1 ✓ */
            /* ★ 第 176 轮 ✓（旧码 `641aaf6` 第 4 参 = `(void*)0` ✓）：**`nr_requeue` 必须为 0** ✗ ——
             *  原先传 `1` ⇒ 内核走"搬 1 个"分支 ⇒ 与 `val3` 一起被判 **EAGAIN(11)** ✗（真机 26/26 ✓）。 */
            const auto *want_requeue = static_cast<const struct timespec *>(nullptr);
            return futex_val3(word, FUTEX_CMP_REQUEUE_PI, want_val, want_requeue, other, expected);
        }

        /** Release the lock, handing it to the highest-priority waiter. */
        static int unlock_pi(std::uint32_t *word) noexcept {
            return futex(word, FUTEX_UNLOCK_PI, 0U, nullptr, nullptr);
        }

        /** The consumer's act: one scheduling-attribute change on another thread, which is what makes
         *  the kernel walk the priority-inheritance chain. */
        static int setattr(std::int32_t tid, std::int32_t nice, std::uint32_t policy) noexcept {
            return static_cast<int>(sched_setattr_nice(tid, nice, policy));
        }

        /** 当前线程 pin 到某个 CPU（退役门禁的"固定 CPU 对"）。 */
        static int pin(std::int32_t cpu) noexcept {
            if (cpu < 0) {
                return 0;
            }
            cpu_set_t set{};
            CPU_ZERO(&set);
            CPU_SET(static_cast<std::size_t>(cpu), &set);
            return ::sched_setaffinity(0, sizeof(set), &set);
        }

        static void yield() noexcept { ::sched_yield(); }

        static void sleep_micros(std::int64_t micros) noexcept {
            if (micros > 0) {
                (void) ::usleep(static_cast<useconds_t>(micros));
            }
        }

        static std::int32_t thread_id() noexcept {
            return static_cast<std::int32_t>(::syscall(SYS_gettid));
        }

        /** The socket the stamp travels on. Close-on-exec, because a descriptor this route opened
         *  must not follow a later exec into whatever runs next. */
        static int open_datagram() noexcept {
            return ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        }

        /** One copy of the stamp, onto this thread's kernel stack. */
        static int set_block_source(int fd, const std::uint8_t *buffer, std::size_t size) noexcept {
            errno = 0;
            return ::setsockopt(fd, IPPROTO_IP, MCAST_BLOCK_SOURCE, buffer,
                                static_cast<socklen_t>(size));
        }

        static void close_fd(int fd) noexcept {
            if (fd >= 0) {
                (void) ::close(fd);
            }
        }

        /** The errno the shape rule reads, taken straight after the call that set it. */
        static std::int32_t last_errno() noexcept { return static_cast<std::int32_t>(errno); }

        /** The one refusal that still means the structure was copied: no address for the group
         *  source, which the kernel decides after the copy. */
        static std::int32_t addr_not_available() noexcept {
            return static_cast<std::int32_t>(EADDRNOTAVAIL);
        }
    };
}  // namespace ghostlock::backend::cve_2026_43499::seam
#endif
