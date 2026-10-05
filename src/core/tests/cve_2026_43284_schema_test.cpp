/* Host test for the cve_2026_43284 owner Schema and its private wire section
 * (A2-3c / S3 B4; stringified + handshake params S4 R4).
 *
 * Covers: the declared (section, key) ownership is unique and all inside the
 * 43284 private section; a Production bind accepts every declared optional key
 * and preserves presence; Production rejects an unknown key/width and an
 * undeclared section; and the S4 R4 String path semantics: a UTF-8 value lands
 * on the View as a view, a >256-byte value and a numeric value on a String field
 * both fail closed, and the handshake defaults (15000 / 40 / 5) apply through
 * the registry bind with a default_used diagnostic while the two path
 * conventions stay declaration-only. */

#include "backend/cve_2026_43284/schema.hpp"
#include "backend/cve_2026_43499/schema.hpp"
#include "profile/document.hpp"
#include "profile/schema.hpp"

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <string>
#include <string_view>

using ghostlock::backend::Cve2026_43284Profile;
using ghostlock::backend::Cve2026_43284Schema;
using ghostlock::profile::BindCode;
using ghostlock::profile::BindMode;
using ghostlock::profile::DecodeMode;
using ghostlock::profile::Document;

namespace {
    void add(Document &doc, std::string_view section, std::string_view key,
             uint64_t raw) {
        ghostlock::profile::Section *found = doc.find_section(section);
        if (!found) found = &doc.append_section(section);
        found->add(key, raw);
    }

    void add_text(Document &doc, std::string_view section, std::string_view key,
                  std::string_view text) {
        ghostlock::profile::Section *found = doc.find_section(section);
        if (!found) found = &doc.append_section(section);
        found->add_text(key, text);
    }

    constexpr std::string_view kCarrier = "/vendor/lib64/libstagefrighthw.so";
    constexpr std::string_view kLkm = "/data/local/tmp/helper_custom.ko";
    constexpr std::string_view kDefex = "vendor_defex_hook";

    Document full_document() {
        Document doc;
        doc.release = "6.6.77-43284-schema";
        doc.backend = 6;
        doc.middleware = ghostlock::profile::kRouteNone;
        for (const auto &field : Cve2026_43284Schema::kFields) {
            if (field.wire == ghostlock::profile::WireKind::String) continue;
            add(doc, field.section, field.key, 1ULL);
        }
        add_text(doc, ghostlock::backend::kCve2026_43284Section, "carrier_path",
                 kCarrier);
        add_text(doc, ghostlock::backend::kCve2026_43284Section, "lkm_path", kLkm);
        add_text(doc, ghostlock::backend::kCve2026_43284Section, "defex_symbol",
                 kDefex);
        add_text(doc, ghostlock::backend::kCve2026_43284Section, "steps", "umh");
        return doc;
    }

    struct LogState {
        int count = 0;
        std::string last;
    };

    void log_emit(void *ctx, std::string_view section, std::string_view key,
                  ghostlock::profile::FieldSource) noexcept {
        auto *state = static_cast<LogState *>(ctx);
        state->count += 1;
        state->last.assign(section);
        state->last.push_back('.');
        state->last.append(key);
    }
} // namespace

