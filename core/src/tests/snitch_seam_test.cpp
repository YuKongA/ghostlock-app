/** @file tests/snitch_seam_test.cpp
 *  The shape of the search's edge, tested by what it refuses.
 *
 *  A seam that answers five of six calls is not a seam, and the useful thing to pin is
 *  exactly that: the concept accepts a type that answers everything and rejects one that
 *  leaves a call out or answers it with the wrong shape. The device implementation is
 *  checked the same way where it is written, which is why this test carries a fake
 *  rather than trying to call a kernel.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43499/leak/snitch_seam.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::MapResult;
    using ghostlock::backend::cve_2026_43499::leak::SnitchSeam;
    using ghostlock::backend::cve_2026_43499::leak::ThreadEntry;
    using ghostlock::backend::cve_2026_43499::leak::ThreadResult;
    using ghostlock::backend::cve_2026_43499::memory::SeamResult;

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
        static SeamResult unmap(void *, std::size_t) noexcept { return SeamResult{0, 0}; }
        static SeamResult futex_wait_private(std::uint32_t *, std::uint32_t) noexcept {
            return SeamResult{0, 0};
        }
        static SeamResult futex_wait_private_until(std::uint32_t *, std::uint32_t, std::int64_t) noexcept { return SeamResult{0, 0}; }
        static SeamResult futex_wake_private(std::uint32_t *, std::int32_t) noexcept {
            return SeamResult{0, 0};
        }
        static ThreadResult start_thread(ThreadEntry, void *) noexcept {
            return ThreadResult{1U, 0};
        }
        static SeamResult join_thread(unsigned long) noexcept { return SeamResult{0, 0}; }
    };
    static_assert(SnitchSeam<CompleteSeam>, "a type that answers every call is a seam");

    /** And the ones that are not: one call missing, and one answered in the wrong shape. */
    struct MissingJoin final {
        static MapResult map_shared(std::size_t) noexcept { return MapResult{nullptr, 0}; }
        static SeamResult unmap(void *, std::size_t) noexcept { return SeamResult{0, 0}; }
        static SeamResult futex_wait_private(std::uint32_t *, std::uint32_t) noexcept {
            return SeamResult{0, 0};
        }
        static SeamResult futex_wait_private_until(std::uint32_t *, std::uint32_t, std::int64_t) noexcept { return SeamResult{0, 0}; }
        static SeamResult futex_wake_private(std::uint32_t *, std::int32_t) noexcept {
            return SeamResult{0, 0};
        }
        static ThreadResult start_thread(ThreadEntry, void *) noexcept {
            return ThreadResult{1U, 0};
        }
    };
    static_assert(!SnitchSeam<MissingJoin>, "a seam with no join is not one");

    struct WrongMap final {
        static void *map_shared(std::size_t) noexcept { return nullptr; }
        static SeamResult unmap(void *, std::size_t) noexcept { return SeamResult{0, 0}; }
        static SeamResult futex_wait_private(std::uint32_t *, std::uint32_t) noexcept {
            return SeamResult{0, 0};
        }
        static SeamResult futex_wait_private_until(std::uint32_t *, std::uint32_t, std::int64_t) noexcept { return SeamResult{0, 0}; }
        static SeamResult futex_wake_private(std::uint32_t *, std::int32_t) noexcept {
            return SeamResult{0, 0};
        }
        static ThreadResult start_thread(ThreadEntry, void *) noexcept {
            return ThreadResult{1U, 0};
        }
        static SeamResult join_thread(unsigned long) noexcept { return SeamResult{0, 0}; }
    };
    static_assert(!SnitchSeam<WrongMap>,
                  "a mapping that cannot report why it failed is not the shape either");
}  // namespace

int main() {
    std::printf("snitch seam: the shape\n");
    expect(SnitchSeam<CompleteSeam>, "a complete seam passes the same check the device one does");
    expect(!SnitchSeam<MissingJoin>, "and one that leaves out a call does not");
    expect(!SnitchSeam<WrongMap>, "nor does one that answers a call in the wrong shape");
    if (g_failures != 0U) {
        std::printf("snitch seam: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("snitch seam: PASS (complete answers pass, partial ones do not)\n");
    return 0;
}
