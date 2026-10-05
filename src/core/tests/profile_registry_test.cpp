/* S4 R1 schema registry + default tests.
 *
 * Covers the single-authority bind_all(registry, document, sink, mode):
 *   - required missing rejects with the owner-qualified (section, key) path;
 *   - BindMode::Test relaxes only the required rule;
 *   - Literal / Derived / Convention defaults are stored and reported through
 *     the no-allocation BindLog (default_used=<section>.<key>);
 *   - a profile value always wins over a declared default;
 *   - the cve_2026_43284 effective values are frozen to the pre-R1 values:
 *     kmi = major*1000 + minor (5015 for 5.15), selinux = 0 (vendor_modprobe),
 *     late_load_args = 0, lkm_path = bundled token under the
 *     $GHOSTLOCK_HOME/helper.ko convention;
 *   - RegistryForSelection composes platform::abi + backend::<id> by
 *     (backend, steps, terminal).
 *
 * Host-only: header-only schemas, no session and no device. */

#include "backend/cve_2026_43284/schema.hpp"
#include "backend/cve_2026_43284_state.hpp"
#include "backend/cve_2026_43499/schema.hpp"
#include "backend/cve_2026_43499/backend_profile/model.hpp"
#include "platform/abi.hpp"
#include "profile/document.hpp"
#include "profile/registry.hpp"
#include "profile/schema.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace {
    using ghostlock::profile::BindCode;
    using ghostlock::profile::BindLog;
    using ghostlock::profile::BindMode;
    using ghostlock::profile::DefaultValue;
    using ghostlock::profile::Document;
    using ghostlock::profile::FieldSource;
    using ghostlock::profile::make_registry;
    using ghostlock::profile::make_sink;
    using ghostlock::profile::SchemaRegistry;
    using ghostlock::profile::WireKind;

    struct TestView final {
        std::uint64_t a = 0;
        std::uint64_t b = 0;
        std::uint64_t c = 0;
        std::uint64_t d = 0;
    };

    [[nodiscard]] bool derive_c(const Document &, std::uint64_t &out) noexcept {
        out = 42;
        return true;
    }
    [[nodiscard]] bool resolve_d(const Document &, std::uint64_t &out) noexcept {
        out = 99;
        return true;
    }

    struct TestSchema final {
        using View = TestView;
        static constexpr ghostlock::profile::FieldSpec<TestView> kFields[] = {
            {"t", "a", 8, false, true,
             [](TestView &view, std::uint64_t raw) { view.a = raw; },
             DefaultValue::none(), FieldSource::Profile, WireKind::UInt,
             "required field"},
            {"t", "b", 8, false, false,
             [](TestView &view, std::uint64_t raw) { view.b = raw; },
             DefaultValue::literal(7), FieldSource::Profile, WireKind::UInt,
             "literal default"},
            {"t", "c", 8, false, false,
             [](TestView &view, std::uint64_t raw) { view.c = raw; },
             DefaultValue::derived(&derive_c), FieldSource::Derived,
             WireKind::UInt, "derived default"},
            {"t", "d", 8, false, false,
             [](TestView &view, std::uint64_t raw) { view.d = raw; },
             DefaultValue::convention("test/convention", &resolve_d),
             FieldSource::Convention, WireKind::UInt, "convention default"},
        };
    };

    struct LogState final {
        int count = 0;
        std::string last{};
    };

    void log_emit(void *ctx, std::string_view section, std::string_view key,
                  FieldSource source) noexcept {
        (void)source;
        auto *state = static_cast<LogState *>(ctx);
        state->count += 1;
        state->last.assign(section);
        state->last.push_back('.');
        state->last.append(key);
    }

    void add(Document &doc, std::string_view section, std::string_view key,
             std::uint64_t raw) {
        ghostlock::profile::Section *found = doc.find_section(section);
        if (found == nullptr) found = &doc.append_section(section);
        found->add(key, raw);
    }

    void test_required_and_defaults() {
        /* Required present: literal + derived + convention all default. */
        {
            Document doc;
            add(doc, "t", "a", 1);
            TestView view{};
            LogState log{};
            auto sink = make_sink(view);
            sink.log = BindLog{&log_emit, &log};
            const auto status = ghostlock::profile::bind_all(
                    make_registry<TestSchema>(), doc, sink, BindMode::Production);
            assert(status.ok());
            assert(view.a == 1);
            assert(view.b == 7);
            assert(view.c == 42);
            assert(view.d == 99);
            assert(log.count == 3);
            assert(log.last == "t.d");
        }
        /* Required missing: Rejected with the owner-qualified path. */
        {
            Document doc;
            add(doc, "t", "b", 5);
            TestView view{};
            const auto status = ghostlock::profile::bind_all(
                    make_registry<TestSchema>(), doc, make_sink(view),
                    BindMode::Production);
            assert(status.code == BindCode::MissingRequired);
            assert(status.section == "t");
            assert(status.key == "a");
        }
        /* Test relaxes required and still applies defaults. */
        {
            Document doc;
            TestView view{};
            LogState log{};
            auto sink = make_sink(view);
            sink.log = BindLog{&log_emit, &log};
            const auto status = ghostlock::profile::bind_all(
                    make_registry<TestSchema>(), doc, sink, BindMode::Test);
            assert(status.ok());
            assert(view.a == 0);
            assert(view.b == 7);
            assert(view.c == 42);
            assert(view.d == 99);
            assert(log.count == 3);
        }
        /* Profile value wins over every default, so nothing is reported. */
        {
            Document doc;
            add(doc, "t", "a", 1);
            add(doc, "t", "b", 8);
            add(doc, "t", "c", 9);
            add(doc, "t", "d", 10);
            TestView view{};
            LogState log{};
            auto sink = make_sink(view);
            sink.log = BindLog{&log_emit, &log};
            assert(ghostlock::profile::bind_all(make_registry<TestSchema>(), doc,
                                                sink, BindMode::Production)
                           .ok());
            assert(view.b == 8 && view.c == 9 && view.d == 10);
            assert(log.count == 0);
        }
        /* Unknown section/key still reject in Test mode. */
        {
            Document doc;
            add(doc, "t", "bogus", 1);
            TestView view{};
            const auto status = ghostlock::profile::bind_all(
                    make_registry<TestSchema>(), doc, make_sink(view),
                    BindMode::Test);
            assert(status.code == BindCode::UnknownKey);
        }
    }

    void test_43284_pre_r1_values() {
        using ghostlock::backend::Cve2026_43284Profile;
        using ghostlock::backend::Cve2026_43284Schema;
        using ghostlock::backend::cve_2026_43284::lkm::kSelinuxExecContextVendorModprobe;

        /* Frozen pre-R1 constants: any drift fails here. */
        constexpr std::uint16_t kKmi515 = 5U * 1000U + 15U;
        static_assert(kKmi515 == 5015U);
        static_assert(kSelinuxExecContextVendorModprobe == 0U);
        assert(ghostlock::backend::kCve2026_43284SelinuxDefault == 0U);
        assert(ghostlock::backend::kCve2026_43284WaitTimeoutDefaultMs == 15000U);
        assert(ghostlock::backend::kCve2026_43284ModulePollAttemptsDefault == 40U);
        assert(ghostlock::backend::kCve2026_43284ModulePollIntervalMsDefault == 5U);
        assert(ghostlock::backend::kCve2026_43284LateLoadArgsDefault == 0U);
        assert(ghostlock::backend::kCve2026_43284LkmPathConvention ==
               "$GHOSTLOCK_HOME/helper.ko");
        assert(ghostlock::backend::kCve2026_43284CarrierConvention ==
               "device-first-present-default");

        /* Root-package convention (was lkm_image default_root_package). */
        using ghostlock::contract::RootProgramKind;
        static_assert(ghostlock::backend::kCve2026_43284RootPackageKernelSU ==
                      "me.weishu.kernelsu");
        assert(ghostlock::backend::root_package_convention(
                       RootProgramKind::KernelSU) == "me.weishu.kernelsu");
        assert(ghostlock::backend::root_package_convention(
                       RootProgramKind::FolkPatch).empty());
        assert(ghostlock::backend::root_package_convention(
                       RootProgramKind::Custom).empty());

        /* Absent fields take exactly the pre-R1 effective values. */
        Document doc;
        doc.release = "5.15.202-android13-8-g51bba4309aac";
        Cve2026_43284Profile view{};
        LogState log{};
        auto sink = make_sink(view);
        sink.log = BindLog{&log_emit, &log};
        const auto status = ghostlock::profile::bind_all(
                make_registry<Cve2026_43284Schema>(), doc, sink,
                BindMode::Production);
        assert(status.ok());
        assert(view.kmi.has_value() && view.kmi.value() == kKmi515);
        assert(view.selinux_exec_context.has_value() &&
               view.selinux_exec_context.value() == 0U);
        assert(view.late_load_args.has_value() && view.late_load_args.value() == 0U);
        /* Both String conventions are declaration-only (the composition root
         * owns the actual path); only the numeric defaults are stored. */
        assert(!view.lkm_path.has_value());
        assert(!view.carrier_path.has_value());
        assert(!view.defex_symbol.has_value());
        assert(!view.steps.has_value());
        assert(view.wait_timeout_ms.has_value() &&
               view.wait_timeout_ms.value() == 15000U);
        assert(view.module_poll_attempts.has_value() &&
               view.module_poll_attempts.value() == 40U);
        assert(view.module_poll_interval_ms.has_value() &&
               view.module_poll_interval_ms.value() == 5U);
        /* Eight defaults: carrier + lkm_path conventions, kmi, selinux,
         * late_load_args and the three tuning literals. */
        assert(log.count == 8);

        /* The derived kmi equals the release parser's own value. */
        ghostlock::backend::cve_2026_43284::lkm::KernelRelease parsed{};
        assert(ghostlock::backend::cve_2026_43284::lkm::parse_kernel_release(
                doc.release, parsed));
        assert(parsed.kmi == view.kmi.value());

        /* An explicit profile value still wins over the default. */
        Document explicit_doc;
        explicit_doc.release = "6.6.77-android15";
        add(explicit_doc, ghostlock::backend::kCve2026_43284Section, "kmi", 6006);
        add(explicit_doc, ghostlock::backend::kCve2026_43284Section,
            "selinux_exec_context", 1);
        add(explicit_doc, ghostlock::backend::kCve2026_43284Section, "late_load_args",
            2);
        ghostlock::profile::Section *explicit_section = explicit_doc.find_section(
                ghostlock::backend::kCve2026_43284Section);
        if (explicit_section == nullptr) {
            explicit_section = &explicit_doc.append_section(
                    ghostlock::backend::kCve2026_43284Section);
        }
        explicit_section->add_text("lkm_path", "/data/local/tmp/custom.ko");
        Cve2026_43284Profile explicit_view{};
        LogState explicit_log{};
        auto explicit_sink = make_sink(explicit_view);
        explicit_sink.log = BindLog{&log_emit, &explicit_log};
        assert(ghostlock::profile::bind_all(
                       make_registry<Cve2026_43284Schema>(), explicit_doc,
                       explicit_sink, BindMode::Production)
                       .ok());
        assert(explicit_view.kmi.value() == 6006U);
        assert(explicit_view.selinux_exec_context.value() == 1U);
        assert(explicit_view.late_load_args.value() == 2U);
        assert(explicit_view.lkm_path.value() == "/data/local/tmp/custom.ko");
        /* Four declarations remain: the carrier convention plus the three
         * tuning literals (lkm_path/kmi/selinux/late_load_args are explicit). */
        assert(explicit_log.count == 4);
        assert(explicit_log.last ==
               "backend.cve_2026_43284.module_poll_interval_ms");
    }

    void test_selection_registry() {
        using ghostlock::contract::BackendKind;
        using ghostlock::contract::ComponentSelection;
        using ghostlock::contract::StepSetKind;
        using ghostlock::contract::TerminalKind;
        using ghostlock::profile::RegistryForSelectionT;

        using R84 = RegistryForSelectionT<ComponentSelection{
                BackendKind::Cve2026_43284, StepSetKind::PageCacheWrite,
                TerminalKind::UmhForward}>;
        static_assert(R84::count == 1);
        static_assert(R84::count ==
                      SchemaRegistry<ghostlock::backend::Cve2026_43284Schema>::count);

        using R49 = RegistryForSelectionT<ComponentSelection{
                BackendKind::Cve2026_43499, StepSetKind::W1W3,
                TerminalKind::RootChild}>;
        static_assert(R49::count == 2);
        static_assert(R49::count ==
                      SchemaRegistry<ghostlock::platform::abi::Schema,
                                     ghostlock::backend::Cve2026_43499Schema>::count);
    }
} // namespace

int main() {
    test_required_and_defaults();
    test_43284_pre_r1_values();
    test_selection_registry();
    std::printf("profile_registry_test: ok\n");
    return 0;
}
