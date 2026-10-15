#pragma once
/** @file backend/cve_2026_43499/perf_ops_linux.hpp
 *  The sampling edge on the platform this runs on: no decisions, only calls.
 *
 *  Android only, and deliberately not part of the host build. Each call answers with its
 *  result and the errno beside it and does nothing else. The open call is the one that a
 *  device may refuse outright -- a kernel with sampling restricted will say so, and that is
 *  worth a named failure rather than a retry.
 */
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/perf_event.h>

#include "backend/cve_2026_43499/perf_ops.hpp"

namespace ghostlock::backend::cve_2026_43499 {
    struct LinuxPerfOps final {
        static OpsResult perf_open(const PerfOpenArgs &args) noexcept {
            struct perf_event_attr attr {};
            attr.type = args.type;
            attr.size = sizeof(attr);
            attr.config = args.config;
            attr.sample_period = args.sample_period;
            attr.sample_type = args.sample_type;
            attr.sample_regs_intr = args.sample_regs_intr;
            attr.disabled = args.start_disabled ? 1U : 0U;
            attr.exclude_user = args.exclude_user ? 1U : 0U;
            attr.exclude_hv = args.exclude_hv ? 1U : 0U;
            attr.exclude_idle = args.exclude_idle ? 1U : 0U;
            errno = 0;
            const long fd = ::syscall(SYS_perf_event_open, &attr, args.pid, args.cpu, -1, 0UL);
            return OpsResult{static_cast<std::int64_t>(fd), errno};
        }

        static void *perf_map(std::int32_t fd, std::size_t bytes) noexcept {
            void *mapped = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            return mapped == MAP_FAILED ? nullptr : mapped;
        }

        static OpsResult perf_ioctl(std::int32_t fd, std::uint64_t request) noexcept {
            errno = 0;
            const long rc = ::ioctl(fd, request == kPerfEventIocEnable ? PERF_EVENT_IOC_ENABLE
                                                                        : PERF_EVENT_IOC_DISABLE,
                                    0UL);
            return OpsResult{static_cast<std::int64_t>(rc), errno};
        }

        /** One unit of work for the sampler to catch: the retired path called getpid in a
         *  loop, which is the cheapest syscall that still shows up as a sample. */
        static OpsResult spin_once() noexcept {
            errno = 0;
            const long pid = ::syscall(SYS_getpid);
            return OpsResult{static_cast<std::int64_t>(pid), errno};
        }
    };

    static_assert(PerfOps<LinuxPerfOps>,
                  "the device landing answers every call the shape names");
}  // namespace ghostlock::backend::cve_2026_43499
