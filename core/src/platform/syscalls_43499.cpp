/** @file platform/syscalls_43499.cpp
 *  43499 的逐 syscall 薄实现（见头文件约定）。
 */
#include "platform/syscalls_43499.hpp"

#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/futex.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#endif

namespace ghostlock::platform::syscalls43499 {
    namespace {
        [[nodiscard]] long failed() noexcept { return -static_cast<long>(errno); }
    }  // namespace

    long futex(std::uintptr_t uaddr, int op, int value, std::int64_t timeout_ns,
               std::uintptr_t uaddr2, int value3) noexcept {
#if defined(__linux__)
        timespec timeout{};
        timespec *timeout_ptr = nullptr;
        if (timeout_ns >= 0) {
            timeout.tv_sec = static_cast<time_t>(timeout_ns / 1000000000);
            timeout.tv_nsec = static_cast<long>(timeout_ns % 1000000000);
            timeout_ptr = &timeout;
        }
        const long result = ::syscall(SYS_futex, reinterpret_cast<void *>(uaddr), op, value,
                                      timeout_ptr, reinterpret_cast<void *>(uaddr2), value3);
        return result >= 0 ? result : failed();
#else
        static_cast<void>(uaddr);
        static_cast<void>(op);
        static_cast<void>(value);
        static_cast<void>(timeout_ns);
        static_cast<void>(uaddr2);
        static_cast<void>(value3);
        return -ENOSYS;
#endif
    }

    long mmap(std::uintptr_t length, int prot, int flags, int fd, std::int64_t offset) noexcept {
        if (length == 0U) {
            return -EINVAL;
        }
        void *const address = ::mmap(nullptr, length, prot, flags, fd, offset);
        if (address == MAP_FAILED) {
            return failed();
        }
        return static_cast<long>(reinterpret_cast<std::uintptr_t>(address));
    }

    long munmap(std::uintptr_t address, std::uintptr_t length) noexcept {
        if (address == 0U || length == 0U) {
            return -EINVAL;
        }
        return ::munmap(reinterpret_cast<void *>(address), length) == 0 ? 0 : failed();
    }

    long memfd_create(const char *name, unsigned int flags) noexcept {
#if defined(__linux__)
        const long fd = ::syscall(SYS_memfd_create, name, flags);
        return fd >= 0 ? fd : failed();
#else
        static_cast<void>(name);
        static_cast<void>(flags);
        return -ENOSYS;
#endif
    }

    long socket(int domain, int type, int protocol) noexcept {
        const int fd = ::socket(domain, type, protocol);
        return fd >= 0 ? fd : failed();
    }

    long setsockopt(int fd, int level, int option, const void *value, std::size_t length) noexcept {
        return ::setsockopt(fd, level, option, value, static_cast<socklen_t>(length)) == 0
                   ? 0
                   : failed();
    }

    long send(int fd, const void *buffer, std::size_t length, int flags) noexcept {
        const ssize_t sent = ::send(fd, buffer, length, flags);
        return sent >= 0 ? static_cast<long>(sent) : failed();
    }

    long recv(int fd, void *buffer, std::size_t length, int flags) noexcept {
        const ssize_t received = ::recv(fd, buffer, length, flags);
        return received >= 0 ? static_cast<long>(received) : failed();
    }

    long close(int fd) noexcept { return ::close(fd) == 0 ? 0 : failed(); }

    long read(int fd, void *buffer, std::size_t length) noexcept {
        const ssize_t read_bytes = ::read(fd, buffer, length);
        return read_bytes >= 0 ? static_cast<long>(read_bytes) : failed();
    }

    long write(int fd, const void *buffer, std::size_t length) noexcept {
        const ssize_t written = ::write(fd, buffer, length);
        return written >= 0 ? static_cast<long>(written) : failed();
    }

    long perf_event_open(const void *attr, int pid, int cpu, int group_fd,
                         unsigned long flags) noexcept {
#if defined(__linux__)
        const long fd = ::syscall(SYS_perf_event_open, attr, pid, cpu, group_fd, flags);
        return fd >= 0 ? fd : failed();
#else
        static_cast<void>(attr);
        static_cast<void>(pid);
        static_cast<void>(cpu);
        static_cast<void>(group_fd);
        static_cast<void>(flags);
        return -ENOSYS;
#endif
    }

    long ioctl(int fd, unsigned long request, std::uintptr_t arg) noexcept {
#if defined(__linux__)
        /* Linux ioctl 的 request 是 32 位命令字；显式截断避免 Android bionic 的签名告警。 */
        const int result = ::ioctl(fd, static_cast<unsigned int>(request),
                                   reinterpret_cast<void *>(arg));
        return result >= 0 ? result : failed();
#else
        static_cast<void>(fd);
        static_cast<void>(request);
        static_cast<void>(arg);
        return -ENOSYS;
#endif
    }

