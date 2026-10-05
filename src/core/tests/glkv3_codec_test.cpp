/* Host test for the GLKv3 MessagePack codec (profile/glkv3).
 *
 * Covers: schema-driven decode, canonical encode stability, shortest integer
 * forms, and the fail-closed rejection vectors (non-map root, missing/wrong
 * schema, unknown section/key, type mismatch, truncation, over-deep, missing
 * required, trailing bytes, oversize). A bounded deterministic fuzz loop
 * mutates valid documents and feeds random bytes; a successful decode must
 * re-encode to a canonical byte-identical document. */

#include "profile/glkv3.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using ghostlock::profile::glkv3::DecodeCode;
using ghostlock::profile::glkv3::DecodeMode;
using ghostlock::profile::glkv3::DecodeStatus;
using ghostlock::profile::glkv3::Document;
using ghostlock::profile::glkv3::Entry;
using ghostlock::profile::glkv3::FieldSpec;
using ghostlock::profile::glkv3::kMaxDocumentBytes;
using ghostlock::profile::glkv3::kSchemaVersion;
using ghostlock::profile::glkv3::Schema;
using ghostlock::profile::glkv3::Section;
using ghostlock::profile::glkv3::Value;
using ghostlock::profile::glkv3::WireType;

namespace {
    constexpr FieldSpec kSchemaFields[] = {
        {"", "schema", WireType::UInt, true},
        {"", "release", WireType::Str, true},
        {"", "terminal", WireType::Str, true},
        {"", "backend", WireType::Str, true},
        {"", "route", WireType::Str, true},
        {"meta", "kernel_major", WireType::UInt, true},
        {"meta", "safe_mode", WireType::Bool, true},
        {"offset", "init_task", WireType::UInt, true},
        {"offset", "slide", WireType::Int, false},
        {"blob", "payload", WireType::Bin, false},
        {"list", "values", WireType::Array, false},
        {"bounded", "text", WireType::Str, false},
    };

    const Schema kSchema{kSchemaFields};

    Value make_uint(uint64_t value) {
        Value out;
        out.type = WireType::UInt;
        out.uint_value = value;
        return out;
    }

    Value make_int(int64_t value) {
        Value out;
        out.type = WireType::Int;
        out.int_value = value;
        return out;
    }

    Value make_bool(bool value) {
        Value out;
        out.type = WireType::Bool;
        out.bool_value = value;
        return out;
    }

    Value make_str(std::string_view value) {
        Value out;
        out.type = WireType::Str;
        out.bytes = value;
        return out;
    }

    Value make_bin(std::string_view value) {
        Value out;
        out.type = WireType::Bin;
        out.bytes = value;
        return out;
    }

    Entry entry(std::string_view key, Value value) {
        return Entry{key, std::move(value)};
    }

    Document sample_document() {
        Document doc;
        doc.schema = kSchemaVersion;
        doc.has_release = true;
        doc.release = "5.15.189-android13-8-00016-g51bba4309aac";
        doc.has_terminal = true;
        doc.terminal = "root_child";
        doc.has_backend = true;
        doc.backend = "cve_2026_43499";
        doc.has_route = true;
        doc.route = "multicast_waiter";

        Section &meta = doc.append_section("meta");
        meta.entries.push_back(entry("kernel_major", make_uint(5)));
        meta.entries.push_back(entry("safe_mode", make_bool(false)));

        Section &offset = doc.append_section("offset");
        offset.entries.push_back(entry("init_task", make_uint(34677760)));
        offset.entries.push_back(entry("slide", make_int(-2)));

        Section &blob = doc.append_section("blob");
        blob.entries.push_back(
                entry("payload", make_bin(std::string_view("\x01\x02\x03", 3))));

        Section &list = doc.append_section("list");
        Value array;
        array.type = WireType::Array;
        array.elements.push_back(make_uint(300));
        array.elements.push_back(make_bool(true));
        array.elements.push_back(make_str("x"));
        list.entries.push_back(entry("values", std::move(array)));
        return doc;
    }

