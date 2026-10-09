/** @file tests/cve43499_kernel_access_test.cpp
 *  The 43499 access against a seam that records what it was asked to do.
 *
 *  The point of the test is the decisions this class makes rather than the write itself, which is
 *  the seam's job: construction does nothing, initialize brings exactly the route the parameters
 *  named, shutdown takes it down, and a second initialize while one is up is refused. It also
 *  pins the honesty rules: a width the window cannot write is refused by name, a write before
 *  initialize is refused by name, parameters from another backend are refused rather than
 *  reinterpreted, and a read says unsupported until the facts are in place rather than returning
 *  a zero that would look like data.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43499/kernel_access.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::Access;
    using ghostlock::backend::cve_2026_43499::AccessParams;
    using ghostlock::backend::cve_2026_43499::AccessSeam;
    using ghostlock::backend::cve_2026_43499::TargetState;
    using ghostlock::contract::AccessChannel;
    using ghostlock::contract::KernelAccess;
    using ghostlock::profile::RouteId;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    class RecordingSeam final : public AccessSeam {
    public:
        bool bring_up_ok = true;
        bool can_read_flag = false;
        int up_calls = 0;
        int down_calls = 0;
        int lands = 0;
        std::uint64_t last_address = 0U;
        std::uint64_t last_value = 0U;
        std::uint64_t last_offset = 0U;
        RouteId last_route = RouteId::Unset;

        [[nodiscard]] bool bring_up(RouteId route, const TargetState &) noexcept override {
            ++up_calls;
            last_route = route;
            return bring_up_ok;
        }
        [[nodiscard]] bool tear_down() noexcept override {
            ++down_calls;
            return true;
        }
        [[nodiscard]] bool land_word(std::uint64_t address, std::uint64_t value,
                                     std::uint64_t offset) noexcept override {
            ++lands;
            last_address = address;
            last_value = value;
            last_offset = offset;
            return true;
        }
        [[nodiscard]] bool can_read() const noexcept override { return can_read_flag; }
    };

    /** Parameters from somewhere else: the same interface, a different name. */
    class ForeignParams final : public KernelAccess::AccessMiscParameters {
    public:
        [[nodiscard]] std::string_view name() const noexcept override { return "another-backend"; }
    };

    TargetState ready_target() noexcept {
        TargetState target;
        target.mm_struct = 0xffff800000100000ULL;
        target.page_base = 0xffff800000200000ULL;
        target.fake_task = 0xffff800000300000ULL;
        target.fake_lock = 0xffff800000400000ULL;
        return target;
    }

    void construction_and_lifecycle() {
        RecordingSeam seam;
        const TargetState target = ready_target();
        Access access(&seam, &target);
        expect(seam.up_calls == 0 && access.route() == RouteId::Unset,
               "construction decides nothing and takes nothing down");
        expect(access.state(AccessChannel::PiWindow) == Access::AccessState::Declared,
               "so the channel is declared and not up");
        AccessParams params;
        expect(access.initialize(params).status == Access::AccessStatus::BadParameters,
               "initializing without a route is refused by name");
        params.route = RouteId::TcpZerocopy;
        params.offset = 0x40U;
        params.offset_set = true;
        expect(access.initialize(params).ok(), "with a route it comes up");
        expect(seam.last_route == RouteId::TcpZerocopy, "and it is the route the parameters named");
        expect(access.state(AccessChannel::PiWindow) == Access::AccessState::Ready, "and reports ready");
        expect(access.initialize(params).status == Access::AccessStatus::Failed,
               "a second initialize while one is up is refused");
        expect(access.shutdown().ok(), "and it shuts down");
        expect(seam.down_calls == 1, "taking the route down exactly once");
        expect(access.state(AccessChannel::PiWindow) == Access::AccessState::Closed,
               "after which the state is closed, not declared: it was up and it was taken down");
    }

    void writes_go_through_the_seam() {
        RecordingSeam seam;
        const TargetState target = ready_target();
        Access access(&seam, &target);
        AccessParams params;
        params.route = RouteId::MulticastWaiter;
        params.offset = 0x80U;
        params.offset_set = true;
        const std::byte word[8] = {std::byte{0x48}, std::byte{0x47}, std::byte{0x46}, std::byte{0x45},
                                   std::byte{0x44}, std::byte{0x43}, std::byte{0x42}, std::byte{0x41}};
        expect(access.write(0x1000U, std::span<const std::byte>(word), AccessChannel::PiWindow, params).status
                   == Access::AccessStatus::NotInitialized,
               "a write before initialize is refused by name");
        static_cast<void>(access.initialize(params));
        const auto written = access.write(0x2000U, std::span<const std::byte>(word),
                                          AccessChannel::PiWindow, params);
        expect(written.ok() && written.value == 8U, "a word writes and says how many bytes");
        expect(seam.lands == 1 && seam.last_value == 0x4142434445464748ULL,
               "landing the value little-endian through the seam");
        expect(seam.last_offset == 0x80U, "with the offset from the parameters");
        const std::byte four[4] = {};
        expect(access.write(0x2000U, std::span<const std::byte>(four), AccessChannel::PiWindow, params).status
                   == Access::AccessStatus::BadWidth,
               "a narrower width is refused rather than written anyway");
        expect(seam.lands == 1, "and nothing was landed for it");
        const ForeignParams foreign;
        expect(access.write(0x2000U, std::span<const std::byte>(word), AccessChannel::PiWindow, foreign).status
                   == Access::AccessStatus::BadParameters,
               "parameters from another backend are refused, not reinterpreted");
        expect(access.read(0x2000U, 8U, AccessChannel::PiWindow, params).status
                   == Access::AccessStatus::Unsupported,
               "and a read says unsupported until a route can read, rather than returning zero");
    }
}  // namespace

int main() {
    std::printf("cve-2026-43499 access: lifecycle\n");
    construction_and_lifecycle();
    std::printf("cve-2026-43499 access: writes and refusals\n");
    writes_go_through_the_seam();
    if (g_failures != 0U) {
        std::printf("cve-2026-43499 access: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("cve-2026-43499 access: PASS (route from parameters, window per access)\n");
    return 0;
}
