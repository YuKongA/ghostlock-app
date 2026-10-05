#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_SCHEMA_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_SCHEMA_HPP

/* Owner schema for the cve_2026_43284 private profile section (S3 B4 / S4 R1).
 *
 * The section name is backend.cve_2026_43284, sibling-in-kind to
 * backend.cve_2026_43499.steps but a distinct owner: 43284 never reuses a
 * 43499 wire slot. Every field is optional (presence is expressed by key
 * occurrence). Since S4 R4 the three policy paths are WireKind::String and the
 * backend consumes them directly; the remaining fields stay numeric.
 *
 * S4 R1 moved the previously-scattered effective values here, byte-for-byte.
 * S4 R4 stringifies the three policy paths and moves the handshake/wait tuning
 * in:
 *   - carrier_path / lkm_path / defex_symbol -> WireKind::String paths. The two
 *     conventions that used to resolve to numeric tokens are now pure
 *     declarations (no resolver): the composition root owns the actual path
 *     (first device-present carrier; $GHOSTLOCK_HOME/helper.ko), so the View
 *     keeps presence semantics and the effective values are unchanged;
 *   - wait_timeout_ms -> literal 15000 (was execution_binding hardcode);
 *   - module_poll_attempts / module_poll_interval_ms -> literal 40 / 5 (was
 *     LkmWindowRuntime kOpenRetryAttempts/kOpenRetryDelayMs).
 *
 * RegistryForSelection composes the 43284 owner for the catalogued
 * {cve_2026_43284, PageCacheWrite, umh_forward} triple. */

