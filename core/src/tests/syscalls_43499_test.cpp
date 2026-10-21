/** @file tests/syscalls_43499_test.cpp
 *  43499 薄 syscall 面的 host 实测：成功值、典型 errno、重复关闭/不可用平台的 ENOSYS。
 */
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/futex.h>
#include <sched.h>
#endif

#include "platform/syscalls_43499.hpp"

namespace {
    namespace sys = ghostlock::platform::syscalls43499;
    std::size_t g_failures = 0U;

    void expect(bool ok, const char *what) {
        if (!ok) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void memory_map_round_trip() {
        const long address = sys::mmap(4096U, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        expect(address > 0, "a page maps");
        if (address <= 0) return;
        auto *const byte = reinterpret_cast<unsigned char *>(address);
        byte[0] = 0x5AU;
        expect(byte[0] == 0x5AU, "the mapping is writable");
        expect(sys::munmap(static_cast<std::uintptr_t>(address), 4096U) == 0, "the page unmaps");
        expect(sys::munmap(0U, 4096U) < 0, "a zero address is refused");
        expect(sys::munmap(static_cast<std::uintptr_t>(address), 0U) < 0, "a zero length is refused");
        expect(sys::mmap(0U, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) < 0, "zero length is refused");
    }

    void clock_and_pid() {
        expect(sys::getpid() > 0, "getpid is positive");
        const long now = sys::clock_gettime_ns(CLOCK_MONOTONIC);
        expect(now > 0, "clock_gettime_ns is positive");
        expect(sys::usleep(1000U) == 0, "usleep returns zero");
        const long tid = sys::gettid();
        expect(tid > 0 || tid == -ENOSYS, "gettid is positive or named ENOSYS");
    }

    void sockets_and_pipes() {
        const long fd = sys::socket(AF_UNIX, SOCK_DGRAM, 0);
        expect(fd >= 0, "a unix datagram socket opens");
        if (fd >= 0) {
            expect(sys::close(static_cast<int>(fd)) == 0, "the socket closes");
            expect(sys::close(static_cast<int>(fd)) < 0, "a repeated close fails");
        }

        int fds[2] = {-1, -1};
        const long piped = sys::pipe2(fds, 0);
        expect(piped == 0, "a pipe opens");
        if (piped == 0) {
            const char byte = 0x7F;
            expect(sys::write(fds[1], &byte, 1U) == 1, "one byte writes");
            char back = 0;
            expect(sys::read(fds[0], &back, 1U) == 1 && back == 0x7F, "and reads back");
            expect(sys::close(fds[0]) == 0 && sys::close(fds[1]) == 0, "both ends close");
        }
    }

    void linux_only_or_named() {
#if defined(__linux__)
        int word = 0;
        const long woken = sys::futex(reinterpret_cast<std::uintptr_t>(&word), FUTEX_WAKE, 1, -1, 0U, 0);
        expect(woken >= 0, "futex wake on a user word succeeds");
        expect(sys::sched_getcpu() >= 0, "sched_getcpu is available");
        std::uint32_t value = 0x12345678U;
        std::uint32_t back = 0U;
        const long wrote = sys::process_vm_writev(
            static_cast<int>(::getpid()), reinterpret_cast<std::uintptr_t>(&back),
            reinterpret_cast<const std::uint8_t *>(&value), sizeof(value));
        expect(wrote == static_cast<long>(sizeof(value)), "process_vm_writev writes the word");
        std::uint32_t read_back = 0U;
        const long read = sys::process_vm_readv(
            static_cast<int>(::getpid()), reinterpret_cast<std::uintptr_t>(&back),
            reinterpret_cast<std::uint8_t *>(&read_back), sizeof(read_back));
        expect(read == static_cast<long>(sizeof(read_back)) && read_back == value,
               "process_vm_readv reads it back");
#else
        expect(sys::futex(0U, 0, 0, -1, 0U, 0) == -ENOSYS, "futex is named ENOSYS off Linux");
        expect(sys::sched_getcpu() == -ENOSYS, "sched_getcpu is named ENOSYS off Linux");
        expect(sys::process_vm_writev(0, 0U, nullptr, 0U) == -ENOSYS,
               "process_vm_writev is named ENOSYS off Linux");
#endif
        expect(sys::perf_event_open(nullptr, 0, 0, -1, 0UL) < 0, "perf with no attr is refused");
        expect(sys::ioctl(-1, 0UL, 0U) < 0, "ioctl on a bad fd is refused");
        const long memfd = sys::memfd_create("x", 0U);
#if defined(__linux__)
        expect(memfd >= 0, "memfd_create succeeds on Linux");
        if (memfd >= 0) expect(sys::close(static_cast<int>(memfd)) == 0, "and closes");
#else
        expect(memfd == -ENOSYS, "memfd_create is named ENOSYS off Linux");
#endif
        expect(sys::sched_setattr(0, nullptr) < 0, "sched_setattr with no attr is refused");
        expect(sys::rseq(nullptr, 0U, 0, 0U) < 0, "rseq with no area is refused");
    }
}  // namespace

int main() {
    memory_map_round_trip();
    clock_and_pid();
    sockets_and_pipes();
    linux_only_or_named();
    if (g_failures != 0U) {
        std::printf("syscalls_43499_test: %zu failure(s)\n", g_failures);
        return 1;
    }
    std::printf("syscalls_43499_test: ok\n");
    return 0;
}
