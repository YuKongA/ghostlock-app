#pragma once
/** @file platform/syscalls_43499.hpp
 *  43499 kt-step 的逐 syscall 薄接口（无状态、无决策）。
 *
 *  约定：返回值 >= 0 是成功值；< 0 是 -errno。调用方（Kotlin）负责解释与清理。
 *  这里不做参数组合、不做重试、不持有 fd/映射/线程。
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::platform::syscalls43499 {
    [[nodiscard]] long futex(std::uintptr_t uaddr, int op, int value,
                             std::int64_t timeout_ns, std::uintptr_t uaddr2, int value3) noexcept;
    [[nodiscard]] long mmap(std::uintptr_t length, int prot, int flags, int fd,
                            std::int64_t offset) noexcept;
    [[nodiscard]] long munmap(std::uintptr_t address, std::uintptr_t length) noexcept;
    [[nodiscard]] long memfd_create(const char *name, unsigned int flags) noexcept;
    [[nodiscard]] long socket(int domain, int type, int protocol) noexcept;
    [[nodiscard]] long setsockopt(int fd, int level, int option, const void *value,
                                  std::size_t length) noexcept;
    [[nodiscard]] long send(int fd, const void *buffer, std::size_t length, int flags) noexcept;
    [[nodiscard]] long recv(int fd, void *buffer, std::size_t length, int flags) noexcept;
    [[nodiscard]] long close(int fd) noexcept;
    [[nodiscard]] long read(int fd, void *buffer, std::size_t length) noexcept;
    [[nodiscard]] long write(int fd, const void *buffer, std::size_t length) noexcept;
    [[nodiscard]] long perf_event_open(const void *attr, int pid, int cpu, int group_fd,
                                       unsigned long flags) noexcept;
    [[nodiscard]] long ioctl(int fd, unsigned long request, std::uintptr_t arg) noexcept;
    [[nodiscard]] long gettid() noexcept;
    [[nodiscard]] long getpid() noexcept;
    [[nodiscard]] long process_vm_writev(int pid, std::uintptr_t remote, const std::uint8_t *local,
                                        std::size_t length) noexcept;
    [[nodiscard]] long process_vm_readv(int pid, std::uintptr_t remote, std::uint8_t *local,
                                       std::size_t length) noexcept;
    [[nodiscard]] long usleep(std::uint64_t micros) noexcept;
    [[nodiscard]] long clock_gettime_ns(int clock_id) noexcept;
    [[nodiscard]] long pipe2(int *fds, int flags) noexcept;
    [[nodiscard]] long sched_setaffinity(int pid, std::uint64_t cpu_mask) noexcept;
    [[nodiscard]] long sched_setattr(int tid, const void *attr) noexcept;
    [[nodiscard]] long sched_getcpu() noexcept;
    [[nodiscard]] long rseq(const void *rseq, std::uint32_t length, int flags,
                            std::uint32_t signature) noexcept;
}  // namespace ghostlock::platform::syscalls43499
