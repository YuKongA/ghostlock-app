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
 *   - wait_timeout_ms -> literal 15000 (was execution_binding hardcode);
 *   - module_poll_attempts / module_poll_interval_ms -> literal 40 / 5 (was
 *     LkmWindowRuntime kOpenRetryAttempts/kOpenRetryDelayMs).
 *
 * HOCON refactor: the owner now spans TWO wire sections --
 *   - backend.cve_2026_43284: the selection token (steps) plus the three
 *     runtime-injected keys (kmi / lkm_path / carrier_path), which the profile
 *     no longer declares (wire_only in the GLKv3 list, so the manifest omits
 *     them) and native resolves at the point of use;
 *   - backend.cve_2026_43284.execution: late_load_args / selinux_exec_context /
 *     module_poll_attempts / module_poll_interval_ms / wait_timeout_ms.
 * is_cve_2026_43284_section() is the single predicate for both, used by every
 * filtered Document copy.
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

    /* HOCON refactor: the five execution knobs live in their own flat section
     * (the wire carries the execution. level too, never a flattened key).
     * kmi / lkm_path / carrier_path stay in the backend section: the wire keeps
     * them, but the profile no longer declares them, so the exported manifest
     * omits them (they are wire_only in the GLKv3 list) and native resolves each
     * value at the point of use. */
    inline constexpr std::string_view kCve2026_43284ExecutionSection =
            "backend.cve_2026_43284.execution";

    /* The sections this owner binds. One predicate, so a filtered Document copy
     * cannot silently drop the execution section -- that would flip five
     * execution fields to their defaults.
     *
     * A THIRD 43284-owned section MUST be added here: every filtered copy
     * (backend_terminal.cpp state_from and tests/profile_bind_compat.cpp) asks
     * this predicate, and a section missing from it is dropped silently. */
    [[nodiscard]] inline bool is_cve_2026_43284_section(
            std::string_view name) noexcept {
        return name == kCve2026_43284Section ||
               name == kCve2026_43284ExecutionSection;
    }

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
            {kCve2026_43284Section, "kmi", 2, false, false,
             [](Cve2026_43284Profile &view, uint64_t raw) {
                 view.kmi = static_cast<uint16_t>(raw);
             },
             profile::DefaultValue::derived(&derive_kmi_from_release),
             profile::FieldSource::Derived, profile::WireKind::UInt,
             "Kernel module interface, encoded major*1000 + minor."},
            {kCve2026_43284ExecutionSection, "selinux_exec_context", 8, false, false,
             [](Cve2026_43284Profile &view, uint64_t raw) {
                 view.selinux_exec_context = raw;
             },
             profile::DefaultValue::literal(kCve2026_43284SelinuxDefault),
             profile::FieldSource::Profile, profile::WireKind::UInt,
             "SELinux exec context selector; 0 = vendor_modprobe."},
            {kCve2026_43284ExecutionSection, "late_load_args", 8, false, false,
             [](Cve2026_43284Profile &view, uint64_t raw) {
                 view.late_load_args = raw;
             },
             profile::DefaultValue::literal(kCve2026_43284LateLoadArgsDefault),
             profile::FieldSource::Profile, profile::WireKind::UInt,
             "late-load argument policy bitmask; 0 = program + late-load only."},
            /* M5: the combination-token SPELLING is removed from the wire (the
             * selection is `queue`). The field stays declared so a token sent by
             * an older document is REFUSED BY NAME (`reason=token-form-removed`)
             * instead of falling into the generic unknown-key path - see
             * profile/glkv3_parse.cpp. A pre-v3 numeric id is still rewritten to
             * its token before the bind runs. */
            {kCve2026_43284Section, "steps", 0, false, false, nullptr,
             profile::DefaultValue::none(), profile::FieldSource::Profile,
             profile::WireKind::String,
             "REMOVED selection surface (M5): a token here is REFUSED (reason=token-form-removed).",
             nullptr,
             [](Cve2026_43284Profile &view, std::string_view text) {
                 std::uint16_t resolved = 0;
                 if (!contract::combination_stepset_wire_checked(
                             contract::BackendKind::Cve2026_43284, text, resolved)) {
                     return false;
                 }
                 view.steps = resolved;
                 return true;
             }},
            /* M2 queue selection (design doc 4.5/5.0): the queue is canonical,
             * route is declared so a route-less backend can report
             * route-not-applicable instead of unknown_key, and experimental is
             * the U5 static opt-in. All three are selection-owned. */
            {kCve2026_43284Section, "queue", 0, false, false, nullptr,
             profile::DefaultValue::none(), profile::FieldSource::Profile,
             profile::WireKind::Array,
             "Step queue: array of {step|seam[,stage]} (M2).", nullptr, nullptr,
             true},
            {kCve2026_43284Section, "route", 0, false, false, nullptr,
             profile::DefaultValue::none(), profile::FieldSource::Profile,
             profile::WireKind::String,
             "Queue-level route token (M2; forbidden for this backend).", nullptr,
             nullptr, true},
            {kCve2026_43284Section, "experimental", 1, false, false, nullptr,
             profile::DefaultValue::none(), profile::FieldSource::Profile,
             profile::WireKind::Bool,
             "Static experimental opt-in declaration (U5).", nullptr, nullptr, true},
            {kCve2026_43284ExecutionSection, "wait_timeout_ms", 4, false, false,
             [](Cve2026_43284Profile &view, uint64_t raw) {
                 view.wait_timeout_ms = static_cast<uint32_t>(raw);
             },
             profile::DefaultValue::literal(kCve2026_43284WaitTimeoutDefaultMs),
             profile::FieldSource::Profile, profile::WireKind::UInt,
             "Chain terminus wait budget in ms; default 15000."},
            {kCve2026_43284ExecutionSection, "module_poll_attempts", 4, false, false,
             [](Cve2026_43284Profile &view, uint64_t raw) {
                 view.module_poll_attempts = static_cast<uint32_t>(raw);
             },
             profile::DefaultValue::literal(kCve2026_43284ModulePollAttemptsDefault),
             profile::FieldSource::Profile, profile::WireKind::UInt,
             "LKM window handshake retries; default 40."},
            {kCve2026_43284ExecutionSection, "module_poll_interval_ms", 4, false, false,
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

    /* Typed uint32 accessor for one 43284 execution-tuning field, or nullopt
     * when absent. The schema default (15000/40/5) is applied by the bind; the
     * composition root reads the raw document (before the bind runs) with this
     * accessor and applies the same default constant. The fields live in the
     * execution section since the HOCON refactor. */
    [[nodiscard]] inline std::optional<uint32_t> u32_field_from(
            const profile::Document &document, std::string_view key) noexcept {
        const profile::Value *value =
                document.find_value(kCve2026_43284ExecutionSection, key);
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
