#ifndef GHOSTLOCK_CVE2026_43499_BACKEND_HPP
#define GHOSTLOCK_CVE2026_43499_BACKEND_HPP

#include "memory/payload_builder.h"
#include "profile/model.h"
#include "route/component_catalog.hpp"
#include "session/exploit_session.hpp"
#include "session/stage_types.hpp"
#include "support/status.hpp"

namespace ghostlock::session::backend {
    /* Batch 4 (D1=B) cve_2026_43499 backend: the setup stage plus the W1/W2/W3
     * step sequence. The sequence is owned by this backend (a different backend
     * may shape or order the stages differently); the middleware route hooks are
     * called directly from these steps (route/middleware_hooks.hpp), so no
     * vtable enters the path. Statement order and log text are the pre-Batch-4
     * sequence, unchanged. */
    struct Cve2026_43499Policy final {
        static constexpr runtime::BackendKind kind = runtime::BackendKind::Cve2026_43499;
        static constexpr bool available = true;

        /* setup -> W1 -> W2/W3 chain. Continue hands the filled chain to the
         * frontend handoff step (see runtime::run_pipeline). */
        [[nodiscard]] static StageResult run(ExploitSession &session,
                                             const profile::kernel_offsets &decoded,
                                             const char *debug_dir, bool force_attack,
                                             VictimChain &chain);

        /* One route write: middleware resident fast path, else heap spray + PI
         * race. Public because the attack-function disassembly gate compares it
         * by symbol (cmp_disasm "do_one_write"). */
        [[nodiscard]] static Status attack_write(ExploitSession &session,
                                                 const memory::WriteRequest &request,
                                                 const char *desc);

        /* Stage: process setup and profile installation. */
        [[nodiscard]] static StageResult run_setup(ExploitSession &session,
                                                   const profile::kernel_offsets &decoded,
                                                   const char *debug_dir, bool force_attack);
    };
} // namespace ghostlock::session::backend

#endif
