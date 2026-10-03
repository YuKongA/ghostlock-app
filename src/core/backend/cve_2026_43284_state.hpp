#ifndef GHOSTLOCK_CVE2026_43284_STATE_HPP
#define GHOSTLOCK_CVE2026_43284_STATE_HPP

/* CVE-2026-43284 backend-private profile (A2-3c / S3 B4).
 *
 * This is deliberately NOT a CoreSession slot: the 43284 backend receives it by
 * value and it never touches Cve2026_43499State or the shared backend_state
 * bytes, so the 43499 state/layout and attack codegen are unaffected.
 *
 * GLK1 v2 values are fixed 64-bit slots (ADR-0003 decision 1), so every field
 * is a backend-owned numeric policy token. The page-cache -> vendor carrier ->
 * LKM -> UMH chain's path/symbol/context parameters are selected by these
 * tokens and resolved by the 43284 backend (B5); they are never carried as
 * literal text. Presence is meaningful: std::optional distinguishes an omitted
 * field from a provided zero. */

#include <cstdint>
#include <optional>

namespace ghostlock::backend {
    struct Cve2026_43284Profile final {
        /* Vendor carrier file to patch: carrier selector/policy token. */
        std::optional<uint64_t> carrier_path;
        /* LKM delivery selector (which .ko / delivery policy). */
        std::optional<uint64_t> lkm_path;
        /* Kernel module interface, encoded major*1000 + minor (e.g. 515). */
        std::optional<uint16_t> kmi;
        /* SELinux exec context selector used for the UMH/LKM transition. */
        std::optional<uint64_t> selinux_exec_context;
        /* late-load argument policy bitmask (ksud/UMH argv selection). */
        std::optional<uint64_t> late_load_args;
        /* Defex symbol selector / token. */
        std::optional<uint64_t> defex_symbol;
        /* StepSet id (ADR-0004 R18); PageCacheWrite is the 43284 vocabulary. */
        std::optional<uint16_t> steps;
    };
} // namespace ghostlock::backend

#endif
