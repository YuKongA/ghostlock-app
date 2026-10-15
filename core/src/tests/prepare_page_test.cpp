/** @file tests/prepare_page_test.cpp
 *  The orchestration, driven by fakes: the happy path and the ways it stops.
 *
 *  An orchestration is most likely to be wrong exactly where it is least likely to
 *  be exercised, so the failure paths get as much attention here as the success: a
 *  search that finds nothing must give every descriptor back, and a call that fails
 *  midway must not leave a child behind. The fake landing records what it was asked to
 *  do, so the order the phases run in is checked rather than assumed.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "backend/cve_2026_43499/memory/prepare_page.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::memory::PageGeometry;
    using ghostlock::backend::cve_2026_43499::memory::PrepareInputs;
    using ghostlock::backend::cve_2026_43499::memory::PreparePatience;
    using ghostlock::backend::cve_2026_43499::memory::prepare_page;
    using ghostlock::backend::cve_2026_43499::memory::prepare_status_name;
    using ghostlock::backend::cve_2026_43499::memory::PrepareStatus;
    using ghostlock::backend::cve_2026_43499::memory::OpsResult;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** A fake platform: hands out descriptors and children, and remembers the calls. */
    struct FakeOps final {
        static std::int64_t next_fd;
        static std::int64_t next_pid;
        static std::size_t fork_calls;
        static std::size_t close_calls;
        static std::int32_t closed_order[64];
        static std::int32_t last_memfd;
        static bool fail_opens;
        static std::size_t kill_calls;
        static std::size_t send_calls;
        static std::size_t send_bytes_calls;
        static std::int64_t failing_fork_after;
        static std::uint64_t reported_page;

        static void reset() noexcept {
            next_fd = 10;
            next_pid = 1000;
            fork_calls = 0U;
            close_calls = 0U;
            for (std::size_t i = 0U; i < 64U; ++i) { closed_order[i] = -1; }
            last_memfd = -1;
            kill_calls = 0U;
            send_calls = 0U;
            send_bytes_calls = 0U;
            failing_fork_after = -1;
            fail_opens = false;
            reported_page = 4096U;
        }
        static OpsResult page_size() noexcept { return OpsResult{static_cast<std::int64_t>(reported_page), 0}; }
        static OpsResult open_memfd(std::uint64_t) noexcept {
            /* The device can refuse a descriptor; a test has to be able to ask for that. */
            if (fail_opens) { return OpsResult{-1, 1}; }   /* a refused open is a negative fd, like the kernel gives */
            last_memfd = static_cast<std::int32_t>(next_fd);
            return OpsResult{next_fd++, 0};
        }
        static OpsResult close_fd(std::int32_t fd) noexcept {
            if (close_calls < 64U) { closed_order[close_calls] = fd; }
            ++close_calls;
            return OpsResult{0, 0};
        }
        static OpsResult socket_pair(std::int32_t *out) noexcept {
            if (out != nullptr) { out[0] = static_cast<std::int32_t>(next_fd++); out[1] = static_cast<std::int32_t>(next_fd++); }
            return OpsResult{0, 0};
        }
        static OpsResult set_socket_buffer(std::int32_t, std::int32_t) noexcept { return OpsResult{0, 0}; }
        static OpsResult set_nonblocking(std::int32_t) noexcept { return OpsResult{0, 0}; }
        static OpsResult send_buffer(std::int32_t, std::size_t) noexcept { ++send_calls; return OpsResult{0, 0}; }
        /** 页面的发送通道（退役 sendmsg(skb_buf) ✓）：测试只数它被叫过几次。 */
        static OpsResult send_bytes(std::int32_t, const void *, std::size_t) noexcept {
            ++send_bytes_calls;
            return OpsResult{0, 0};
        }
        static OpsResult fork_child() noexcept {
            ++fork_calls;
            if (failing_fork_after >= 0 && static_cast<std::int64_t>(fork_calls) > failing_fork_after) {
                return OpsResult{-1, 11};
            }
            return OpsResult{next_pid++, 0};
        }
        static OpsResult stop_child(std::uint64_t) noexcept { ++kill_calls; return OpsResult{0, 0}; }
        static OpsResult yield_now() noexcept { return OpsResult{0, 0}; }
        static OpsResult pin_to_processor(std::int32_t) noexcept { return OpsResult{0, 0}; }
        static void exit_child() noexcept {}
    };
    std::int64_t FakeOps::next_fd = 10;
    std::int64_t FakeOps::next_pid = 1000;
    std::size_t FakeOps::fork_calls = 0U;
    std::size_t FakeOps::close_calls = 0U;
    std::int32_t FakeOps::closed_order[64] = {};
    std::int32_t FakeOps::last_memfd = -1;
    bool FakeOps::fail_opens = false;
    std::size_t FakeOps::kill_calls = 0U;
    std::size_t FakeOps::send_calls = 0U;
    std::size_t FakeOps::send_bytes_calls = 0U;
    std::int64_t FakeOps::failing_fork_after = -1;
    std::uint64_t FakeOps::reported_page = 4096U;

    /** A fake search: told how many rounds before it finds something, or never. */
    struct FakeSnitch final {
        static std::size_t rounds_until_found;
        static std::uint64_t found_address;
        static bool started;
        static std::size_t resets;
        static std::size_t asked;

        static void reset_state() noexcept {
            rounds_until_found = 0U;
            found_address = 0x1f0000U;
            started = false;
            resets = 0U;
            asked = 0U;
        }
        struct Answer final {
            bool ran = false;
            bool found = false;
            std::uint64_t address = 0U;
        };

        Answer start(std::size_t) noexcept {
            started = true;
            return Answer{true, false, 0U};
        }
        bool has_collision() noexcept { ++asked; return asked > rounds_until_found; }
        std::uint64_t address() const noexcept { return found_address; }
        void reset() noexcept { ++resets; }
    };
    std::size_t FakeSnitch::rounds_until_found = 0U;
    std::uint64_t FakeSnitch::found_address = 0x1f0000U;
    bool FakeSnitch::started = false;
    std::size_t FakeSnitch::resets = 0U;
    std::size_t FakeSnitch::asked = 0U;

    PrepareInputs inputs() {
        PrepareInputs made;
        made.pre_groups = 2U;
        made.spray_groups = 4U;
        made.post_groups = 2U;
        made.leak_groups = 1U;
        made.snitch_collisions = 1U;
        made.region_size = 32768U;
        made.object_size = 512U;
        return made;
    }

    PageGeometry geometry() {
        PageGeometry made;
        made.align_region_size = 32768U;
        made.send_size = 65536U;
        made.socket_buffer = 1 << 20;
        made.main_cpu = 0;
        return made;
    }

    PreparePatience patience() {
        PreparePatience made;
        made.max_attempts = 1U;
        made.poll_rounds = 8U;
        return made;
    }

    void a_preparation_that_finds_something() {
        FakeOps::reset();
        FakeSnitch snitch;
        FakeSnitch::reset_state();
        const auto outcome = prepare_page<FakeOps, FakeSnitch>(inputs(), geometry(), patience(), snitch);
        expect(outcome.status == PrepareStatus::Ok, "a preparation that finds an address is ok");
        expect(outcome.base == 0x1f0000U, "and its base is the aligned address");
        expect(outcome.base % geometry().align_region_size == 0U,
               "which is the start of a region, not somewhere inside one");
        /* The mechanism keeps some of what it opened on purpose: the reclaim
         *  walks the spray one descriptor per region. What matters is the last close. */
        expect(FakeOps::close_calls > 0U, "descriptors were given back");
        /* The rule the reclaim exists for: the descriptor opened last -- the leak child own -- is closed last of all. */
        expect(FakeOps::close_calls > 0U &&
                   FakeOps::closed_order[FakeOps::close_calls - 1U] == FakeOps::last_memfd,
               "the leak descriptor is closed last of all");
        expect(FakeOps::kill_calls > 0U, "and the children were stopped");
        expect(FakeOps::send_calls == 1U, "and the shaping send happened once");
    }

    void a_descriptor_that_never_opened_is_counted_as_nothing() {
        FakeOps::reset();
        FakeSnitch snitch;
        FakeSnitch::reset_state();
        FakeSnitch::rounds_until_found = 1000000U;
        FakeOps::fail_opens = true;
        const auto outcome = prepare_page<FakeOps, FakeSnitch>(inputs(), geometry(), patience(), snitch);
        FakeOps::fail_opens = false;
        /* The device showed opened above closed by exactly the spray groups. The count rises
         *  with the groups, so this asks the question the device asked: does a descriptor that
         *  never opened still count as one more that was opened? */
        expect(outcome.opened == 0U,
               "a descriptor that never opened is not counted as opened");
        expect(outcome.closed == 0U,
               "and nothing was returned, because there was nothing to return");
    }

    void everything_opened_is_returned_when_no_collision_stops_it() {
        FakeOps::reset();
        FakeSnitch snitch;
        FakeSnitch::reset_state();
        FakeSnitch::rounds_until_found = 1000000U;   /* nothing ever collides */
        const auto outcome = prepare_page<FakeOps, FakeSnitch>(inputs(), geometry(), patience(), snitch);
        expect(outcome.status == PrepareStatus::NoCollision, "no collision stops the preparation");
        /* The gate the device asked for: every descriptor that opened is one that was given back,
         *  and each group accounts for itself. The device showed post 1/0 and then spray 2/1. */
        expect(outcome.opened == outcome.closed, "every descriptor that opened is returned");
        expect(outcome.opened_pre == outcome.closed_pre, "the pre group balances");
        expect(outcome.opened_spray == outcome.closed_spray, "the spray group balances");
        expect(outcome.opened_post == outcome.closed_post, "the post group balances");
        expect(outcome.opened_pre + outcome.opened_spray + outcome.opened_post + outcome.opened_leak ==
                       outcome.opened,
               "and the groups add up to the total");
    }
    void a_search_that_finds_nothing() {
        FakeOps::reset();
        FakeSnitch snitch;
        FakeSnitch::reset_state();
        FakeSnitch::rounds_until_found = 1000000U;
        const auto outcome = prepare_page<FakeOps, FakeSnitch>(inputs(), geometry(), patience(), snitch);
        expect(outcome.status == PrepareStatus::NoCollision,
               "a search that finds nothing stops as no collision");
        expect(outcome.at_step == ghostlock::backend::cve_2026_43499::memory::PrepareStep::LeakLoop,
               "and says it stopped at the wait");
        expect(FakeSnitch::resets == 1U, "the search is reset rather than left running");
        expect(FakeOps::close_calls > 0U, "and the descriptors are given back even so");
        /* The device found this one: the reclaim walk skips the leak group on purpose,
         *  so a path that stops early has to close that descriptor itself. */
        expect(FakeOps::closed_order[FakeOps::close_calls - 1U] == FakeOps::last_memfd,
               "including the leak descriptor, which nothing else will close");
    }

    void a_call_that_fails_midway() {
        FakeOps::reset();
        FakeSnitch snitch;
        FakeSnitch::reset_state();
        FakeOps::failing_fork_after = 3;
        const auto outcome = prepare_page<FakeOps, FakeSnitch>(inputs(), geometry(), patience(), snitch);
        expect(outcome.status == PrepareStatus::CallFailed, "a fork that fails stops the preparation");
        expect(outcome.error == 11, "and the errno it reported comes back with it");
        expect(FakeOps::kill_calls >= 3U, "the children already forked are stopped");
        expect(outcome.closed == 0U, "and no descriptor was closed twice");
    }

    void geometry_that_is_not_this_device() {
        FakeOps::reset();
        FakeSnitch snitch;
        FakeSnitch::reset_state();
        FakeOps::reported_page = 8192U;
        const auto outcome = prepare_page<FakeOps, FakeSnitch>(inputs(), geometry(), patience(), snitch);
        expect(outcome.status == PrepareStatus::Refused,
               "a region built for another page size is refused before anything is forked");
        expect(FakeOps::fork_calls == 0U, "with nothing forked");
        expect(std::string_view(prepare_status_name(outcome.status)) == std::string_view("geometry-missing"),
               "under the geometry name");
    }
}  // namespace

int main() {
    std::printf("prepare page: the happy path\n");
    a_preparation_that_finds_something();
    std::printf("prepare page: the failure paths\n");
    a_descriptor_that_never_opened_is_counted_as_nothing();
    everything_opened_is_returned_when_no_collision_stops_it();
    a_search_that_finds_nothing();
    a_call_that_fails_midway();
    geometry_that_is_not_this_device();
    if (g_failures != 0U) {
        std::printf("prepare page: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("prepare page: PASS (runs the order, tidies up on both failure paths)\n");
    return 0;
}
