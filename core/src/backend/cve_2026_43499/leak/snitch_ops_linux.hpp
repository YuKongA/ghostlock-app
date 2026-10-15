#pragma once
/** @file backend/cve_2026_43499/leak/snitch_ops_linux.hpp
 *  The collision search's edge on the platform this runs on: no decisions, only calls.
 *
 *  Android only, and deliberately not part of the host build: it maps, waits and
 *  starts threads. Each call answers with its result and the errno beside it, and does
 *  nothing else -- no retry, no fallback, no interpreting what a failure means.
 */
#include <cerrno>
#include <ctime>
#include <cstddef>
#include <cstdint>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <unistd.h>

#include "backend/cve_2026_43499/leak/snitch_ops.hpp"

namespace ghostlock::backend::cve_2026_43499::leak {
    struct LinuxSnitchOps final {
        static MapResult map_shared(std::size_t bytes) noexcept {
            errno = 0;
            void *mapped = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                                  MAP_SHARED | MAP_ANONYMOUS, -1, 0);
            const std::int32_t error = errno;
            if (mapped == MAP_FAILED) {
                return MapResult{nullptr, error};
            }
            return MapResult{mapped, error};
        }

        static OpsResult unmap(void *address, std::size_t bytes) noexcept {
            errno = 0;
            const int rc = ::munmap(address, bytes);
            return OpsResult{static_cast<std::int64_t>(rc), errno};
        }

        static OpsResult futex_wait_private(std::uint32_t *word, std::uint32_t expected) noexcept {
            errno = 0;
            const long rc = ::syscall(SYS_futex, word, FUTEX_WAIT_PRIVATE,
                                      static_cast<std::uint32_t>(expected), nullptr, nullptr, 0U);
            return OpsResult{static_cast<std::int64_t>(rc), errno};
        }

        static OpsResult futex_wait_private_until(std::uint32_t *word, std::uint32_t expected,
                                                    std::int64_t timeout_us) noexcept {
            errno = 0;
            struct timespec budget {};
            budget.tv_sec = static_cast<time_t>(timeout_us / 1000000);
            budget.tv_nsec = static_cast<long>((timeout_us % 1000000) * 1000);
            const long rc = ::syscall(SYS_futex, word, FUTEX_WAIT_PRIVATE,
                                      static_cast<std::uint32_t>(expected), &budget, nullptr, 0U);
            return OpsResult{static_cast<std::int64_t>(rc), errno};
        }

        static OpsResult futex_wake_private(std::uint32_t *word, std::int32_t waiters) noexcept {
            errno = 0;
            const long rc = ::syscall(SYS_futex, word, FUTEX_WAKE_PRIVATE,
                                      static_cast<std::uint32_t>(waiters), nullptr, nullptr, 0U);
            return OpsResult{static_cast<std::int64_t>(rc), errno};
        }

        static ThreadResult start_thread(ThreadEntry entry, void *argument) noexcept {
            errno = 0;
            pthread_t handle = 0U;
            const int rc = ::pthread_create(&handle, nullptr, entry, argument);
            if (rc != 0) {
                return ThreadResult{0U, rc};
            }
            return ThreadResult{static_cast<unsigned long>(handle), 0};
        }

        static OpsResult join_thread(unsigned long handle) noexcept {
            const int rc = ::pthread_join(static_cast<pthread_t>(handle), nullptr);
            return OpsResult{static_cast<std::int64_t>(rc), rc};
        }
    };

    /* The device implementation answers the shape, checked where it is written: a landing
     *  that answers five of six calls is not a landing. */
    static_assert(SnitchOps<LinuxSnitchOps>,
                  "the device landing answers every call the shape names");
}  // namespace ghostlock::backend::cve_2026_43499::leak
