#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_DIAGNOSTIC_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_DIAGNOSTIC_HPP

/* B5-9a read-only device diagnostic for CVE-2026-43284.
 *
 * This module composes the already-shipped read-only pieces without ever
 * touching an attack primitive:
 *   - platform::DeviceProbeOps (real_device_probe on a device) collects the
 *     DeviceFacts the endgame will later gate on;
 *   - lkm::resolve_lkm_selection diagnoses the KMI (match / unsupported /
 *     already-patched) from the device uname -r;
 *   - lkm::precheck_module_file inspects the named .ko (ELF / .modinfo /
 *     vermagic / __versions / signature) without loading it.
 *
 * The diagnostic never writes a file, never forks/execs and never loads a
 * module: the only side effect is the structured status-record stream it
 * prints. The composition is injectable (DeviceProbeOps), so host tests drive
 * it with a fake and the device build binds real_device_probe(). */

#include "backend/cve_2026_43284/lkm/lkm_image.hpp"
#include "backend/cve_2026_43284/lkm/lkm_policy.hpp"
#include "platform/device_facts.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace ghostlock::backend::cve_2026_43284::diagnostic {

    inline constexpr char kDiagnosticMarker = static_cast<char>(0x1e);

    enum class DiagnosticOutcome : std::uint8_t {
        Ready = 0,          /* facts present, KMI matched, precheck passed */
        DeviceBlocked = 2,  /* probe unavailable or a mandatory fact missing */
        ModuleRejected = 3, /* device ok, KMI/module policy rejected */
    };

    struct DiagnosticReport final {
        DiagnosticOutcome outcome = DiagnosticOutcome::DeviceBlocked;
        platform::DeviceFactError fact_error = platform::DeviceFactError::Unavailable;
        bool facts_present = false;
        platform::DeviceFacts facts{};
        bool release_parsed = false;
        lkm::KernelRelease release{};
        lkm::LkmPolicyError lkm_error = lkm::LkmPolicyError::None;
        bool kmi_resolved = false;
        const lkm::SupportedKmi *kmi = nullptr;
        bool module_checked = false;
        lkm::ModuleFacts module_facts{};
        lkm::LkmImageError image_error = lkm::LkmImageError::None;
        std::string_view module_path{};
    };

    /* Read-only composition. device may be an unavailable surface (host), in
     * which case the outcome is DeviceBlocked. Never writes, forks or execs. */
    [[nodiscard]] DiagnosticReport run_device_diagnostic(
            const platform::DeviceProbeOps &device, std::string_view module_path) noexcept;

    /* Deterministic status-record style text: one 0x1e-framed line per fact
     * slot, so the output is greppable and testable. */
    [[nodiscard]] std::string format_diagnostic(const DiagnosticReport &report);

    /* Process exit code: 0 Ready, 2 DeviceBlocked, 3 ModuleRejected. */
    [[nodiscard]] int diagnostic_exit_code(const DiagnosticReport &report) noexcept;

    /* Stable enum spellings for logs and tests. */
    [[nodiscard]] std::string_view device_fact_error_name(
            platform::DeviceFactError error) noexcept;
    [[nodiscard]] std::string_view lkm_policy_error_name(
            lkm::LkmPolicyError error) noexcept;
    [[nodiscard]] std::string_view lkm_image_error_name(
            lkm::LkmImageError error) noexcept;

    /* Device entry point used by main: real_device_probe() -> run -> print.
     * Returns 1 for an empty module path (usage guard), else the report exit
     * code. Not noexcept because formatting allocates a std::string. */
    int run_diagnostic_cli(std::string_view module_path);
} // namespace ghostlock::backend::cve_2026_43284::diagnostic

#endif
