/** @file tools/snitch_ops_probe.cpp
 *  The collision search's edge on the device, one call at a time.
 *
 *  Four things are asked of the kernel here. Whether a shared mapping can be made, since
 *  the cohort and its parent read the same words. Whether a wait on a word whose value has
 *  already changed comes back with the answer Linux gives in that case rather than hanging,
 *  which is what proves the call reached the kernel at all. Whether a thread that really
 *  waits is really woken by a wake from another thread -- the round trip the search is
 *  built on, not a call that merely returns zero. And whether the mapping goes back.
 *
 *  Two habits are in here because their absence cost a device run. The output is
 *  unbuffered, because a probe that hangs with its evidence still in a buffer is a probe
 *  that says nothing about why. And the waiting thread checks the word before it waits, in
 *  a loop, with the waker setting the word before it wakes: the first version used a spin
 *  as a delay, which is not a protocol -- when the wake arrived before the waiter reached
 *  the kernel there was nobody to wake, and the join that followed never returned.
 *
 *  Device only, like the landing it exercises: it calls the kernel.
 */
#include <cstdio>
#include <cstdint>

#include "backend/cve_2026_43499/leak/snitch_ops_linux.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::LinuxSnitchOps;
    using ghostlock::backend::cve_2026_43499::leak::MapResult;
    using ghostlock::backend::cve_2026_43499::memory::OpsResult;

    std::uint32_t *g_word = nullptr;
    volatile std::uint32_t *g_said = nullptr;
    std::size_t g_failed = 0U;

    void report(const char *what, long long value, int error, bool ok) {
        std::printf("  %-22s value=%lld errno=%d %s\n", what, value, error, ok ? "ok" : "FAILED");
        std::fflush(stdout);
        if (!ok) {
            ++g_failed;
        }
    }

    void *waiter(void *) {
        while (*g_word == 0U) {
            (void) LinuxSnitchOps::futex_wait_private(g_word, 0U);
        }
        *g_said = 1U;
        return nullptr;
    }
}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0U);
    std::printf("snitch landing probe\n");
    const MapResult mapped = LinuxSnitchOps::map_shared(4096U);
    std::printf("  %-22s address=%p errno=%d %s\n", "map_shared", mapped.address, mapped.error,
                mapped.address != nullptr ? "ok" : "FAILED");
    std::fflush(stdout);
    if (mapped.address == nullptr) {
        std::printf("snitch landing probe: FAIL\n");
        return 1;
    }
    std::uint32_t *const word = reinterpret_cast<std::uint32_t *>(mapped.address);
    g_word = word;
    g_said = reinterpret_cast<volatile std::uint32_t *>(static_cast<unsigned char *>(mapped.address) + 8U);

    *word = 7U;
    const OpsResult mismatched = LinuxSnitchOps::futex_wait_private(word, 0U);
    report("wait(mismatch)", static_cast<long long>(mismatched.value), mismatched.error,
           mismatched.value < 0 && mismatched.error == 11);

    *word = 0U;
    *g_said = 0U;
    const auto started = LinuxSnitchOps::start_thread(&waiter, nullptr);
    std::printf("  %-22s handle=%lu errno=%d %s\n", "start_thread", started.handle, started.error,
                started.error == 0 ? "ok" : "FAILED");
    std::fflush(stdout);
    if (started.error != 0) {
        ++g_failed;
    } else {
        *word = 1U;
        const OpsResult woke = LinuxSnitchOps::futex_wake_private(word, 1);
        report("wake(1)", static_cast<long long>(woke.value), woke.error, true);
        const OpsResult joined = LinuxSnitchOps::join_thread(started.handle);
        report("join_thread", static_cast<long long>(joined.value), joined.error, joined.value == 0);
        std::printf("  %-22s %s\n", "the waiter came back",
                    *g_said == 1U ? "ok" : "FAILED (it did not report being woken)");
        std::fflush(stdout);
        if (*g_said != 1U) {
            ++g_failed;
        }
    }

    const OpsResult gone = LinuxSnitchOps::unmap(mapped.address, 4096U);
    report("unmap", static_cast<long long>(gone.value), gone.error, gone.value == 0);
    std::printf("snitch landing probe: %s\n", g_failed == 0U ? "PASS" : "FAIL");
    return g_failed == 0U ? 0 : 1;
}
