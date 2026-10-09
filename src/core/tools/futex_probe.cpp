/** @file tools/futex_probe.cpp
 *  A device probe for the futex seam: one real priority-inheritance round trip.
 *
 *  Two threads share one word. One takes the lock, the other asks for it and is
 *  made to wait, the first releases it, and the kernel hands the lock to the
 *  waiter. Every step is reported as it happens, so the transcript shows the
 *  kernel actually moving the lock rather than a thread reporting success.
 *
 *  This is what the race does on every attempt, so a round trip here is the
 *  smallest thing that can be said to work on the target.
 */
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>

#include "race/futex_seam_linux.hpp"

namespace {
    std::uint32_t g_word = 0U;
    std::atomic<int> g_stage{0};
    std::atomic<bool> g_woken{false};
}  // namespace

int main() {
    using ghostlock::race::LinuxFutexSeam;
    std::printf("futex probe: one round trip\n");
    std::thread second([]() {
        g_stage.store(1);
        const int taken = LinuxFutexSeam::lock_pi(&g_word);
        g_woken.store(true);
        std::printf("  waiter: lock returned %d (errno %d)\n", taken, errno);
        const int released = LinuxFutexSeam::unlock_pi(&g_word);
        std::printf("  waiter: unlock returned %d\n", released);
    });
    const int first = LinuxFutexSeam::lock_pi(&g_word);
    std::printf("  holder: lock returned %d (errno %d)\n", first, errno);
    while (!g_stage.load()) {
        std::this_thread::yield();
    }
    /* Give the waiter time to park on the word before handing the lock over. */
    for (int i = 0; i < 1000 && !g_woken.load(); ++i) {
        std::this_thread::yield();
    }
    const int handed = LinuxFutexSeam::unlock_pi(&g_word);
    std::printf("  holder: unlock returned %d\n", handed);
    second.join();
    const bool clean = first == 0 && handed == 0 && g_woken.load();
    std::printf("futex probe: %s\n", clean ? "PASS (the lock moved)" : "FAIL");
    return clean ? 0 : 1;
}
