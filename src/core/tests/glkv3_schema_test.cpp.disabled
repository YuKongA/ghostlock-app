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
 * Native is the single authority for that fixture's canonical bytes: the bare
 * run prints them on the "glkv3_profile_hex:" line, and the two export modes
 * below produce the artifacts the Kotlin cross-language test consumes --
 * regenerated here, never recomputed on the Kotlin side (that would degrade the
 * byte comparison to self-proof):
 *   make -C src glkv3-golden-hex      -> the canonical hex, one line
 *   make -C src glkv3-golden-fixture  -> the fixture as path/wire/value rows,
 *                                        so Kotlin aligns the same logical
 *                                        document field by field. */

#include "backend/cve_2026_43284/glkv3_schema.hpp"
#include "backend/cve_2026_43284/schema.hpp"
#include "backend/cve_2026_43499/glkv3_schema.hpp"
#include "backend/cve_2026_43499/schema.hpp"
#include "platform/abi.hpp"
#include "profile/glkv3.hpp"

#include <algorithm>
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
        return key == "safe_mode" || key == "compact_waiter";
    }

    template<typename V2Field>
    WireType expected_wire_type(const V2Field &field) {
        /* S4 R4: a String owner field maps to the str wire type; M2 adds the
         * declared composite (array of map) and the explicit bool fields. */
        if (field.wire == ghostlock::profile::WireKind::String) return WireType::Str;
        if (field.wire == ghostlock::profile::WireKind::Array) return WireType::Array;
        if (field.wire == ghostlock::profile::WireKind::Bool) return WireType::Bool;
        if (is_bool_field(field.key)) return WireType::Bool;
        if (field.is_signed) return WireType::Int;
        return WireType::UInt;
    }

    /* Unique keys and v2 key-set equality for one owner. */
    template<typename V2Schema>
    void check_owner(const FieldSpec *fields, size_t count, std::string_view owner) {
        std::set<std::string> v3_keys;
        for (size_t i = 0; i < count; i++) {
            /* An empty section is the document root (the HOCON-refactor
             * kernel_major / kernel_minor / safe_mode scalars); every other
             * field names the owner section it belongs to. */
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
        const auto str_value = [&](std::string_view value) {
            Value v;
            v.type = WireType::Str;
            v.bytes = value;
            return v;
        };

        if (s == "backend.cve_2026_43499.abi.task_struct" && k == "prio") return uint_value(101);
        if (s == "backend.cve_2026_43499.cred" && k == "caps_value")
            return uint_value(0x123456789abcdef0ULL);
        if (s == "backend.cve_2026_43499.cred" && k == "ref0_image")
            return uint_value(0x1111111111111111ULL);
        if (s == "backend.cve_2026_43499.abi.offset" && k == "init_task") return uint_value(34677760);
        if (s == "backend.cve_2026_43499.offset" && k == "vr_sys_exit_tp")
            return uint_value(0x2a);
        if (s == "backend.cve_2026_43499.abi.kernel" && k == "kernel_phys_load") return uint_value(0xb000);
        if (s == "backend.cve_2026_43499.abi.kernel" && k == "kernel_phys_offset")
            return uint_value(0xc000);
        if (s == "backend.cve_2026_43499.kernel" && k == "compact_waiter")
            return bool_value(true);
        if (s == "backend.cve_2026_43499.kernel" && k == "kernelsnitch_collisions")
            return uint_value(7);
        if (s == "backend.cve_2026_43499.kernel" && k == "mm_struct_sz")
            return uint_value(0x400);
        if (s == "backend.cve_2026_43499" && k == "route")
            return str_value("multicast_waiter");
        if (s == "backend.cve_2026_43499" && k == "experimental") return bool_value(true);
        /* M2 declared composite + M5: the fixture carries a queue (array of map)
         * and NO `steps` token - that is exactly what the App emits now, so the
         * golden hex and the field fixture stay byte-comparable with the Kotlin
         * adapter, and the normalised document never grows a removed syntax. */
        if (s == "backend.cve_2026_43499" && k == "queue") {
            Value element;
            element.type = WireType::Map;
            ghostlock::profile::glkv3::MapMember step;
            step.key = "step";
            step.type = WireType::Str;
            step.bytes = "w1";
            element.members.push_back(step);
            Value array;
            array.type = WireType::Array;
            array.elements.push_back(element);
            return array;
        }
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
        /* HOCON refactor root scalars: root keys, not sections (the former
         * "common" owner). */
        doc.has_kernel_major = true;
        doc.kernel_major = 5;
        doc.has_kernel_minor = true;
        doc.kernel_minor = 15;
        doc.has_safe_mode = true;
        doc.safe_mode = true;

        for (const FieldSpec &field : kPlatformAbiGlkv3Fields) {
            Section *section = doc.find_section(field.section);
            if (section == nullptr) section = &doc.append_section(field.section);
            section->entries.push_back(Entry{field.key, value_for(field)});
        }
        for (const FieldSpec &field : kCve2026_43499Glkv3Fields) {
            /* Root scalars are root keys, never sections: encode() writes them
             * beside the component tokens (asserted below). */
            if (field.section.empty()) continue;
            if (is_route_section(field.section) && field.section != kActiveRoute) {
                continue;
            }
            /* M5: the selection is `route` + `queue` and the App emits NO
             * combination token. This fixture mirrors the App document (the
             * golden hex and the field fixture are compared against its adapter),
             * so the schema-recognised `steps` field is deliberately left out: it
             * stays declared so a token can be REFUSED by name, not emitted. */
            if (field.key == "steps" && field.section == "backend.cve_2026_43499") {
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

    /* ---- Reproducible golden export (native is the SSOT) ------------------
     * Kotlin's NativeProfileGlkv3AdapterTest compares its adapter bytes against
     * the canonical hex of THIS fixture, so the hex must be regenerated here
     * whenever the codec or the fixture changes -- never recomputed on the
     * Kotlin side (that would degrade the cross-language check to self-proof).
     * Commands:
     *   make -C src glkv3-golden-hex     # the canonical hex, one line
     *   make -C src glkv3-golden-fixture # this document as path/wire/value rows
     * The dump exists so the Kotlin fixture can be aligned field by field
     * instead of copying the hex. */

    std::string_view wire_name(WireType type) {
        switch (type) {
            case WireType::UInt: return "uint";
            case WireType::Int: return "int";
            case WireType::Bool: return "bool";
            case WireType::Str: return "str";
            case WireType::Bin: return "bin";
            case WireType::Array: return "array";
            case WireType::Map: return "map";
            case WireType::Union: return "union";
        }
        return "unknown";
    }

    std::string value_text(const Value &value) {
        switch (value.type) {
            case WireType::UInt: return std::to_string(value.uint_value);
            case WireType::Int: return std::to_string(value.int_value);
            case WireType::Bool: return value.bool_value ? "true" : "false";
            case WireType::Str: return std::string(value.bytes);
            default: return {};
        }
    }

    /* --dump-fixture: the logical document as path/wire/value rows (root keys
     * first, then the sections and their keys in canonical order). */
    void dump_fixture() {
        const Document doc = profile_document();
        std::printf("# root keys\n");
        std::printf("schema\tuint\t%llu\n",
                    static_cast<unsigned long long>(doc.schema));
        const auto root_text = [](const char *key, std::string_view text) {
            std::printf("%s\tstr\t%.*s\n", key, static_cast<int>(text.size()),
                        text.data());
        };
        if (doc.has_release) root_text("release", doc.release);
        if (doc.has_terminal) root_text("terminal", doc.terminal);
        if (doc.has_backend) root_text("backend", doc.backend);
        if (doc.has_route) root_text("route", doc.route);
        if (doc.has_kernel_major) {
            std::printf("kernel_major\tuint\t%llu\n",
                        static_cast<unsigned long long>(doc.kernel_major));
        }
        if (doc.has_kernel_minor) {
            std::printf("kernel_minor\tuint\t%llu\n",
                        static_cast<unsigned long long>(doc.kernel_minor));
        }
        if (doc.has_safe_mode) {
            std::printf("safe_mode\tbool\t%s\n", doc.safe_mode ? "true" : "false");
        }
        std::printf("# sections\n");
        std::vector<const Section *> sections;
        sections.reserve(doc.sections.size());
        for (const Section &section : doc.sections) sections.push_back(&section);
        std::sort(sections.begin(), sections.end(),
                  [](const Section *left, const Section *right) {
                      return left->name < right->name;
                  });
        for (const Section *section : sections) {
            std::vector<const Entry *> entries;
            entries.reserve(section->entries.size());
            for (const Entry &entry : section->entries) entries.push_back(&entry);
            std::sort(entries.begin(), entries.end(),
                      [](const Entry *left, const Entry *right) {
                          return left->key < right->key;
                      });
            for (const Entry *entry : entries) {
                std::printf("%.*s.%.*s\t%s\t%s\n",
                            static_cast<int>(section->name.size()),
                            section->name.data(),
                            static_cast<int>(entry->key.size()), entry->key.data(),
                            std::string(wire_name(entry->value.type)).c_str(),
                            value_text(entry->value).c_str());
            }
        }
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

/* Guard (M2 residual retirement): the retired vendor guard vocabulary must not
 * come back through a declaration. The exported manifest is generated from these
 * same two tables, so a hit here is a hit in profile-manifest-v3.tsv too. */
template<std::size_t N>
void check_no_retired_vendor_keys(const FieldSpec (&fields)[N], const char *owner) {
    for (std::size_t i = 0; i < N; ++i) {
        const std::string_view section = fields[i].section;
        const std::string_view key = fields[i].key;
        const bool retired_word =
                key.find("vr_guard") != std::string_view::npos ||
                section.find("vr_guard") != std::string_view::npos;
        /* The live offset key vr_sys_exit_tp is the ONLY legal carrier of the
         * "tracepoint" spelling; any other key naming it is a retired layout key. */
        const bool retired_layout =
                key.find("tracepoint_funcs") != std::string_view::npos &&
                key.find("vr_sys_exit_tp") == std::string_view::npos;
        const bool retired = retired_word || retired_layout;
        if (retired) {
            std::fprintf(stderr,
                         "glkv3_schema_test: guard: retired vendor key '%.*s' in "

                         "%.*s\n",
                         static_cast<int>(key.size()), key.data(),
                         static_cast<int>(section.size()), section.data());
            std::abort();
        }
    }
    std::printf("glkv3_schema_test: %s: no retired vendor keys\n", owner);
}

int main(int argc, char **argv) {
    /* Golden-export modes run before the assertions: this test is the native
     * producer for the Kotlin cross-language comparison (header comment). */
    for (int i = 1; i < argc; i++) {
        const std::string_view arg = argv[i];
        if (arg == "--print-golden-hex") {
            std::printf("%s\n", hex(encode(profile_document())).c_str());
            return 0;
        }
        if (arg == "--dump-fixture") {
            dump_fixture();
            return 0;
        }
    }
    check_no_retired_vendor_keys(kPlatformAbiGlkv3Fields, "platform::abi");
    check_no_retired_vendor_keys(kCve2026_43499Glkv3Fields, "cve_2026_43499");
    check_no_retired_vendor_keys(kCve2026_43284Glkv3Fields, "cve_2026_43284");
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

        /* HOCON refactor root scalars: decoded from the document root. */
        assert(decoded.has_kernel_major && decoded.kernel_major == 5);
        assert(decoded.has_kernel_minor && decoded.kernel_minor == 15);
        assert(decoded.has_safe_mode && decoded.safe_mode);
        const Value *shift = decoded.find(
                "backend.cve_2026_43499.route.multicast_waiter", "waiter_off");
        assert(shift != nullptr && shift->type == WireType::Int && shift->int_value == -2);

        const std::string canonical = hex(encoded);
        std::printf("glkv3_profile_hex: %s\n", canonical.c_str());

        assert(encode(decoded) == encoded);
    }

    std::printf("glkv3_schema_test: ok\n");
    return 0;
}
