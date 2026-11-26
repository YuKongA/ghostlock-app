#include "host_attack_script.hpp"
#include "session/handoff_probe.hpp"

/* Host stubs for the KernelSU handoff probe: no /proc scan, no su probe.
 * handoff_probe_run reports ready per the script so the data flow reaches
 * StageResult::Done. */
namespace lsprivilege::session {
    bool kernelsu_module_visible() noexcept { return false; }

    bool ksu_root_owned() noexcept { return false; }

    bool scan_ksu_log(std::string_view path, bool &loaded, bool &failed,
                      bool *is_apatch) noexcept {
        (void) path;
        (void) loaded;
        (void) failed;
        (void) is_apatch;
        return false;
    }

    HandoffProbeResult handoff_probe_run(const HandoffPollPolicy &policy,
                                         std::string_view ksu_log_path) noexcept {
        (void) policy;
        (void) ksu_log_path;
        HandoffProbeResult result{};
        result.module_visible = host::script().handoff_ready;
        if (host::script().handoff_ready) result.enforce_ok = true;
        return result;
    }
} // namespace lsprivilege::session