    std::string bytes(std::initializer_list<uint8_t> raw) {
        std::string out;
        out.reserve(raw.size());
        for (const uint8_t byte : raw) out.push_back(static_cast<char>(byte));
        return out;
    }

    bool contains(const std::string &haystack, std::initializer_list<uint8_t> needle) {
        const std::string pattern = bytes(needle);
        return haystack.find(pattern) != std::string::npos;
    }
} // namespace

int main() {
    /* ---- Round trip and canonical stability. ---- */
    {
        const Document original = sample_document();
        const std::string encoded = encode(original);
        assert(!encoded.empty());

        Document decoded;
        const DecodeStatus status = decode(encoded, kSchema, decoded, DecodeMode::Production);
        assert(status.ok());
        assert(decoded.schema == kSchemaVersion);
        assert(decoded.has_release && decoded.release == original.release);
        assert(decoded.has_terminal && decoded.terminal == original.terminal);
        assert(decoded.has_backend && decoded.backend == original.backend);
        assert(decoded.has_route && decoded.route == original.route);

        const Value *major = decoded.find("meta", "kernel_major");
        assert(major != nullptr && major->type == WireType::UInt && major->uint_value == 5);
        const Value *safe = decoded.find("meta", "safe_mode");
        assert(safe != nullptr && safe->type == WireType::Bool && !safe->bool_value);
        const Value *init = decoded.find("offset", "init_task");
        assert(init != nullptr && init->uint_value == 34677760);
        const Value *slide = decoded.find("offset", "slide");
        assert(slide != nullptr && slide->type == WireType::Int && slide->int_value == -2);
        const Value *payload = decoded.find("blob", "payload");
        assert(payload != nullptr && payload->type == WireType::Bin);
        assert(payload->bytes.size() == 3 && payload->bytes[0] == '\x01');
        const Value *values = decoded.find("list", "values");
        assert(values != nullptr && values->type == WireType::Array);
        assert(values->elements.size() == 3);
        assert(values->elements[0].uint_value == 300);
        assert(values->elements[1].bool_value);
        assert(values->elements[2].bytes == "x");

        /* Canonical: same logical document -> byte-identical output. */
        const std::string again = encode(decoded);
        assert(again == encoded);

        /* Section-insertion order must not affect canonical bytes. */
        Document reordered;
        reordered.schema = original.schema;
        reordered.has_release = true;
        reordered.release = original.release;
        reordered.has_terminal = true;
        reordered.terminal = original.terminal;
        reordered.has_backend = true;
        reordered.backend = original.backend;
        reordered.has_route = true;
        reordered.route = original.route;
        for (auto it = original.sections.rbegin(); it != original.sections.rend(); ++it) {
            reordered.sections.push_back(*it);
            std::swap(reordered.sections.back().entries[0],
                      reordered.sections.back().entries.back());
        }
        assert(encode(reordered) == encoded);
    }

    /* ---- Canonical golden for a minimal document. ---- */
    {
        Document doc;
        doc.schema = kSchemaVersion;
        doc.has_release = true;
        doc.release = "r";
        const std::string encoded = encode(doc);
        const std::string golden = bytes({
                0x83,
                0xa7, 'r', 'e', 'l', 'e', 'a', 's', 'e',
                0xa1, 'r',
                0xa6, 's', 'c', 'h', 'e', 'm', 'a',
                0x03,
                0xa8, 's', 'e', 'c', 't', 'i', 'o', 'n', 's',
                0x80,
        });
        assert(encoded == golden);
    }

    /* ---- Shortest integer forms. ---- */
    {
        Document doc;
        doc.schema = kSchemaVersion;
        doc.has_release = true;
        doc.release = "r";
        Section &meta = doc.append_section("meta");
        meta.entries.push_back(entry("kernel_major", make_uint(300)));
        Section &offset = doc.append_section("offset");
        offset.entries.push_back(entry("init_task", make_uint(128)));
        offset.entries.push_back(entry("slide", make_int(-33)));
        const std::string encoded = encode(doc);
        assert(contains(encoded, {0xcd, 0x01, 0x2c}));       /* uint16 300 */
        assert(contains(encoded, {0xcc, 0x80}));             /* uint8 128 */
        assert(contains(encoded, {0xd0, 0xdf}));             /* int8 -33 */
        assert(!contains(encoded, {0xce, 0x00, 0x00, 0x01, 0x2c}));
    }

    /* ---- Rejection vectors. ---- */
    {
        Document doc;
        assert(decode("", kSchema, doc, DecodeMode::Production).code == DecodeCode::Truncated);
        assert(decode(std::string(kMaxDocumentBytes + 1, '\0'), kSchema, doc,
                      DecodeMode::Production).code == DecodeCode::DocumentTooLarge);
        assert(decode(bytes({0x01}), kSchema, doc, DecodeMode::Production).code ==
               DecodeCode::NotAMap);

        /* map { "sections": {} } with no schema key. */
        const std::string no_schema = bytes({
                0x81,
                0xa8, 's', 'e', 'c', 't', 'i', 'o', 'n', 's',
                0x80,
        });
        assert(decode(no_schema, kSchema, doc, DecodeMode::Production).code ==
               DecodeCode::MissingSchema);

        /* map { "schema": 2 }. */
        const std::string wrong_schema = bytes({
                0x81,
                0xa6, 's', 'c', 'h', 'e', 'm', 'a',
                0x02,
        });
        assert(decode(wrong_schema, kSchema, doc, DecodeMode::Production).code ==
               DecodeCode::WrongSchema);

        /* Trailing byte after a valid document. */
        {
            std::string trailing = encode(sample_document());
            trailing.push_back('\0');
            assert(decode(trailing, kSchema, doc, DecodeMode::Production).code ==
                   DecodeCode::Malformed);
        }

        /* Truncated document: drop the last byte of a valid encoding. */
        {
            std::string cut = encode(sample_document());
            cut.pop_back();
            assert(!decode(cut, kSchema, doc, DecodeMode::Production).ok());
        }

        /* Unknown section: Production rejects, Tooling skips. */
        {
            Document unknown_section = sample_document();
            Section &extra = unknown_section.append_section("nope");
            extra.entries.push_back(entry("x", make_uint(1)));
            const std::string encoded = encode(unknown_section);
            assert(decode(encoded, kSchema, doc, DecodeMode::Production).code ==
                   DecodeCode::UnknownSection);
            Document tooling;
            assert(decode(encoded, kSchema, tooling, DecodeMode::Tooling).ok());
            assert(tooling.find_section("nope") == nullptr);
        }

        /* Unknown key inside a known section. */
        {
            Document unknown_key = sample_document();
            Section *meta = unknown_key.find_section("meta");
            assert(meta != nullptr);
            meta->entries.push_back(entry("nope", make_uint(1)));
            const std::string encoded = encode(unknown_key);
            assert(decode(encoded, kSchema, doc, DecodeMode::Production).code ==
                   DecodeCode::UnknownKey);
            Document tooling;
            assert(decode(encoded, kSchema, tooling, DecodeMode::Tooling).ok());
        }

        /* Type mismatch: kernel_major declared UInt but written as a string. */
        {
            Document mismatch = sample_document();
            Section *meta = mismatch.find_section("meta");
            assert(meta != nullptr);
            meta->entries.push_back(entry("kernel_major", make_str("five")));
            const std::string encoded = encode(mismatch);
            assert(decode(encoded, kSchema, doc, DecodeMode::Production).code ==
                   DecodeCode::TypeMismatch);
        }

        /* Missing required root field. */
        {
            Document missing = sample_document();
            missing.has_release = false;
            missing.release = {};
            const std::string encoded = encode(missing);
            const DecodeStatus status =
                    decode(encoded, kSchema, doc, DecodeMode::Production);
            assert(status.code == DecodeCode::MissingRequired);
            assert(status.key == "release");
        }

        /* R2 string bound: 256 UTF-8 bytes decode, 257 reject. */
        {
            const std::string text_256(256, 'a');
            const std::string text_257(257, 'a');
            const auto bounded_document = [](const std::string &text) {
                Document doc;
                doc.schema = kSchemaVersion;
                doc.has_release = true;
                doc.release = "r";
                doc.has_terminal = true;
                doc.terminal = "root_child";
                doc.has_backend = true;
                doc.backend = "cve_2026_43499";
                doc.has_route = true;
                doc.route = "multicast_waiter";
                Section &meta = doc.append_section("meta");
                meta.entries.push_back(entry("kernel_major", make_uint(5)));
                meta.entries.push_back(entry("safe_mode", make_bool(false)));
                doc.append_section("offset")
                        .entries.push_back(entry("init_task", make_uint(1)));
                doc.append_section("bounded")
                        .entries.push_back(entry("text", make_str(text)));
                return doc;
            };
            Document decoded;
            assert(decode(encode(bounded_document(text_256)), kSchema, decoded,
                          DecodeMode::Production).ok());
            assert(decode(encode(bounded_document(text_257)), kSchema, doc,
                          DecodeMode::Production).code == DecodeCode::TypeMismatch);
        }

        /* Over-deep unknown subtree: map { "schema": 3, "junk": [[...[nil]...]] }. */
        {
            std::string deep = bytes({0x82, 0xa6, 's', 'c', 'h', 'e', 'm', 'a', 0x03,
                                      0xa4, 'j', 'u', 'n', 'k'});
            for (int i = 0; i < 40; ++i) deep.push_back(static_cast<char>(0x91));
            deep.push_back('\0');
            /* Production rejects the unknown root key before descending; the
             * depth bound is what stops the Tooling skip from recursing. */
            assert(decode(deep, kSchema, doc, DecodeMode::Production).code ==
                   DecodeCode::UnknownKey);
            Document tooling;
            assert(decode(deep, kSchema, tooling, DecodeMode::Tooling).code ==
                   DecodeCode::TooDeep);
        }
    }

    /* ---- Bounded deterministic fuzz. ---- */
    {
        uint32_t state = 0x1234abcdu;
        const auto next = [&state]() {
            state = state * 1664525u + 1013904223u;
            return state;
        };

        const std::string base = encode(sample_document());
        for (int iteration = 0; iteration < 3000; ++iteration) {
            std::string mutated = base;
            const int mutations = static_cast<int>(next() % 4u) + 1;
            for (int m = 0; m < mutations; ++m) {
                const size_t index = static_cast<size_t>(next()) % mutated.size();
                mutated[index] = static_cast<char>(next() & 0xffu);
            }
            Document decoded;
            const DecodeStatus status =
                    decode(mutated, kSchema, decoded, DecodeMode::Production);
            if (status.ok()) {
                const std::string again = encode(decoded);
                Document second;
                assert(decode(again, kSchema, second, DecodeMode::Production).ok());
                assert(encode(second) == again);
            }
            Document tooling;
            (void)decode(mutated, kSchema, tooling, DecodeMode::Tooling);
        }

        for (int iteration = 0; iteration < 2000; ++iteration) {
            const size_t length = static_cast<size_t>(next()) % 64u;
            std::string random(length, '\0');
            for (size_t i = 0; i < length; ++i) {
                random[i] = static_cast<char>(next() & 0xffu);
            }
            Document decoded;
            (void)decode(random, kSchema, decoded, DecodeMode::Production);
            (void)decode(random, kSchema, decoded, DecodeMode::Tooling);
        }
    }

    std::puts("glkv3_codec_test: OK");
    return 0;
}
