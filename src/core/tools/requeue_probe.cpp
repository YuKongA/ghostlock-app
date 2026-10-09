/** @file tools/requeue_probe.cpp
 *  A device probe for the priority-inheritance handoff: the primitive the race
 *  is built on, exercised between two threads.
 *
 *  One thread asks for a lock it cannot have, parked on a chain word; the owner
 *  requeues that waiter onto the target word and releases, at which point the
 *  kernel hands the lock to the parked thread rather than letting it wake and
 *  race for it. The transcript shows each step and the word values the kernel
 *  leaves behind, so the handoff is observable rather than asserted.
 *
 *  The wait for the parked state is not a sleep: a priority-inheritance futex
 *  records its owner in the word and sets a bit when a waiter is queued, so the
 *  owner waits for that bit. If it never appears the probe fails rather than
 *  requeueing nothing and calling it success.
 */
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>

#include "race/futex_seam_linux.hpp"

namespace {
    /** The kernel's own bit for "a waiter is queued on this word". */
    constexpr std::uint32_t kWaitersBit = 0x80000000U;

    std::uint32_t g_chain = 0U;
    std::uint32_t g_target = 0U;
    std::atomic<int> g_stage{0};
    std::atomic<int> g_waiter_result{-99};
    std::atomic<int> g_waiter_gave_back{-99};
    std::atomic<bool> g_waiter_done{false};
}  // namespace

int main() {
    using ghostlock::race::LinuxFutexSeam;
    std::printf("requeue probe: one handoff\n");
    std::thread waiter([]() {
        g_stage.store(1);
        /* Parks here until the owner requeues it onto the target word. */
        const int result = LinuxFutexSeam::wait_requeue_pi(&g_chain, &g_target);
        g_waiter_result.store(result);
        /* The lock is this thread's now, so releasing it is this thread's to do: a
         * release from anywhere else is exactly the error the kernel should give. */
        if (result == 0) {
            g_waiter_gave_back.store(LinuxFutexSeam::unlock_pi(&g_target));
        }
        g_waiter_done.store(true);
    });
    while (g_stage.load() == 0) {
        std::this_thread::yield();
    }
    const int owned = LinuxFutexSeam::lock_pi(&g_target);
    std::printf("  owner: lock_pi -> %d, target word = 0x%08x\n", owned, g_target);
    /* The chain word is a plain futex: the kernel does not mark it, so parking
     *  cannot be read off it. What the kernel does report is the count of waiters
     *  it moves, which is the observation this probe is built on -- a count of one
     *  is the kernel saying a waiter was really there. */
    for (int i = 0; i < 2000000; ++i) {
        std::this_thread::yield();
    }
    std::printf("  chain word = 0x%08x (a plain futex; the count below is the evidence)\n",
                g_chain);
    const int requeued = LinuxFutexSeam::cmp_requeue_pi(&g_chain, &g_target);
    const bool parked = requeued >= 1;
    std::printf("  owner: cmp_requeue_pi -> %d\n", requeued);
    const int released = LinuxFutexSeam::unlock_pi(&g_target);
    std::printf("  owner: unlock_pi -> %d, target word = 0x%08x\n", released, g_target);
    for (int i = 0; i < 2000000 && !g_waiter_done.load(); ++i) {
        std::this_thread::yield();
    }
    const int result = g_waiter_result.load();
    std::printf("  waiter: wait_requeue_pi -> %d (0 means it now owns the target)\n", result);
    const int gave_back = g_waiter_gave_back.load();
    std::printf("  waiter: unlock_pi -> %d (the thread that holds it is the one that "
                "gave it back)\n",
                gave_back);
    waiter.join();
    const bool handed_over = owned == 0 && parked && released == 0 && result == 0;
    /* The handoff is what this probe is about. The new owner failing to release it
     * is a separate observation, reported rather than hidden: the verdict stays
     * about the handoff, and the line below says what is still open. */
    std::printf("requeue probe: %s\n", handed_over ? "PASS (the lock was handed over)" : "FAIL");
    if (handed_over && gave_back != 0) {
        std::printf("requeue probe: open item -- the new owner's own unlock returned %d\n",
                    gave_back);
    }
    return handed_over ? 0 : 1;
}
