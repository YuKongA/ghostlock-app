/* Host test for the GLKv3 owner path -> type declarations (GLKv3-3).
 *
 * Covers, for both backend owners:
 *   - every GLKv3 FieldSpec list has unique (section, key) pairs;
 *   - the GLKv3 (section, key) set equals the v2 owner Schema set (no drift
 *     between profile/schema.hpp and glkv3_schema.hpp);
 *   - the declared WireType is the semantic mapping of the v2 field
 *     (bool flags -> Bool, signed geometry -> Int, otherwise UInt);
 *   - every field is optional (presence is key occurrence), like v2;
 *   - a fully populated 43499 profile document round-trips through the codec
 *     with a combined root + owner Schema, and its canonical encoding is the
 *     same golden the Kotlin adapter test asserts.
 *
 * When run, the canonical hex of that profile document is printed on the
 * "glkv3_profile_hex:" line so the Kotlin golden can be regenerated. */

#include "backend/cve_2026_43284/glkv3_schema.hpp"
#include "backend/cve_2026_43284/schema.hpp"
#include "backend/cve_2026_43499/glkv3_schema.hpp"
#include "backend/cve_2026_43499/schema.hpp"
#include "platform/abi.hpp"
#include "profile/glkv3.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using ghostlock::backend::Cve2026_43284Schema;
using ghostlock::backend::Cve2026_43499Schema;
using ghostlock::backend::kCve2026_43284Glkv3Fields;
using ghostlock::backend::kCve2026_43499Glkv3Fields;
using ghostlock::platform::abi::kPlatformAbiGlkv3Fields;
using ghostlock::profile::glkv3::DecodeCode;
using ghostlock::profile::glkv3::DecodeMode;
using ghostlock::profile::glkv3::DecodeStatus;
using ghostlock::profile::glkv3::Document;
using ghostlock::profile::glkv3::Entry;
using ghostlock::profile::glkv3::FieldSpec;
using ghostlock::profile::glkv3::kSchemaVersion;
using ghostlock::profile::glkv3::Schema;
using ghostlock::profile::glkv3::Section;
using ghostlock::profile::glkv3::Value;
using ghostlock::profile::glkv3::WireType;

namespace {
    std::string key_of(std::string_view section, std::string_view key) {
        std::string out(section);
        out.push_back('\t');
        out.append(key);
        return out;
    }

    bool is_bool_field(std::string_view key) {
        return key == "safe_mode" || key == "vr_guard" || key == "compact_waiter";
    }

    template<typename V2Field>
    WireType expected_wire_type(const V2Field &field) {
        /* S4 R4: a String owner field maps to the str wire type. */
        if (field.wire == ghostlock::profile::WireKind::String) return WireType::Str;
        if (is_bool_field(field.key)) return WireType::Bool;
        if (field.is_signed) return WireType::Int;
        return WireType::UInt;
    }

    /* Unique keys and v2 key-set equality for one owner. */
    template<typename V2Schema>
    void check_owner(const FieldSpec *fields, size_t count, std::string_view owner) {
        std::set<std::string> v3_keys;
        for (size_t i = 0; i < count; i++) {
            assert(fields[i].section != std::string_view{});
            assert(!fields[i].required);
            const std::string key = key_of(fields[i].section, fields[i].key);
            assert(v3_keys.insert(key).second);
            for (size_t j = i + 1; j < count; j++) {
                const bool same = fields[i].section == fields[j].section &&
                                  fields[i].key == fields[j].key;
                assert(!same);
            }
        }

        std::set<std::string> v2_keys;
        for (const auto &field : V2Schema::kFields) {
            v2_keys.insert(key_of(field.section, field.key));
            const auto *match = [&]() -> const FieldSpec * {
                for (size_t i = 0; i < count; i++) {
                    if (fields[i].section == field.section && fields[i].key == field.key) {
                        return &fields[i];
                    }
                }
                return nullptr;
            }();
            assert(match != nullptr);
            assert(match->type == expected_wire_type(field));
        }
        assert(v2_keys == v3_keys);
        std::printf("glkv3_schema_test: %.*s ok (%zu fields)\n",
                    static_cast<int>(owner.size()), owner.data(), count);
    }

