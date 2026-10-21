/** @file tests/multicast_landing_test.cpp
 *  The landing's decisions, with a window that records instead of writing.
 *
 *  This is the half that can be checked before the route body is ported: which route the landing
 *  accepts and which it refuses, that bring_up keeps the facts the write will land on and that
 *  tear_down gives them back, that a write before bring_up does not reach the window, and that a
 *  landed word carries the address, the value and the offset unchanged. The window itself is
 *  faked here; a real one arrives with the port.
 */
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43499/landing/multicast_landing.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::WordLanding;
    using ghostlock::backend::cve_2026_43499::TargetState;
    using ghostlock::backend::cve_2026_43499::memory::WriteMode;
    using ghostlock::backend::cve_2026_43499::memory::WriteRequest;
    using ghostlock::backend::cve_2026_43499::landing::MulticastLanding;
    using ghostlock::backend::cve_2026_43499::landing::Window;
    using ghostlock::profile::RouteId;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    class RecordingWindow final : public Window {
    public:
        int writes = 0;
        std::uint64_t address = 0U;
        std::uint64_t value = 0U;
        std::uint64_t offset = 0U;
        WriteMode mode = WriteMode::Disabled;
        bool preserve = false;

        [[nodiscard]] bool write_word(std::uint64_t a, std::uint64_t v, std::uint64_t o) noexcept override {
            ++writes;
            address = a;
            value = v;
            offset = o;
            return true;
        }

        [[nodiscard]] bool write_word(std::uint64_t a, std::uint64_t v,
                                     const WriteRequest &request,
                                     std::uint64_t o) noexcept override {
            ++writes;
            address = a;
            value = v;
            offset = o;
            mode = request.mode;
            preserve = request.preserve_child;
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

    void only_multicast_is_served() {
        RecordingWindow window;
        MulticastLanding landing(window);
        const TargetState target = prepared();
        expect(!landing.bring_up(RouteId::TcpZerocopy, target),
               "a route this landing does not do is refused rather than quietly served");
        expect(!landing.up(), "and nothing came up");
        expect(!landing.bring_up(RouteId::Unset, target), "and so is an undecided route");
        expect(landing.bring_up(RouteId::MulticastWaiter, target), "its own route comes up");
        expect(landing.up() && landing.state() == MulticastLanding::AccessState::Ready, "and reports ready");
        expect(!landing.bring_up(RouteId::MulticastWaiter, target), "a second bring_up is refused");
        expect(landing.tear_down(), "and it goes down");
        expect(!landing.up() && landing.state() == MulticastLanding::AccessState::Declared,
               "back to declared, not unsupported: it has the route, it is not up");
        expect(!landing.tear_down(), "a second tear_down is refused rather than reported as success");
    }

    void a_word_arrives_unchanged() {
        RecordingWindow window;
        MulticastLanding landing(window);
        expect(!landing.land_word(0x2000U, 0x4142434445464748ULL, 0x40U),
               "a write before bring_up does not reach the window");
        expect(window.writes == 0, "and the window was not touched");
        expect(landing.bring_up(RouteId::MulticastWaiter, prepared()), "the route comes up");
        expect(landing.land_word(0x2000U, 0x4142434445464748ULL, 0x40U), "and a word lands");
        expect(window.writes == 1, "once");
        expect(window.address == 0x2000U && window.value == 0x4142434445464748ULL,
               "with the address and the value unchanged all the way down");
        expect(window.offset == 0x40U, "and the offset from the parameters");
    }

    void a_word_carries_its_shape() {
        RecordingWindow window;
        MulticastLanding landing(window);
        expect(landing.bring_up(RouteId::MulticastWaiter, prepared()), "the route comes up");
        const WriteRequest request{0x2000U, WriteMode::Zero, false};
        expect(landing.land_word(0x2000U, 1U, request, 0x40U), "a shaped word lands");
        expect(window.mode == WriteMode::Zero && !window.preserve,
               "and the landing carries the shape to the window");
    }

    void missing_facts_stop_the_write() {
        RecordingWindow window;
        MulticastLanding landing(window);
        TargetState empty;
        empty.mm_struct = 0xffff800000100000ULL;
        static_cast<void>(landing.bring_up(RouteId::MulticastWaiter, empty));
        expect(!landing.land_word(0x2000U, 1U, 0U),
               "with no page prepared the write is refused here, not left to the window");
        expect(window.writes == 0, "and the window never opened");
    }
}  // namespace

int main() {
    std::printf("multicast landing: which route is served\n");
    only_multicast_is_served();
    std::printf("multicast landing: a word arrives unchanged\n");
    a_word_arrives_unchanged();
    std::printf("multicast landing: a word carries its shape\n");
    a_word_carries_its_shape();
    std::printf("multicast landing: missing facts\n");
    missing_facts_stop_the_write();
    if (g_failures != 0U) {
        std::printf("multicast landing: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("multicast landing: PASS (one route, refusals by name, fields unchanged)\n");
    return 0;
}
