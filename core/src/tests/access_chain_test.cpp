/** @file tests/access_chain_test.cpp
 *  The three pieces wired into one: access, seam, window, controller.
 *
 *  Each was tested alone and each test said the same thing about itself -- that it pins the
 *  decisions and not the write. This one drives the whole chain with a controller that records, so
 *  what is checked is that the pieces agree: the access takes its parameters, asks the seam for its
 *  route, the seam lands a word on the window, the window spends its warm-up rounds and then hands
 *  the address, the value and the offset to the controller, and the byte count the access reports
 *  is the width it declared rather than a number it made up.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43499/kernel_access.hpp"
#include "backend/cve_2026_43499/seam/multicast_seam.hpp"
#include "backend/cve_2026_43499/seam/multicast_window.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::Access;
    using ghostlock::backend::cve_2026_43499::AccessParams;
    using ghostlock::backend::cve_2026_43499::TargetState;
    using ghostlock::backend::cve_2026_43499::route::MulticastLimits;
    using ghostlock::backend::cve_2026_43499::seam::MulticastSeam;
    using ghostlock::backend::cve_2026_43499::seam::MulticastWindow;
    using ghostlock::backend::cve_2026_43499::seam::RouteExec;
    using ghostlock::contract::AccessChannel;
    using ghostlock::profile::RouteId;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    class RecordingExec final : public RouteExec {
    public:
        int arms = 0;
        int executes = 0;
        std::uint64_t address = 0U;
        std::uint64_t value = 0U;
        std::uint64_t offset = 0U;

        [[nodiscard]] bool execute(std::uint64_t a, std::uint64_t v, std::uint64_t o) noexcept override {
            ++executes;
            address = a;
            value = v;
            offset = o;
            return true;
        }
        [[nodiscard]] bool arm(std::int32_t) noexcept override {
            ++arms;
            return true;
        }
    };

    TargetState prepared() noexcept {
        TargetState target;
        target.mm_struct = 0xffff800000100000ULL;
        target.page_base = 0xffff800000200000ULL;
        target.fake_task = 0xffff800000300000ULL;
        target.fake_lock = 0xffff800000400000ULL;
        return target;
    }

    void the_chain_carries_one_write_end_to_end() {
        RecordingExec exec;
        MulticastLimits limits;
        limits.max_attempts = 4;
        limits.arm_sequence = 2;
        MulticastWindow window(exec, limits);
        MulticastSeam seam(window);
        const TargetState target = prepared();
        Access access(seam, target);
        AccessParams params;
        params.route = RouteId::MulticastWaiter;
        params.offset = 0x40U;
        params.offset_set = true;
        expect(access.initialize(params).ok(), "the access comes up through the seam");
        expect(seam.up(), "and the seam brought the route up");
        const std::byte word[8] = {std::byte{0x48}, std::byte{0x47}, std::byte{0x46}, std::byte{0x45},
                                   std::byte{0x44}, std::byte{0x43}, std::byte{0x42}, std::byte{0x41}};
        const auto written = access.write(0x2000U, std::span<const std::byte>(word),
                                          AccessChannel::PiWindow, params);
        expect(written.ok() && written.value == 8U, "a word writes and reports its own width");
        expect(exec.arms == 2, "the window spent exactly the warm-up rounds the caller gave");
        expect(exec.executes == 1, "and made exactly one attempt");
        expect(exec.address == 0x2000U && exec.value == 0x4142434445464748ULL,
               "with the address and the little-endian value reaching the controller");
        expect(exec.offset == 0x40U, "and the offset from the access parameters");
        expect(access.shutdown().ok() && !seam.up(), "and it all comes back down");
    }

    void a_chain_without_warm_up_refuses_before_the_controller() {
        RecordingExec exec;
        const MulticastLimits unsaid;
        MulticastWindow window(exec, unsaid);
        MulticastSeam seam(window);
        Access access(seam, prepared());
        AccessParams params;
        params.route = RouteId::MulticastWaiter;
        params.offset_set = true;
        static_cast<void>(access.initialize(params));
        const std::byte word[8] = {};
        const auto refused = access.write(0x2000U, std::span<const std::byte>(word),
                                           AccessChannel::PiWindow, params);
        expect(!refused.ok(), "a run whose warm-up count nobody gave fails");
        expect(exec.executes == 0, "and the controller is never reached");
    }
}  // namespace

int main() {
    std::printf("access chain: one write, end to end\n");
    the_chain_carries_one_write_end_to_end();
    std::printf("access chain: the refusal stops at the seam\n");
    a_chain_without_warm_up_refuses_before_the_controller();
    if (g_failures != 0U) {
        std::printf("access chain: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("access chain: PASS (access, seam, window, controller agree)\n");
    return 0;
}