    long gettid() noexcept {
#if defined(__linux__)
        const long tid = ::syscall(SYS_gettid);
        return tid >= 0 ? tid : failed();
#else
        return -ENOSYS;
#endif
    }

    long getpid() noexcept {
        const long pid = ::getpid();
        return pid >= 0 ? pid : failed();
    }

    long process_vm_writev(int pid, std::uintptr_t remote, const std::uint8_t *local,
                           std::size_t length) noexcept {
#if defined(__linux__)
        if (local == nullptr || length == 0U) return -EINVAL;
        iovec local_iov{const_cast<std::uint8_t *>(local), length};
        iovec remote_iov{reinterpret_cast<void *>(remote), length};
        const long result = ::syscall(SYS_process_vm_writev, pid, &local_iov, 1UL, &remote_iov, 1UL,
                                      0UL);
        return result >= 0 ? result : failed();
#else
        static_cast<void>(pid);
        static_cast<void>(remote);
        static_cast<void>(local);
        static_cast<void>(length);
        return -ENOSYS;
#endif
    }

    long process_vm_readv(int pid, std::uintptr_t remote, std::uint8_t *local,
                          std::size_t length) noexcept {
#if defined(__linux__)
        if (local == nullptr || length == 0U) return -EINVAL;
        iovec local_iov{local, length};
        iovec remote_iov{reinterpret_cast<void *>(remote), length};
        const long result = ::syscall(SYS_process_vm_readv, pid, &local_iov, 1UL, &remote_iov, 1UL,
                                      0UL);
        return result >= 0 ? result : failed();
#else
        static_cast<void>(pid);
        static_cast<void>(remote);
        static_cast<void>(local);
        static_cast<void>(length);
        return -ENOSYS;
#endif
    }

    long usleep(std::uint64_t micros) noexcept {
        timespec delay{};
        delay.tv_sec = static_cast<time_t>(micros / 1000000U);
        delay.tv_nsec = static_cast<long>((micros % 1000000U) * 1000U);
        return ::nanosleep(&delay, nullptr) == 0 ? 0 : failed();
    }

    long clock_gettime_ns(int clock_id) noexcept {
        timespec now{};
        if (::clock_gettime(static_cast<clockid_t>(clock_id), &now) != 0) {
            return failed();
        }
        return static_cast<long>(now.tv_sec) * 1000000000L + now.tv_nsec;
    }

    long pipe2(int *fds, int flags) noexcept {
        if (fds == nullptr) {
            return -EINVAL;
        }
#if defined(__linux__)
        const long result = ::syscall(SYS_pipe2, fds, flags);
        return result >= 0 ? result : failed();
#else
        if (::pipe(fds) != 0) {
            return failed();
        }
        if ((flags & O_CLOEXEC) != 0) {
            static_cast<void>(::fcntl(fds[0], F_SETFD, FD_CLOEXEC));
            static_cast<void>(::fcntl(fds[1], F_SETFD, FD_CLOEXEC));
        }
        if ((flags & O_NONBLOCK) != 0) {
            static_cast<void>(::fcntl(fds[0], F_SETFL, O_NONBLOCK));
            static_cast<void>(::fcntl(fds[1], F_SETFL, O_NONBLOCK));
        }
        return 0;
#endif
    }

    long sched_setaffinity(int pid, std::uint64_t cpu_mask) noexcept {
#if defined(__linux__)
        cpu_set_t mask;
        CPU_ZERO(&mask);
        for (int cpu = 0; cpu < 64; ++cpu) {
            if ((cpu_mask & (std::uint64_t{1} << static_cast<unsigned>(cpu))) != 0U) {
                CPU_SET(cpu, &mask);
            }
        }
        return ::sched_setaffinity(pid, sizeof(mask), &mask) == 0 ? 0 : failed();
#else
        static_cast<void>(pid);
        static_cast<void>(cpu_mask);
        return -ENOSYS;
#endif
    }

    long sched_setattr(int tid, const void *attr) noexcept {
#if defined(__linux__)
        const long result = ::syscall(SYS_sched_setattr, tid, attr, 0U);
        return result >= 0 ? result : failed();
#else
        static_cast<void>(tid);
        static_cast<void>(attr);
        return -ENOSYS;
#endif
    }

    long sched_getcpu() noexcept {
#if defined(__linux__)
        const int cpu = ::sched_getcpu();
        return cpu >= 0 ? cpu : failed();
#else
        return -ENOSYS;
#endif
    }

    long rseq(const void *rseq, std::uint32_t length, int flags, std::uint32_t signature) noexcept {
#if defined(__linux__)
        const long result = ::syscall(SYS_rseq, rseq, length, flags, signature);
        return result >= 0 ? result : failed();
#else
        static_cast<void>(rseq);
        static_cast<void>(length);
        static_cast<void>(flags);
        static_cast<void>(signature);
        return -ENOSYS;
#endif
    }
}  // namespace ghostlock::platform::syscalls43499
