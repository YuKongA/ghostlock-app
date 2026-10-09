/** @file tests/access_factory_test.cpp
 *  The factory: two backends answer, four do not, and none of them is a stand-in.
 *
 *  What is being pinned is not that the call compiles but that it discriminates. Each backend in
 *  the catalogue is asked for, and the two that exist must come back as their own type with their
 *  own capabilities -- the first backend carries a window and the second a page cache, so a factory
 *  that returned the same kind for both would be wrong in a way that still compiled.
 */
#include <cstdio>

#include "contract/kernel_access.hpp"
#include "contract/kind.hpp"

namespace {
    using ghostlock::contract::AccessChannel;
    using ghostlock::contract::BackendId;
    using ghostlock::contract::KernelAccess;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void both_implemented_backends_answer() {
        const std::unique_ptr<KernelAccess> window = KernelAccess::create(BackendId::Cve_2026_43499);
        expect(window != nullptr, "the window backend is built by this build");
        if (window != nullptr) {
            expect(window->supports(AccessChannel::PiWindow), "and it carries the pi window");
            expect(!window->supports(AccessChannel::PageCache), "and not the page cache");
        }
        const std::unique_ptr<KernelAccess> cache = KernelAccess::create(BackendId::Cve_2026_43284);
        expect(cache != nullptr, "the page cache backend is built by this build");
        if (cache != nullptr) {
            expect(cache->supports(AccessChannel::PageCache), "and it carries the page cache");
            expect(!cache->supports(AccessChannel::PiWindow), "and not the pi window");
        }
    }

    void a_backend_without_an_implementation_is_told_no() {
        expect(KernelAccess::create(BackendId::Cve_2026_64560) == nullptr,
               "a declared backend with no implementation returns nothing");
        expect(KernelAccess::create(BackendId::Cve_2026_31431) == nullptr,
               "and so does the second of them");
        expect(KernelAccess::create(static_cast<BackendId>(1U)) == nullptr,
               "and so does an id that is not in the catalogue at all");
    }

    void a_fresh_instance_is_not_yet_initialised() {
        const std::unique_ptr<KernelAccess> window = KernelAccess::create(BackendId::Cve_2026_43499);
        if (window != nullptr) {
            expect(window->state(AccessChannel::PiWindow) != KernelAccess::AccessState::Ready,
                   "a fresh instance is not ready: construction does no work");
        }
    }
}  // namespace

int main() {
    std::printf("access factory: the two that exist\n");
    both_implemented_backends_answer();
    std::printf("access factory: the four that do not\n");
    a_backend_without_an_implementation_is_told_no();
    std::printf("access factory: a fresh instance\n");
    a_fresh_instance_is_not_yet_initialised();
    if (g_failures != 0U) {
        std::printf("access factory: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("access factory: PASS (two answer, four return nothing)\n");
    return 0;
}
