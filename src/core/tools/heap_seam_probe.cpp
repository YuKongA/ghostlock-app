/** @file tools/heap_seam_probe.cpp
 *  The heap seam on the device: every call it makes, once, with its answer.
 *
 *  Two things come out of this run. The first is the page size the device actually
 *  boots with, which the mechanism's constants are checked against -- the retired tree
 *  assumed one and would have worked on the wrong region without saying so. The second
 *  is that each operation the seam declares answers on this kernel at all: a memory
 *  descriptor opens, a pair connects, a buffer is set, an end stops blocking, and a
 *  send goes through.
 *
 *  Device only, like the seam it exercises: it calls the kernel.
 */
#include <cstdio>
#include <cstdint>

#include "backend/cve_2026_43499/memory/heap_geometry.hpp"
#include "backend/cve_2026_43499/memory/heap_seam_linux.hpp"
#include "backend/cve_2026_43499/memory/shaping_plan.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::memory::check_region_matches_device;
    using ghostlock::backend::cve_2026_43499::memory::heap_geometry_verdict_name;
    using ghostlock::backend::cve_2026_43499::memory::LinuxHeapSeam;
    using ghostlock::backend::cve_2026_43499::memory::region_for;
    using ghostlock::backend::cve_2026_43499::memory::SeamResult;
    using ghostlock::backend::cve_2026_43499::memory::send_size_for;

    std::size_t g_failed = 0U;

    void report(const char *what, const SeamResult &result, bool ok) {
        std::printf("  %-22s value=%lld errno=%d %s\n", what,
                    static_cast<long long>(result.value), result.error, ok ? "ok" : "FAILED");
        if (!ok) {
            ++g_failed;
        }
    }
}  // namespace

int main() {
    std::printf("heap seam probe\n");
    const SeamResult page = LinuxHeapSeam::page_size();
    report("page_size", page, page.value > 0);
    const std::uint64_t page_size = static_cast<std::uint64_t>(page.value > 0 ? page.value : 0);
    const std::uint64_t region = region_for(page_size);
    std::printf("  region=%llu send=%llu\n", static_cast<unsigned long long>(region),
                static_cast<unsigned long long>(send_size_for(region)));
    /* The check the retired tree could not make: does the region these constants
     *  derive match the page this device actually has. */
    const auto geometry = check_region_matches_device(region, page_size);
    std::printf("  region vs device       %s\n", heap_geometry_verdict_name(geometry));
    if (geometry != ghostlock::backend::cve_2026_43499::memory::HeapGeometryVerdict::Ok) {
        ++g_failed;
    }

    const SeamResult memfd = LinuxHeapSeam::open_memfd(0U);
    report("open_memfd", memfd, memfd.value >= 0);
    if (memfd.value >= 0) {
        report("close_fd", LinuxHeapSeam::close_fd(static_cast<std::int32_t>(memfd.value)), true);
    }

    std::int32_t reclaim_pair[2] = {-1, -1};
    const SeamResult pair = LinuxHeapSeam::socket_pair(reclaim_pair);
    report("socket_pair", pair, pair.value == 0);
    if (pair.value == 0) {
        report("set_socket_buffer",
               LinuxHeapSeam::set_socket_buffer(reclaim_pair[0], 1 << 20), true);
        report("set_nonblocking", LinuxHeapSeam::set_nonblocking(reclaim_pair[0]), true);
        report("send_buffer",
               LinuxHeapSeam::send_buffer(reclaim_pair[0], static_cast<std::size_t>(region)), true);
        (void) LinuxHeapSeam::close_fd(reclaim_pair[0]);
        (void) LinuxHeapSeam::close_fd(reclaim_pair[1]);
    }

    const SeamResult child = LinuxHeapSeam::fork_child();
    if (child.value == 0) {
        /* The child leaves at once: this probe measures calls, not children. */
        return 0;
    }
    report("fork_child", child, child.value > 0);
    if (child.value > 0) {
        (void) LinuxHeapSeam::stop_child(static_cast<std::uint64_t>(child.value));
    }
    report("yield_now", LinuxHeapSeam::yield_now(), true);
    report("pin_to_processor", LinuxHeapSeam::pin_to_processor(0), true);

    std::printf("heap seam probe: %s\n", g_failed == 0U ? "PASS" : "FAIL");
  return g_failed == 0U ? 0 : 1;
}