    bool is_route_section(std::string_view section) {
        return section.starts_with("backend.cve_2026_43499.route.");
    }

    constexpr std::string_view kActiveRoute =
            "backend.cve_2026_43499.route.multicast_waiter";
    constexpr std::string_view kRouteToken = "multicast_waiter";

    Value value_for(const FieldSpec &field) {
        const std::string_view s = field.section;
        const std::string_view k = field.key;
        const auto uint_value = [&](uint64_t value) {
            Value v;
            v.type = WireType::UInt;
            v.uint_value = value;
            return v;
        };
        const auto int_value = [&](int64_t value) {
            Value v;
            v.type = WireType::Int;
            v.int_value = value;
            return v;
        };
        const auto bool_value = [&](bool value) {
            Value v;
            v.type = WireType::Bool;
            v.bool_value = value;
            return v;
        };

        if (s == "common" && k == "kernel_major") return uint_value(5);
        if (s == "common" && k == "fallback_route") return uint_value(2);
        if (s == "common" && k == "safe_mode") return bool_value(true);
        if (s == "common" && k == "vr_guard") return bool_value(true);
        if (s == "platform.abi.task_struct" && k == "prio") return uint_value(101);
        if (s == "backend.cve_2026_43499.cred" && k == "caps_value")
            return uint_value(0x123456789abcdef0ULL);
        if (s == "backend.cve_2026_43499.cred" && k == "ref0_image")
            return uint_value(0x1111111111111111ULL);
        if (s == "platform.abi.offset" && k == "init_task") return uint_value(34677760);
        if (s == "backend.cve_2026_43499.offset" && k == "vr_sys_exit_tp")
            return uint_value(0x2a);
        if (s == "platform.abi.kernel" && k == "kernel_phys_load") return uint_value(0xb000);
        if (s == "platform.abi.kernel" && k == "kernel_phys_offset")
            return uint_value(0xc000);
        if (s == "backend.cve_2026_43499.kernel" && k == "compact_waiter")
            return bool_value(true);
        if (s == "backend.cve_2026_43499.kernel" && k == "kernelsnitch_collisions")
            return uint_value(7);
        if (s == "backend.cve_2026_43499.kernel" && k == "mm_struct_sz")
            return uint_value(0x400);
        if (s == "countermeasure.vivo_vr_guard" && k == "tracepoint_funcs")
            return uint_value(0x20);
        if (s == "backend.cve_2026_43499" && k == "steps") return uint_value(2);
        if (s == kActiveRoute && k == "waiter_off") return int_value(-2);
        if (s == kActiveRoute && k == "buffer_size") return uint_value(512);
        if (s == kActiveRoute && k == "task_offset") return uint_value(0x30);
        if (s == kActiveRoute && k == "lock_offset") return uint_value(0x40);
        if (s == kActiveRoute && k == "attempts") return uint_value(3);
        if (s == kActiveRoute && k == "arm_sequence") return uint_value(4);
        if (s == kActiveRoute && k == "arm_hold") return uint_value(20000);

        switch (field.type) {
            case WireType::Int: return int_value(0);
            case WireType::Bool: return bool_value(false);
            default: return uint_value(0);
        }
    }

    /* The Kotlin adapter fixture: every non-route 43499 field present, plus the
     * active route's seven fields, with a small distinctive value set. */
    Document profile_document() {
        Document doc;
        doc.schema = kSchemaVersion;
        doc.has_release = true;
        doc.release = "5.15.189-android13-8-00016-g51bba4309aac";
        doc.has_terminal = true;
        doc.terminal = "root_child";
        doc.has_backend = true;
        doc.backend = "cve_2026_43499";
        doc.has_route = true;
        doc.route = kRouteToken;

        for (const FieldSpec &field : kPlatformAbiGlkv3Fields) {
            Section *section = doc.find_section(field.section);
            if (section == nullptr) section = &doc.append_section(field.section);
            section->entries.push_back(Entry{field.key, value_for(field)});
        }
        for (const FieldSpec &field : kCve2026_43499Glkv3Fields) {
            if (is_route_section(field.section) && field.section != kActiveRoute) {
                continue;
            }
            Section *section = doc.find_section(field.section);
            if (section == nullptr) section = &doc.append_section(field.section);
            section->entries.push_back(Entry{field.key, value_for(field)});
        }
        return doc;
    }

