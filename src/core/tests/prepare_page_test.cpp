/** @file tests/prepare_page_test.cpp
 *  The orchestration, driven by fakes: the happy path and the ways it stops.
 *
 *  An orchestration is most likely to be wrong exactly where it is least likely to
 *  be exercised, so the failure paths get as much attention here as the success: a
 *  search that finds nothing must give every descriptor back, and a call that fails
 *  midway must not leave a child behind. The fake seam records what it was asked to
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
    using ghostlock::backend::cve_2026_43499::memory::SeamResult;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** A fake platform: hands out descriptors and children, and remembers the calls. */
    struct FakeSeam final {
        static std::int64_t next_fd;
        static std::int64_t next_pid;
        static std::size_t fork_calls;
        static std::size_t close_calls;
        static std::int32_t closed_order[64];
        static std::int32_t last_memfd;
        static std::size_t kill_calls;
        static std::size_t send_calls;
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
            failing_fork_after = -1;
            reported_page = 4096U;
        }
        static SeamResult page_size() noexcept { return SeamResult{static_cast<std::int64_t>(reported_page), 0}; }
        static SeamResult open_memfd(std::uint64_t) noexcept {
            last_memfd = static_cast<std::int32_t>(next_fd);
            return SeamResult{next_fd++, 0};
        }
        static SeamResult close_fd(std::int32_t fd) noexcept {
            if (close_calls < 64U) { closed_order[close_calls] = fd; }
            ++close_calls;
            return SeamResult{0, 0};
        }
        static SeamResult socket_pair(std::int32_t *out) noexcept {
            if (out != nullptr) { out[0] = static_cast<std::int32_t>(next_fd++); out[1] = static_cast<std::int32_t>(next_fd++); }
            return SeamResult{0, 0};
        }
        static SeamResult set_socket_buffer(std::int32_t, std::int32_t) noexcept { return SeamResult{0, 0}; }
        static SeamResult set_nonblocking(std::int32_t) noexcept { return SeamResult{0, 0}; }
        static SeamResult send_buffer(std::int32_t, std::size_t) noexcept { ++send_calls; return SeamResult{0, 0}; }
        static SeamResult fork_child() noexcept {
            ++fork_calls;
            if (failing_fork_after >= 0 && static_cast<std::int64_t>(fork_calls) > failing_fork_after) {
                return SeamResult{-1, 11};
            }
            return SeamResult{next_pid++, 0};
        }
        static SeamResult stop_child(std::uint64_t) noexcept { ++kill_calls; return SeamResult{0, 0}; }
        static SeamResult yield_now() noexcept { return SeamResult{0, 0}; }
        static SeamResult pin_to_processor(std::int32_t) noexcept { return SeamResult{0, 0}; }
    };
    std::int64_t FakeSeam::next_fd = 10;
    std::int64_t FakeSeam::next_pid = 1000;
    std::size_t FakeSeam::fork_calls = 0U;
    std::size_t FakeSeam::close_calls = 0U;
    std::int32_t FakeSeam::closed_order[64] = {};
    std::int32_t FakeSeam::last_memfd = -1;
    std::size_t FakeSeam::kill_calls = 0U;
    std::size_t FakeSeam::send_calls = 0U;
    std::int64_t FakeSeam::failing_fork_after = -1;
    std::uint64_t FakeSeam::reported_page = 4096U;

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
        static bool start(std::size_t) noexcept { started = true; return true; }
        static bool has_collision() noexcept { ++asked; return asked > rounds_until_found; }
        static std::uint64_t address() noexcept { return found_address; }
        static void reset() noexcept { ++resets; }
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
        FakeSeam::reset();
        FakeSnitch::reset_state();
        const auto outcome = prepare_page<FakeSeam, FakeSnitch>(inputs(), geometry(), patience());
        expect(outcome.status == PrepareStatus::Ok, "a preparation that finds an address is ok");
        expect(outcome.base == 0x1f0000U, "and its base is the aligned address");
        expect(outcome.base % geometry().align_region_size == 0U,
               "which is the start of a region, not somewhere inside one");
        /* The mechanism keeps some of what it opened on purpose: the reclaim
         *  walks the spray one descriptor per region. What matters is the last close. */
        expect(FakeSeam::close_calls > 0U, "descriptors were given back");
        /* The rule the reclaim exists for: the descriptor opened last -- the leak child own -- is closed last of all. */
        expect(FakeSeam::close_calls > 0U &&
                   FakeSeam::closed_order[FakeSeam::close_calls - 1U] == FakeSeam::last_memfd,
               "the leak descriptor is closed last of all");
        expect(FakeSeam::kill_calls > 0U, "and the children were stopped");
        expect(FakeSeam::send_calls == 1U, "and the shaping send happened once");
    }

    void a_search_that_finds_nothing() {
        FakeSeam::reset();
        FakeSnitch::reset_state();
        FakeSnitch::rounds_until_found = 1000000U;
        const auto outcome = prepare_page<FakeSeam, FakeSnitch>(inputs(), geometry(), patience());
        expect(outcome.status == PrepareStatus::NoCollision,
               "a search that finds nothing stops as no collision");
        expect(outcome.at_step == ghostlock::backend::cve_2026_43499::memory::PrepareStep::LeakLoop,
               "and says it stopped at the wait");
        expect(FakeSnitch::resets == 1U, "the search is reset rather than left running");
        expect(FakeSeam::close_calls > 0U, "and the descriptors are given back even so");
    }

    void a_call_that_fails_midway() {
        FakeSeam::reset();
        FakeSnitch::reset_state();
        FakeSeam::failing_fork_after = 3;
        const auto outcome = prepare_page<FakeSeam, FakeSnitch>(inputs(), geometry(), patience());
        expect(outcome.status == PrepareStatus::CallFailed, "a fork that fails stops the preparation");
        expect(outcome.error == 11, "and the errno it reported comes back with it");
        expect(FakeSeam::kill_calls >= 3U, "the children already forked are stopped");
        expect(outcome.closed == 0U, "and no descriptor was closed twice");
    }

    void geometry_that_is_not_this_device() {
        FakeSeam::reset();
        FakeSnitch::reset_state();
        FakeSeam::reported_page = 8192U;
        const auto outcome = prepare_page<FakeSeam, FakeSnitch>(inputs(), geometry(), patience());
        expect(outcome.status == PrepareStatus::Refused,
               "a region built for another page size is refused before anything is forked");
        expect(FakeSeam::fork_calls == 0U, "with nothing forked");
        expect(std::string_view(prepare_status_name(outcome.status)) == std::string_view("geometry-missing"),
               "under the geometry name");
    }
}  // namespace

int main() {
    std::printf("prepare page: the happy path\n");
    a_preparation_that_finds_something();
    std::printf("prepare page: the failure paths\n");
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
