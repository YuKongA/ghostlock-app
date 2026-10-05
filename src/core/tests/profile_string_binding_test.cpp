/* S4 R4 host test: WireKind::String end to end.
 *
 * Covers the two native paths a String policy field travels:
 *   1. schema-driven codec: a Str section value decodes as WireType::Str, and a
 *      value longer than the 256-byte bound is rejected fail-closed;
 *   2. neutral framing + owner bind: encode -> frame_v3 -> profile::Document
 *      preserves the text as a non-owning view, and the 43284 owner bind
 *      materialises it into the View as std::string_view with the declared
 *      defaults for the handshake fields.
 *
 * The test itself owns the encoded buffer for the whole bind, which is the
 * lifetime contract the production entry (ReadResult::storage) upholds. */

#include "backend/cve_2026_43284/glkv3_schema.hpp"
#include "backend/cve_2026_43284/schema.hpp"
#include "profile/document.hpp"
#include "profile/glkv3.hpp"
#include "profile/glkv3_parse.hpp"
#include "profile/schema.hpp"

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using ghostlock::backend::Cve2026_43284Profile;
using ghostlock::backend::Cve2026_43284Schema;

namespace {
    ghostlock::profile::glkv3::Value str_value(std::string_view text) {
        ghostlock::profile::glkv3::Value value;
        value.type = ghostlock::profile::glkv3::WireType::Str;
        value.bytes = text;
        return value;
    }

    ghostlock::profile::glkv3::Value uint_value(uint64_t raw) {
        ghostlock::profile::glkv3::Value value;
        value.type = ghostlock::profile::glkv3::WireType::UInt;
        value.uint_value = raw;
        return value;
    }

    void test_schema_driven_string_and_bound() {
        constexpr std::string_view kCarrier = "/vendor/lib64/libstagefrighthw.so";
        {
            ghostlock::profile::glkv3::Document doc;
            doc.schema = ghostlock::profile::glkv3::kSchemaVersion;
            ghostlock::profile::glkv3::Section &section =
                    doc.append_section("backend.cve_2026_43284");
            section.entries.push_back(
                    ghostlock::profile::glkv3::Entry{"carrier_path", str_value(kCarrier)});
            section.entries.push_back(
                    ghostlock::profile::glkv3::Entry{"kmi", uint_value(5015U)});
            const std::string encoded = ghostlock::profile::glkv3::encode(doc);
            assert(!encoded.empty());

            ghostlock::profile::glkv3::Document decoded;
            const auto status = ghostlock::profile::glkv3::decode(
                    encoded, ghostlock::backend::kCve2026_43284Glkv3Schema, decoded,
                    ghostlock::profile::glkv3::DecodeMode::Production);
            assert(status.ok());
            const auto *carrier = decoded.find("backend.cve_2026_43284", "carrier_path");
            assert(carrier != nullptr);
            assert(carrier->type == ghostlock::profile::glkv3::WireType::Str);
            assert(carrier->bytes == kCarrier);
        }
        {
            /* 257 UTF-8 bytes exceeds the wire bound: rejected whole. */
            std::string huge(ghostlock::profile::glkv3::kMaxStringBytes + 1U, 'a');
            ghostlock::profile::glkv3::Document doc;
            doc.schema = ghostlock::profile::glkv3::kSchemaVersion;
            ghostlock::profile::glkv3::Section &section =
                    doc.append_section("backend.cve_2026_43284");
            section.entries.push_back(
                    ghostlock::profile::glkv3::Entry{"lkm_path", str_value(huge)});
            const std::string encoded = ghostlock::profile::glkv3::encode(doc);
            assert(!encoded.empty());

            ghostlock::profile::glkv3::Document decoded;
            const auto status = ghostlock::profile::glkv3::decode(
                    encoded, ghostlock::backend::kCve2026_43284Glkv3Schema, decoded,
                    ghostlock::profile::glkv3::DecodeMode::Production);
            assert(!status.ok());
            assert(status.code == ghostlock::profile::glkv3::DecodeCode::TypeMismatch);
        }
    }

    void test_neutral_frame_then_owner_bind() {
        constexpr std::string_view kCarrier = "/vendor/lib64/libbinderdebug.so";
        constexpr std::string_view kLkm = "/data/local/tmp/helper_custom.ko";

        ghostlock::profile::glkv3::Document doc;
        doc.schema = ghostlock::profile::glkv3::kSchemaVersion;
        doc.has_release = true;
        doc.release = "5.15.202-android14-8-gabc";
        doc.has_terminal = true;
        doc.terminal = "umh_forward";
        doc.has_backend = true;
        doc.backend = "cve_2026_43284";
        ghostlock::profile::glkv3::Section &section =
                doc.append_section("backend.cve_2026_43284");
        section.entries.push_back(
                ghostlock::profile::glkv3::Entry{"carrier_path", str_value(kCarrier)});
        section.entries.push_back(
                ghostlock::profile::glkv3::Entry{"lkm_path", str_value(kLkm)});
        section.entries.push_back(
                ghostlock::profile::glkv3::Entry{"steps", str_value("umh")});
        const std::string encoded = ghostlock::profile::glkv3::encode(doc);
        assert(!encoded.empty());

        ghostlock::profile::Document framed;
        assert(ghostlock::profile::frame_v3(encoded, &framed) == 0);
        const ghostlock::profile::Value *carrier = framed.find_value(
                ghostlock::backend::kCve2026_43284Section, "carrier_path");
        assert(carrier != nullptr && carrier->present && carrier->is_text);
        assert(carrier->text == kCarrier);

        Cve2026_43284Profile view{};
        auto sink = ghostlock::profile::make_sink(view);
        const auto status = ghostlock::profile::bind_all(
                ghostlock::profile::make_registry<Cve2026_43284Schema>(), framed, sink,
                ghostlock::profile::BindMode::Production);
        assert(status.ok());
        assert(view.carrier_path.has_value() && view.carrier_path.value() == kCarrier);
        assert(view.lkm_path.has_value() && view.lkm_path.value() == kLkm);
        /* The auto-derived kmi and the handshake defaults still apply. */
        assert(view.kmi.has_value() && view.kmi.value() == 5015U);
        assert(view.wait_timeout_ms.has_value() &&
               view.wait_timeout_ms.value() == 15000U);
        assert(view.module_poll_attempts.has_value() &&
               view.module_poll_attempts.value() == 40U);
        assert(view.module_poll_interval_ms.has_value() &&
               view.module_poll_interval_ms.value() == 5U);
    }
} // namespace

int main() {
    test_schema_driven_string_and_bound();
    test_neutral_frame_then_owner_bind();
    std::puts("profile_string_binding_test: OK");
    return 0;
}
