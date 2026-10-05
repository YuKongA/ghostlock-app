#ifndef GHOSTLOCK_CVE2026_43284_STATE_HPP
#define GHOSTLOCK_CVE2026_43284_STATE_HPP

/* CVE-2026-43284 backend-private profile (A2-3c / S3 B4, stringified S4 R4).
 *
 * This is deliberately NOT a CoreSession slot: the 43284 backend receives it by
 * value and it never touches Cve2026_43499State or the shared backend_state
 * bytes, so the 43499 state/layout and attack codegen are unaffected.
 *
 * S4 R4: the three path/symbol policy fields are WireKind::String views into
 * the decode buffer instead of numeric selector tokens. They are non-owning:
 * the Document/entry storage must outlive the View that carries them. The
 * handshake/wait tuning (wait_timeout_ms, module_poll_attempts,
 * module_poll_interval_ms) is now document policy too, defaulting to the values
 * that were previously hardcoded in execution_binding (15000) and
 * lkm_window (40 retries x 5 ms).
 *
 * Presence is meaningful: std::optional distinguishes an omitted field from a
 * provided empty string. */

#include <cstdint>
#include <optional>
#include <string_view>

namespace ghostlock::backend {
    /* S4 R4 single authority for the 43284 handshake/wait defaults; shared by the
     * schema FieldSpec (wire default + default_used diagnostic) and the
     * composition root that binds the values before the schema is applied. */
    inline constexpr uint32_t kCve2026_43284WaitTimeoutDefaultMs = 15000U;
    inline constexpr uint32_t kCve2026_43284ModulePollAttemptsDefault = 40U;
    inline constexpr uint32_t kCve2026_43284ModulePollIntervalMsDefault = 5U;

    struct Cve2026_43284Profile final {
        /* Vendor carrier file to patch (absolute path), or absent = first
         * device-present default carrier. */
        std::optional<std::string_view> carrier_path;
        /* LKM path: explicit .ko path (custom-file delivery), or absent =
         * $GHOSTLOCK_HOME/helper.ko bundled mirror. */
        std::optional<std::string_view> lkm_path;
        /* Kernel module interface, encoded major*1000 + minor (e.g. 515). */
        std::optional<uint16_t> kmi;
        /* SELinux exec context selector used for the UMH/LKM transition. */
        std::optional<uint64_t> selinux_exec_context;
        /* late-load argument policy bitmask (ksud/UMH argv selection). */
        std::optional<uint64_t> late_load_args;
        /* Defex symbol name, or absent (was a numeric selector token). */
        std::optional<std::string_view> defex_symbol;
        /* StepSet id (ADR-0004 R18); PageCacheWrite is the 43284 vocabulary. */
        std::optional<uint16_t> steps;
        /* Chain terminus wait budget in ms (was hardcoded 15000). */
        std::optional<uint32_t> wait_timeout_ms;
        /* LKM residency-window handshake poll (was kOpenRetryAttempts/Delay). */
        std::optional<uint32_t> module_poll_attempts;
        std::optional<uint32_t> module_poll_interval_ms;
    };
} // namespace ghostlock::backend

#endif