int main() {
    constexpr size_t kFieldCount = std::size(Cve2026_43284Schema::kFields);

    /* ---- Ownership: unique (section, key), all in the 43284 section. ---- */
    for (size_t i = 0; i < kFieldCount; i++) {
        assert(Cve2026_43284Schema::kFields[i].section ==
               ghostlock::backend::kCve2026_43284Section);
        for (size_t j = i + 1; j < kFieldCount; j++) {
            const bool same =
                    Cve2026_43284Schema::kFields[i].section ==
                            Cve2026_43284Schema::kFields[j].section &&
                    Cve2026_43284Schema::kFields[i].key ==
                            Cve2026_43284Schema::kFields[j].key;
            assert(!same);
        }
    }
    /* Every field is optional: presence is the only requiredness. */
    for (size_t i = 0; i < kFieldCount; i++) {
        assert(Cve2026_43284Schema::kFields[i].required == false);
    }

    /* ---- Production accepts every declared key and preserves presence. ---- */
    {
        Document doc = full_document();
        Cve2026_43284Profile view{};
        const auto status = ghostlock::profile::bind<Cve2026_43284Schema>(
                doc, view, DecodeMode::Production);
        assert(status.ok());
        assert(view.carrier_path == kCarrier);
        assert(view.lkm_path == kLkm);
        assert(view.defex_symbol == kDefex);
        assert(view.kmi == 1);
        assert(view.selinux_exec_context == 1);
        assert(view.late_load_args == 1);
        /* The "umh" token derives the internal PageCacheWrite id. */
        assert(view.steps == 3);
        assert(view.wait_timeout_ms == 1);
        assert(view.module_poll_attempts == 1);
        assert(view.module_poll_interval_ms == 1);

        /* An empty document binds to all-absent (no required field). */
        Document empty;
        Cve2026_43284Profile none{};
        assert(ghostlock::profile::bind<Cve2026_43284Schema>(
                       empty, none, DecodeMode::Production)
                       .ok());
        assert(!none.carrier_path.has_value());
        assert(!none.lkm_path.has_value());
        assert(!none.defex_symbol.has_value());
        assert(!none.steps.has_value());
    }

    /* ---- S4 R4 registry defaults: the three tuning fields, reported once. ---- */
    {
        Document doc;
        doc.release = "6.6.77-43284-defaults";
        Cve2026_43284Profile view{};
        LogState log{};
        auto sink = ghostlock::profile::make_sink(view);
        sink.log = ghostlock::profile::BindLog{&log_emit, &log};
        const auto status = ghostlock::profile::bind_all(
                ghostlock::profile::make_registry<Cve2026_43284Schema>(), doc, sink,
                BindMode::Production);
        assert(status.ok());
        assert(view.wait_timeout_ms.has_value());
        assert(view.wait_timeout_ms.value() == 15000U);
        assert(view.module_poll_attempts.has_value());
        assert(view.module_poll_attempts.value() == 40U);
        assert(view.module_poll_interval_ms.has_value());
        assert(view.module_poll_interval_ms.value() == 5U);
        /* carrier_path + lkm_path conventions + kmi derived + selinux +
         * late_load_args + the three tuning literals = 8 declarations. */
        assert(log.count == 8);
        /* The two String conventions are declaration-only: no value stored. */
        assert(!view.carrier_path.has_value());
        assert(!view.lkm_path.has_value());
    }

    /* ---- S4 R4 String semantics: a view, and fail-closed over-limit. ---- */
    {
        Document doc;
        std::string huge(ghostlock::profile::kMaxStringBytes + 1U, 'a');
        add_text(doc, ghostlock::backend::kCve2026_43284Section, "carrier_path",
                 huge);
        Cve2026_43284Profile view{};
        const auto blocked = ghostlock::profile::bind<Cve2026_43284Schema>(
                doc, view, DecodeMode::Production);
        assert(blocked.code == BindCode::WidthMismatch);

        /* A numeric value on a String field is a type mismatch, fail closed. */
        Document numeric;
        add(numeric, ghostlock::backend::kCve2026_43284Section, "lkm_path", 7U);
        Cve2026_43284Profile view2{};
        assert(ghostlock::profile::bind<Cve2026_43284Schema>(
                       numeric, view2, DecodeMode::Production)
                       .code == BindCode::WidthMismatch);

        /* A text value on a numeric field is rejected too. */
        Document text_numeric;
        add_text(text_numeric, ghostlock::backend::kCve2026_43284Section, "kmi",
                 "5015");
        Cve2026_43284Profile view3{};
        assert(ghostlock::profile::bind<Cve2026_43284Schema>(
                       text_numeric, view3, DecodeMode::Production)
                       .code == BindCode::WidthMismatch);
    }

    /* ---- Production rejects an unknown key in the owned section. ---- */
    {
        Document doc = full_document();
        doc.find_section(ghostlock::backend::kCve2026_43284Section)
                ->add("bogus", 1ULL);
        Cve2026_43284Profile view{};
        const auto blocked = ghostlock::profile::bind<Cve2026_43284Schema>(
                doc, view, DecodeMode::Production);
        assert(blocked.code == BindCode::UnknownKey);
        assert(blocked.section == ghostlock::backend::kCve2026_43284Section);
        assert(blocked.key == "bogus");
    }

    /* ---- Production rejects a section this owner does not declare. ---- */
    {
        Document doc = full_document();
        doc.append_section("not_a_section").add("x", 1ULL);
        Cve2026_43284Profile view{};
        const auto blocked = ghostlock::profile::bind<Cve2026_43284Schema>(
                doc, view, DecodeMode::Production);
        assert(blocked.code == BindCode::UnknownSection);
    }

    /* ---- Width mismatch fails closed. ---- */
    {
        Document doc;
        doc.append_section(ghostlock::backend::kCve2026_43284Section)
                .add("kmi", 0x10000ULL);
        Cve2026_43284Profile view{};
        const auto blocked = ghostlock::profile::bind<Cve2026_43284Schema>(
                doc, view, DecodeMode::Production);
        assert(blocked.code == BindCode::WidthMismatch);
    }

    std::printf("cve_2026_43284_schema_test: ok (%zu fields)\n", kFieldCount);
    return 0;
}
