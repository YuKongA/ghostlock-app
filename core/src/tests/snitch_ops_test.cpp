/** @file tests/snitch_ops_test.cpp
 *  The shape of the search's edge, tested by what it refuses.
 *
 *  A landing that answers five of six calls is not a landing, and the useful thing to pin is
 *  exactly that: the concept accepts a type that answers everything and rejects one that
 *  leaves a call out or answers it with the wrong shape. The device implementation is
 *  checked the same way where it is written, which is why this test carries a fake
 *  rather than trying to call a kernel.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43499/leak/snitch_ops.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::MapResult;
    using ghostlock::backend::cve_2026_43499::leak::SnitchOps;
    using ghostlock::backend::cve_2026_43499::leak::ThreadEntry;
    using ghostlock::backend::cve_2026_43499::leak::ThreadResult;
    using ghostlock::backend::cve_2026_43499::memory::OpsResult;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** A fake that answers everything, in the smallest way that first compiles. */
    struct CompleteSeam final {
        static MapResult map_shared(std::size_t) noexcept { return MapResult{nullptr, 0}; }
        static OpsResult unmap(void *, std::size_t) noexcept { return OpsResult{0, 0}; }
        static OpsResult futex_wait_private(std::uint32_t *, std::uint32_t) noexcept {
            return OpsResult{0, 0};
        }
        static OpsResult futex_wait_private_until(std::uint32_t *, std::uint32_t, std::int64_t) noexcept { return OpsResult{0, 0}; }
        static OpsResult futex_wake_private(std::uint32_t *, std::int32_t) noexcept {
            return OpsResult{0, 0};
        }
        static ThreadResult start_thread(ThreadEntry, void *) noexcept {
            return ThreadResult{1U, 0};
        }
        static OpsResult join_thread(unsigned long) noexcept { return OpsResult{0, 0}; }
    };
    static_assert(SnitchOps<CompleteSeam>, "a type that answers every call is a landing");

    /** And the ones that are not: one call missing, and one answered in the wrong shape. */
    struct MissingJoin final {
        static MapResult map_shared(std::size_t) noexcept { return MapResult{nullptr, 0}; }
        static OpsResult unmap(void *, std::size_t) noexcept { return OpsResult{0, 0}; }
        static OpsResult futex_wait_private(std::uint32_t *, std::uint32_t) noexcept {
            return OpsResult{0, 0};
        }
        static OpsResult futex_wait_private_until(std::uint32_t *, std::uint32_t, std::int64_t) noexcept { return OpsResult{0, 0}; }
        static OpsResult futex_wake_private(std::uint32_t *, std::int32_t) noexcept {
            return OpsResult{0, 0};
        }
        static ThreadResult start_thread(ThreadEntry, void *) noexcept {
            return ThreadResult{1U, 0};
        }
    };
    static_assert(!SnitchOps<MissingJoin>, "a landing with no join is not one");

    struct WrongMap final {
        static void *map_shared(std::size_t) noexcept { return nullptr; }
        static OpsResult unmap(void *, std::size_t) noexcept { return OpsResult{0, 0}; }
        static OpsResult futex_wait_private(std::uint32_t *, std::uint32_t) noexcept {
            return OpsResult{0, 0};
        }
        static OpsResult futex_wait_private_until(std::uint32_t *, std::uint32_t, std::int64_t) noexcept { return OpsResult{0, 0}; }
        static OpsResult futex_wake_private(std::uint32_t *, std::int32_t) noexcept {
            return OpsResult{0, 0};
        }
        static ThreadResult start_thread(ThreadEntry, void *) noexcept {
            return ThreadResult{1U, 0};
        }
        static OpsResult join_thread(unsigned long) noexcept { return OpsResult{0, 0}; }
    };
    static_assert(!SnitchOps<WrongMap>,
                  "a mapping that cannot report why it failed is not the shape either");
}  // namespace

int main() {
    std::printf("snitch landing: the shape\n");
    expect(SnitchOps<CompleteSeam>, "a complete landing passes the same check the device one does");
    expect(!SnitchOps<MissingJoin>, "and one that leaves out a call does not");
    expect(!SnitchOps<WrongMap>, "nor does one that answers a call in the wrong shape");
    if (g_failures != 0U) {
        std::printf("snitch landing: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("snitch landing: PASS (complete answers pass, partial ones do not)\n");
    return 0;
}
