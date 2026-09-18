#ifndef GHOSTLOCK_HANDOFF_PROBE_HPP
#define GHOSTLOCK_HANDOFF_PROBE_HPP

#include <cstdint>
#include <string_view>

namespace ghostlock {

/* Post-exploit KernelSU handoff probe results. The field semantics stay the
 * legacy booleans so the caller keeps the exact log lines and their order. */
struct HandoffProbeResult {
    bool module_visible = false;
    bool ksu_log_loaded = false;
    bool ksu_log_failed = false;
    bool enforce_ok = false;

    [[nodiscard]] bool ready() const noexcept {
        return module_visible || ksu_log_loaded;
    }
};

/* Poll policy from TargetProfile.execution plus the legacy log cadence. */
struct HandoffPollPolicy {
    uint32_t module_poll_attempts = 0;
    uint32_t module_poll_interval_ms = 0;
    uint32_t enforce_poll_attempts = 0;
    uint32_t enforce_poll_interval_ms = 0;
    uint32_t log_poll_attempts = 60;
    uint32_t log_poll_interval_ms = 500;
};

/* /proc/modules scan for the kernelsu entry. */
bool kernelsu_module_visible() noexcept;

/* Re-scan the root-side log; markers accumulate into loaded/failed. */
bool scan_ksu_log(std::string_view path, bool &loaded, bool &failed) noexcept;

/* TODO(U01-S14-KSU-LOG): the per-run KernelSU log path is still derived from
 * home_dir here; upstream generates a per-run path in Kotlin and passes it
 * explicitly. Blocked by: the Kotlin/native launch protocol change.
 * Completion: accept the resolved path from the caller and delete this note. */

/* Run the module, log and enforce polls with the given policy. */
HandoffProbeResult handoff_probe_run(const HandoffPollPolicy &policy,
                                     std::string_view home_dir) noexcept;

}  // namespace ghostlock

#endif
