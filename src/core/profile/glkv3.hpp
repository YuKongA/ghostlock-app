#ifndef GHOSTLOCK_PROFILE_GLKV3_HPP
#define GHOSTLOCK_PROFILE_GLKV3_HPP

/* GLKv3 wire codec (MessagePack / MPack, no magic prefix).
 *
 * Design: docs/analysis/wire-transport-model.md. GLKv3 is a bare MessagePack
 * value whose root is a map carrying a mandatory "schema": 3. There is no
 * container header; the version lives in the document, and presence is
 * expressed by key occurrence (an omitted key is not 0/false/empty).
 *
 * The decoder is schema-driven and fail-closed: the MPack expect API walks the
 * document directly (no generic node tree, no MPack allocation); a field is
 * only stored when the schema declares its (section, key) and the wire type
 * matches, so a partial or mistyped document is never published; unknown
 * sections/keys are rejected under Production and depth-bound discarded under
 * Tooling; required fields, schema == 3, the 1 MiB size bound and a
 * nesting-depth bound are checked before the result is handed back.
 *
 * A decoded Document is a view: strings and binaries point into the caller
 * input buffer (section/key names too), so that buffer must outlive it.
 *
 * encode() is the canonical writer used for tests and goldens: shortest
 * integer forms, map keys sorted by UTF-8 byte order, no floats. */

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ghostlock::profile::glkv3 {
    enum class WireType : uint8_t {
        UInt = 0,
        Int,
        Bool,
        Str,
        Bin,
        Array,
        /* Union of the scalar kinds {uint,int,bool,str}, used by the S4 P1
         * dynamic plugin paths (plugin.<id>.params.* / .extract.*) whose exact
         * type is fixed by the plugin descriptor. The manifest writes the
         * members explicitly as a "|"-separated union; a manifest type token
         * outside the wire-kind vocabulary is rejected (fail-closed). */
        Union,
    };

    [[nodiscard]] constexpr std::string_view wire_type_name(WireType type) noexcept {
        switch (type) {
            case WireType::UInt: return "uint";
            case WireType::Int: return "int";
            case WireType::Bool: return "bool";
            case WireType::Str: return "str";
            case WireType::Bin: return "bin";
            case WireType::Array: return "array";
            case WireType::Union: return "uint|int|bool|str";
        }
        return "unknown";
    }

    /* The union members of WireType::Union, in canonical order. The manifest
     * "type" column may carry a "|"-separated union; every member must come
     * from this list, and the parser rejects an unknown member. */
    inline constexpr WireType kUnionScalarTypes[] = {
        WireType::UInt, WireType::Int, WireType::Bool, WireType::Str,
    };

    [[nodiscard]] constexpr bool wire_type_is_union_member(WireType type) noexcept {
        for (const WireType member : kUnionScalarTypes) {
            if (member == type) return true;
        }
        return false;
    }

    inline constexpr uint64_t kSchemaVersion = 3u;

    /* R2 string bound: a Str value is UTF-8 text with at most this many bytes.
     * Enforced on decode (the Kotlin encoder enforces the same bound). */
    inline constexpr uint32_t kMaxStringBytes = 256u;

    /* Hard bounds; a document that exceeds any of them is rejected whole. */
    inline constexpr size_t kMaxDocumentBytes = 1u << 20; /* 1 MiB */
    inline constexpr unsigned kMaxDepth = 24;
    inline constexpr uint32_t kMaxRootEntries = 64u;
    inline constexpr uint32_t kMaxSections = 64u;
    inline constexpr uint32_t kMaxSectionEntries = 4096u;
    inline constexpr uint32_t kMaxArrayElements = 4096u;

    enum class DecodeMode : uint8_t { Production = 0, Tooling };

    enum class DecodeCode : uint8_t {
        Ok = 0,
        DocumentTooLarge,
        NotAMap,
        MissingSchema,
        WrongSchema,
        UnknownSection,
        UnknownKey,
        MissingRequired,
        TypeMismatch,
        Truncated,
        TooDeep,
        TooManyEntries,
        Malformed,
    };

    [[nodiscard]] constexpr std::string_view decode_code_name(DecodeCode code) noexcept {
        switch (code) {
            case DecodeCode::Ok: return "ok";
            case DecodeCode::DocumentTooLarge: return "document_too_large";
            case DecodeCode::NotAMap: return "not_a_map";
            case DecodeCode::MissingSchema: return "missing_schema";
            case DecodeCode::WrongSchema: return "wrong_schema";
            case DecodeCode::UnknownSection: return "unknown_section";
            case DecodeCode::UnknownKey: return "unknown_key";
            case DecodeCode::MissingRequired: return "missing_required";
            case DecodeCode::TypeMismatch: return "type_mismatch";
            case DecodeCode::Truncated: return "truncated";
            case DecodeCode::TooDeep: return "too_deep";
            case DecodeCode::TooManyEntries: return "too_many_entries";
            case DecodeCode::Malformed: return "malformed";
        }
        return "unknown";
    }

    struct DecodeStatus {
        DecodeCode code = DecodeCode::Ok;
        std::string_view section{};
        std::string_view key{};

        [[nodiscard]] bool ok() const noexcept { return code == DecodeCode::Ok; }
        explicit operator bool() const noexcept { return ok(); }
    };

    struct FieldSpec {
        std::string_view section;
        std::string_view key;
        WireType type = WireType::UInt;
        bool required = false;
        /* HOCON refactor: a wire-only key has NO profile declaration. The wire
         * still accepts the key (native resolves its value at the point of use,
         * or a document may carry it), but the exported manifest -- the Kotlin
         * side's profile-declaration surface -- must not list it, so a profile
         * can never write it. kmi / lkm_path / carrier_path are the first
         * three; every other key is profile-declarable. */
        bool wire_only = false;
    };

    struct Schema {
        std::span<const FieldSpec> fields{};
    };

    struct Value {
        WireType type = WireType::UInt;
        uint64_t uint_value = 0;
        int64_t int_value = 0;
        bool bool_value = false;
        std::string_view bytes{};
        std::vector<Value> elements{};
    };

    struct Entry {
        std::string_view key;
        Value value;
    };

    struct Section {
        std::string_view name;
        std::vector<Entry> entries;

        [[nodiscard]] const Value *find(std::string_view key) const noexcept {
            for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
                if (it->key == key) return &it->value;
            }
            return nullptr;
        }
    };

    struct Document {
        uint64_t schema = 0;
        bool has_release = false;
        bool has_terminal = false;
        bool has_backend = false;
        bool has_route = false;
        /* HOCON refactor: root-level scalars. The wire carries them beside the
         * component tokens in the document root (never inside an owner
         * section); has_* is key occurrence, so an absent scalar keeps its
         * 0/false value with no sentinel.
         *
         * ADDING A ROOT KEY means changing FOUR places together (a missed
         * frame_v3 is silent -- the bind then sees no value and keeps the
         * default):
         *   1. this struct (has_* + value);
         *   2. glkv3.cpp decode (the schema-free and the schema-driven walk);
         *   3. glkv3.cpp encode (root key count + canonical UTF-8 order);
         *   4. glkv3_parse.cpp frame_v3 (materialise the key into the root
         *      section, document.hpp kRootSection).
         * Plus the declaration row with an empty section in the v2 Schema and in
         * the GLKv3 table, and owner_for("") == "root" in the manifest
         * generator. The codec round-trip assertion catches a missing encode;
         * only step 4 fails silently. */
        bool has_kernel_major = false;
        bool has_kernel_minor = false;
        bool has_safe_mode = false;
        std::string_view release{};
        std::string_view terminal{};
        std::string_view backend{};
        std::string_view route{};
        uint64_t kernel_major = 0;
        uint64_t kernel_minor = 0;
        bool safe_mode = false;
        std::vector<Section> sections;

        [[nodiscard]] const Section *find_section(std::string_view name) const noexcept {
            for (const Section &section : sections) {
                if (section.name == name) return &section;
            }
            return nullptr;
        }

        [[nodiscard]] Section *find_section(std::string_view name) noexcept {
            for (Section &section : sections) {
                if (section.name == name) return &section;
            }
            return nullptr;
        }

        [[nodiscard]] const Value *find(std::string_view section_name,
                                        std::string_view key) const noexcept {
            const Value *found = nullptr;
            for (const Section &section : sections) {
                if (section.name != section_name) continue;
                if (const Value *value = section.find(key)) found = value;
            }
            return found;
        }

        [[nodiscard]] bool empty() const noexcept { return sections.empty(); }

        Section &append_section(std::string_view name) {
            sections.push_back(Section{name, {}});
            return sections.back();
        }
    };

    [[nodiscard]] DecodeStatus decode(std::string_view input, const Schema &schema,
                                      Document &out,
                                      DecodeMode mode = DecodeMode::Production);

    /* Schema-free framing decode (A2-5): the same MPack walk and fail-closed
     * structural rules, but with no owner declaration. Unknown section/key are
     * preserved instead of rejected (the owner bind that follows rejects them),
     * while section values preserve their wire type (UInt/Int/Bool/Str; S4 R4
     * added Str for string policy paths). bin/array remain rejected. Used by
     * profile_entry to produce a neutral Document before any backend is
     * selected. */
    [[nodiscard]] DecodeStatus decode_neutral(
            std::string_view input, Document &out,
            DecodeMode mode = DecodeMode::Production);
    [[nodiscard]] std::string encode(const Document &document);
} // namespace ghostlock::profile::glkv3

#endif