#include "backend/cve_2026_43284_state.hpp"
#include "backend/cve_2026_43284/lkm/lkm_policy.hpp"
#include "contract/identity.hpp"
#include "profile/registry.hpp"
#include "profile/schema.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace ghostlock::backend {
    using Cve2026_43284Field = profile::FieldSpec<Cve2026_43284Profile>;

    inline constexpr std::string_view kCve2026_43284Section =
            "backend.cve_2026_43284";

    /* R1 derived default: kmi = kernel_major * 1000 + kernel_minor from the
     * document release. Returns false when the release is unparsable, in which
     * case the field stays absent and resolve_lkm_selection reports the failure
     * exactly as before. */
    [[nodiscard]] inline bool derive_kmi_from_release(
            const profile::Document &document, uint64_t &out) noexcept {
        cve_2026_43284::lkm::KernelRelease release{};
        if (!cve_2026_43284::lkm::parse_kernel_release(document.release, release)) return false;
        out = release.kmi;
        return true;
    }

    inline constexpr std::string_view kCve2026_43284LkmPathConvention =
            "$GHOSTLOCK_HOME/helper.ko";
    inline constexpr std::string_view kCve2026_43284CarrierConvention =
            "device-first-present-default";

    /* The single literal authorities for the two policy defaults.
     * run_backend_terminal and the tests use these instead of re-spelling them. */
    inline constexpr uint64_t kCve2026_43284SelinuxDefault =
            cve_2026_43284::lkm::kSelinuxExecContextVendorModprobe;
    inline constexpr uint64_t kCve2026_43284LateLoadArgsDefault = 0U;

    /* R1 convention: the KernelSU late-load package name (was lkm_image.cpp
     * default_root_package). It is keyed by the terminal root-program kind, not
     * by a wire field, because the module package is the App's root program. */
    inline constexpr std::string_view kCve2026_43284RootPackageKernelSU =
            "me.weishu.kernelsu";

    [[nodiscard]] inline std::string_view root_package_convention(
            contract::RootProgramKind kind) noexcept {
        return kind == contract::RootProgramKind::KernelSU
                       ? kCve2026_43284RootPackageKernelSU
                       : std::string_view{};
    }

    struct Cve2026_43284Schema final {
        using View = Cve2026_43284Profile;

        static constexpr Cve2026_43284Field kFields[] = {
            {kCve2026_43284Section, "carrier_path", 0, false, false, nullptr,
             profile::DefaultValue::convention(kCve2026_43284CarrierConvention),
             profile::FieldSource::Convention, profile::WireKind::String,
             "Vendor carrier absolute path; absent = first device-present default.",
             [](Cve2026_43284Profile &view, std::string_view text) {
                 view.carrier_path = text;
             }},
            {kCve2026_43284Section, "lkm_path", 0, false, false, nullptr,
             profile::DefaultValue::convention(kCve2026_43284LkmPathConvention),
             profile::FieldSource::Convention, profile::WireKind::String,
             "LKM absolute path; absent = $GHOSTLOCK_HOME/helper.ko.",
             [](Cve2026_43284Profile &view, std::string_view text) {
                 view.lkm_path = text;
             }},
            {kCve2026_43284Section, "defex_symbol", 0, false, false, nullptr,
             profile::DefaultValue::none(), profile::FieldSource::Profile,
             profile::WireKind::String, "Defex symbol name.",
             [](Cve2026_43284Profile &view, std::string_view text) {
                 view.defex_symbol = text;
             }},
            {kCve2026_43284Section, "kmi", 2, false, false,
             [](Cve2026_43284Profile &view, uint64_t raw) {
                 view.kmi = static_cast<uint16_t>(raw);
             },
             profile::DefaultValue::derived(&derive_kmi_from_release),
             profile::FieldSource::Derived, profile::WireKind::UInt,
             "Kernel module interface, encoded major*1000 + minor."},
            {kCve2026_43284Section, "selinux_exec_context", 8, false, false,
             [](Cve2026_43284Profile &view, uint64_t raw) {
                 view.selinux_exec_context = raw;
             },
             profile::DefaultValue::literal(kCve2026_43284SelinuxDefault),
             profile::FieldSource::Profile, profile::WireKind::UInt,
             "SELinux exec context selector; 0 = vendor_modprobe."},
            {kCve2026_43284Section, "late_load_args", 8, false, false,
             [](Cve2026_43284Profile &view, uint64_t raw) {
                 view.late_load_args = raw;
             },
             profile::DefaultValue::literal(kCve2026_43284LateLoadArgsDefault),
             profile::FieldSource::Profile, profile::WireKind::UInt,
             "late-load argument policy bitmask; 0 = program + late-load only."},
            /* S4 R6b combination token. 43284 has no route axis, so the token
             * is the bare path name (umh today); the internal PageCacheWrite id
             * is derived from the shared contract whitelist. A legacy numeric
             * value is rewritten to its token before the bind runs. */
            {kCve2026_43284Section, "steps", 0, false, false, nullptr,
             profile::DefaultValue::none(), profile::FieldSource::Profile,
             profile::WireKind::String,
             "Combination token (bare path, S4 R6b).",
             [](Cve2026_43284Profile &view, std::string_view text) {
                 view.steps = contract::combination_stepset_wire(
                         contract::BackendKind::Cve2026_43284, text);
             }},
            {kCve2026_43284Section, "wait_timeout_ms", 4, false, false,
             [](Cve2026_43284Profile &view, uint64_t raw) {
                 view.wait_timeout_ms = static_cast<uint32_t>(raw);
             },
             profile::DefaultValue::literal(kCve2026_43284WaitTimeoutDefaultMs),
             profile::FieldSource::Profile, profile::WireKind::UInt,
             "Chain terminus wait budget in ms; default 15000."},
            {kCve2026_43284Section, "module_poll_attempts", 4, false, false,
             [](Cve2026_43284Profile &view, uint64_t raw) {
                 view.module_poll_attempts = static_cast<uint32_t>(raw);
             },
             profile::DefaultValue::literal(kCve2026_43284ModulePollAttemptsDefault),
             profile::FieldSource::Profile, profile::WireKind::UInt,
             "LKM window handshake retries; default 40."},
            {kCve2026_43284Section, "module_poll_interval_ms", 4, false, false,
             [](Cve2026_43284Profile &view, uint64_t raw) {
                 view.module_poll_interval_ms = static_cast<uint32_t>(raw);
             },
             profile::DefaultValue::literal(kCve2026_43284ModulePollIntervalMsDefault),
             profile::FieldSource::Profile, profile::WireKind::UInt,
             "LKM window handshake retry delay in ms; default 5."},
        };
    };

    static_assert(profile::SchemaDefinition<Cve2026_43284Schema>);

    /* The backend-private StepSet id carried by the 43284 section (0 when the
     * key is absent). Consumed by the composition root before dispatch. */
    [[nodiscard]] inline uint16_t steps_from(const profile::Document &document) noexcept {
        const profile::Value *value = document.find_value(kCve2026_43284Section, "steps");
        return value != nullptr ? static_cast<uint16_t>(value->raw) : 0;
    }

    /* The carrier absolute path carried by the 43284 section, or nullopt when
     * the key is absent. The composition root reads this to select and bind the
     * single carrier before Pipeline::run, since the chain's page-cache target
     * must be opened before the backend decodes the profile. The returned view
     * aliases the Document's decode buffer. */
    [[nodiscard]] inline std::optional<std::string_view> carrier_path_from(
            const profile::Document &document) noexcept {
        const profile::Value *value =
                document.find_value(kCve2026_43284Section, "carrier_path");
        if (value == nullptr || !value->present || !value->is_text) {
            return std::nullopt;
        }
        return value->text;
    }

    /* Typed string accessor for one 43284 String field, or nullopt when absent.
     * Used by the composition root to read paths without re-implementing the
     * present/text validation. */
    [[nodiscard]] inline std::optional<std::string_view> string_field_from(
            const profile::Document &document, std::string_view key) noexcept {
        const profile::Value *value = document.find_value(kCve2026_43284Section, key);
        if (value == nullptr || !value->present || !value->is_text) {
            return std::nullopt;
        }
        return value->text;
    }

    /* Typed uint32 accessor for one 43284 tuning field, or nullopt when absent.
     * The schema default (15000/40/5) is applied by the bind; the composition
     * root reads the raw document (before the bind runs) with this accessor and
     * applies the same default constant. */
    [[nodiscard]] inline std::optional<uint32_t> u32_field_from(
            const profile::Document &document, std::string_view key) noexcept {
        const profile::Value *value = document.find_value(kCve2026_43284Section, key);
        if (value == nullptr || !value->present || value->is_text ||
            value->raw > 0xFFFFFFFFULL) {
            return std::nullopt;
        }
        return static_cast<uint32_t>(value->raw);
    }
} // namespace ghostlock::backend

/* S4 R1 registry composition: the catalogue 43284 triple owns only this backend
 * section (no platform ABI section). */
namespace ghostlock::profile {
    template<>
    struct RegistryForSelection<contract::ComponentSelection{
            contract::BackendKind::Cve2026_43284,
            contract::StepSetKind::PageCacheWrite,
            contract::TerminalKind::UmhForward}> {
        using type = SchemaRegistry<backend::Cve2026_43284Schema>;
    };
} // namespace ghostlock::profile

#endif
