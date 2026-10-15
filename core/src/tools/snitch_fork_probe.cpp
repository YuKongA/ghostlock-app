/** @file tools/snitch_fork_probe.cpp
 *  Does a forked child run, and do its writes land where its parent reads?
 *
 *  Written because the whole-search run on a device reported a cohort that never poked,
 *  and the first guess -- that the parent handed out its rounds too fast for the children
 *  to be scheduled -- did not change the answer. So this asks the smaller question
 *  directly: one shared word, one child writing it a few times with a print each time, and
 *  a parent that prints what it sees. Whatever the answer is, it is the answer to where the
 *  pokes went.
 *
 *  Unbuffered, both sides, because a child that hangs must still show what it was doing.
 */
#include <cstdio>
#include <cstdint>
#include <sys/wait.h>

#include "backend/cve_2026_43499/leak/snitch_ops_linux.hpp"
#include "backend/cve_2026_43499/memory/heap_ops_linux.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::LinuxSnitchOps;
    using ghostlock::backend::cve_2026_43499::leak::MapResult;
    using ghostlock::backend::cve_2026_43499::memory::LinuxHeapOps;
    using ghostlock::backend::cve_2026_43499::memory::OpsResult;
}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0U);
    std::printf("snitch fork probe: pid=%d\n", static_cast<int>(::getpid()));
    const MapResult mapped = LinuxSnitchOps::map_shared(4096U);
    if (mapped.address == nullptr) {
        std::printf("  map FAILED errno=%d\n", mapped.error);
        return 1;
    }
    auto *counter = static_cast<volatile std::uint32_t *>(mapped.address);
    std::uint32_t *word = reinterpret_cast<std::uint32_t *>(static_cast<unsigned char *>(mapped.address) + 64U);
    *counter = 0U;
    *word = 0U;

    const OpsResult child = LinuxHeapOps::fork_child();
    if (child.value == 0) {
        std::printf("  child: running, pid=%d\n", static_cast<int>(::getpid()));
        for (std::uint32_t i = 0U; i < 5U; ++i) {
            *counter += 1U;
            *word += 1U;
            (void) LinuxSnitchOps::futex_wake_private(word, 1);
            std::printf("  child: poke %u, counter=%u\n", i, *counter);
        }
        std::printf("  child: leaving\n");
        LinuxHeapOps::exit_child();
    }
    std::printf("  parent: forked pid=%lld\n", static_cast<long long>(child.value));
    /* Give the child room, then look. Bounded so the probe cannot hang. */
    for (int i = 0; i < 200; ++i) {
        (void) LinuxHeapOps::yield_now();
    }
    int status = 0;
    const pid_t reaped = ::waitpid(static_cast<pid_t>(child.value), &status, 0);
    std::printf("  parent: waitpid=%d status=%d, counter=%u word=%u\n", static_cast<int>(reaped), status,
                *counter, *word);
    const bool ok = *counter == 5U && *word == 5U;
    std::printf("snitch fork probe: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
