/** @file tools/prepare_page_probe.cpp
 *  The orchestration on the device, with an honest stub where a mechanism is missing.
 *
 *  Two things are not real here, and both are named rather than faked. The address
 *  search is not ported yet, so the probe supplies a search that reports no collision
 *  -- which is why this run exercises the failure path rather than the success: on a
 *  device, the preparation must fork its children, wait, find nothing, stop every
 *  child it made and give every descriptor back. The second is the child side of a
 *  fork: this probe has no work for a child to do, so its seam exits in the child at
 *  once rather than letting one wander back into the probe.
 *
 *  What is real is everything else: the addresses are the device's, the descriptors
 *  are the kernel's, and the accounting printed at the end is what actually happened.
 */
#include <cstdio>
#include <cstdint>
#include <cstdlib>

#include "backend/cve_2026_43499/memory/heap_seam_linux.hpp"
#include "backend/cve_2026_43499/memory/prepare_page.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::memory::HeapSeam;
    using ghostlock::backend::cve_2026_43499::memory::PageGeometry;
    using ghostlock::backend::cve_2026_43499::memory::PrepareInputs;
    using ghostlock::backend::cve_2026_43499::memory::PreparePatience;
    using ghostlock::backend::cve_2026_43499::memory::prepare_page;
    using ghostlock::backend::cve_2026_43499::memory::prepare_status_name;
    using ghostlock::backend::cve_2026_43499::memory::region_for;
    using ghostlock::backend::cve_2026_43499::memory::SeamResult;
    using ghostlock::backend::cve_2026_43499::memory::send_size_for;
    using Platforms = ghostlock::backend::cve_2026_43499::memory::LinuxHeapSeam;

    /** The device seam, with a child that leaves at once instead of running on. */
    struct ProbeSeam final {
        static SeamResult page_size() noexcept { return Platforms::page_size(); }
        static SeamResult open_memfd(std::uint64_t pid) noexcept { return Platforms::open_memfd(pid); }
        static SeamResult close_fd(std::int32_t fd) noexcept { return Platforms::close_fd(fd); }
        static SeamResult socket_pair(std::int32_t *out) noexcept { return Platforms::socket_pair(out); }
        static SeamResult set_socket_buffer(std::int32_t fd, std::int32_t bytes) noexcept {
            return Platforms::set_socket_buffer(fd, bytes);
        }
        static SeamResult set_nonblocking(std::int32_t fd) noexcept {
            return Platforms::set_nonblocking(fd);
        }
        static SeamResult send_bytes(std::int32_t fd, const void *data, std::size_t bytes) noexcept {
            return Platforms::send_bytes(fd, data, bytes);
        }
        static SeamResult send_buffer(std::int32_t fd, std::size_t bytes) noexcept {
            return Platforms::send_buffer(fd, bytes);
        }
        static SeamResult fork_child() noexcept {
            const SeamResult forked = Platforms::fork_child();
            if (forked.value == 0) {
                std::_Exit(0);
            }
            return forked;
        }
        static SeamResult stop_child(std::uint64_t pid) noexcept { return Platforms::stop_child(pid); }
        static SeamResult yield_now() noexcept { return Platforms::yield_now(); }
        static SeamResult pin_to_processor(std::int32_t cpu) noexcept {
            return Platforms::pin_to_processor(cpu);
        }
    };
    static_assert(HeapSeam<ProbeSeam>, "the probe seam answers every call the shape names");

    /** A search that never finds anything: the mechanism is not ported yet. */
    struct StubSnitch final {
        static bool start(std::size_t) noexcept { return true; }
        static bool has_collision() noexcept { return false; }
        static std::uint64_t address() noexcept { return 0U; }
        static void reset() noexcept {}
    };
}  // namespace

int main() {
    std::printf("prepare page probe\n");
    const auto page = ProbeSeam::page_size();
    const std::uint64_t page_size = static_cast<std::uint64_t>(page.value > 0 ? page.value : 0U);
    const std::uint64_t region = region_for(page_size);
    PageGeometry geometry{};
    geometry.align_region_size = region;
    geometry.send_size = static_cast<std::size_t>(send_size_for(region));
    geometry.socket_buffer = 1 << 20;
    geometry.main_cpu = 0;
    PrepareInputs inputs{};
    inputs.pre_groups = 2U;
    inputs.spray_groups = 4U;
    inputs.post_groups = 2U;
    inputs.leak_groups = 1U;
    inputs.snitch_collisions = 1U;
    inputs.region_size = region;
    inputs.object_size = page_size;
    PreparePatience patience{};
    patience.max_attempts = 1U;
    patience.poll_rounds = 5U;
    std::printf("  page=%llu region=%llu groups: pre=%zu spray=%zu post=%zu\n",
                static_cast<unsigned long long>(page_size),
                static_cast<unsigned long long>(region), inputs.pre_groups, inputs.spray_groups,
                inputs.post_groups);
    const auto outcome = prepare_page<ProbeSeam, StubSnitch>(inputs, geometry, patience);
    std::printf("  status=%s at=%d opened=%zu closed=%zu errno=%d\n", prepare_status_name(outcome.status),
                static_cast<int>(outcome.at_step), outcome.opened, outcome.closed, outcome.error);
    const bool expected = outcome.status ==
                           ghostlock::backend::cve_2026_43499::memory::PrepareStatus::NoCollision &&
                       outcome.opened > 0U && outcome.closed > 0U && outcome.error == 0;
    std::printf("prepare page probe: %s\n", expected ? "PASS" : "FAIL");
    return expected ? 0 : 1;
}