    std::string hex(const std::string &bytes) {
        static const char *digits = "0123456789abcdef";
        std::string out;
        out.reserve(bytes.size() * 2);
        for (const char raw : bytes) {
            const auto byte = static_cast<unsigned char>(raw);
            out.push_back(digits[byte >> 4]);
            out.push_back(digits[byte & 0x0f]);
        }
        return out;
    }

    /* Root fields are special-cased by both codec directions; a full document
     * Schema is the root declaration plus one owner's section fields. */
    constexpr FieldSpec kRootFields[] = {
        {"", "schema", WireType::UInt, true},
        {"", "release", WireType::Str, true},
        {"", "terminal", WireType::Str, true},
        {"", "backend", WireType::Str, true},
        {"", "route", WireType::Str, true},
    };
} // namespace

int main() {
    check_owner<ghostlock::platform::abi::Schema>(
            kPlatformAbiGlkv3Fields, std::size(kPlatformAbiGlkv3Fields),
            "platform::abi");
    check_owner<Cve2026_43499Schema>(kCve2026_43499Glkv3Fields,
                                     std::size(kCve2026_43499Glkv3Fields),
                                     "cve_2026_43499");
    check_owner<Cve2026_43284Schema>(kCve2026_43284Glkv3Fields,
                                     std::size(kCve2026_43284Glkv3Fields),
                                     "cve_2026_43284");

    /* ---- Canonical encoding + schema-driven decode of a full document. ---- */
    {
        const Document original = profile_document();
        const std::string encoded = encode(original);
        assert(!encoded.empty());

        std::vector<FieldSpec> combined(std::begin(kRootFields),
                                        std::end(kRootFields));
        combined.insert(combined.end(), std::begin(kPlatformAbiGlkv3Fields),
                        std::end(kPlatformAbiGlkv3Fields));
        combined.insert(combined.end(), std::begin(kCve2026_43499Glkv3Fields),
                        std::end(kCve2026_43499Glkv3Fields));
        const Schema schema{combined};
        Document decoded;
        const DecodeStatus status =
                decode(encoded, schema, decoded, DecodeMode::Production);
        if (!status.ok()) {
            std::fprintf(stderr, "decode failed: code=%s section='%.*s' key='%.*s'\n",
                         std::string(ghostlock::profile::glkv3::decode_code_name(
                                             status.code))
                                 .c_str(),
                         static_cast<int>(status.section.size()),
                         status.section.data(),
                         static_cast<int>(status.key.size()), status.key.data());
        }
        assert(status.ok());

        assert(decoded.has_release && decoded.release == original.release);
        assert(decoded.has_terminal && decoded.terminal == original.terminal);
        assert(decoded.has_backend && decoded.backend == original.backend);
        assert(decoded.has_route && decoded.route == original.route);

        const Value *safe = decoded.find("common", "safe_mode");
        assert(safe != nullptr && safe->type == WireType::Bool && safe->bool_value);
        const Value *shift = decoded.find(
                "backend.cve_2026_43499.route.multicast_waiter", "waiter_off");
        assert(shift != nullptr && shift->type == WireType::Int && shift->int_value == -2);
        const Value *major = decoded.find("common", "kernel_major");
        assert(major != nullptr && major->type == WireType::UInt && major->uint_value == 5);

        const std::string canonical = hex(encoded);
        std::printf("glkv3_profile_hex: %s\n", canonical.c_str());

        assert(encode(decoded) == encoded);
    }

    std::printf("glkv3_schema_test: ok\n");
    return 0;
}
