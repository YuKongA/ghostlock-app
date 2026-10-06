#ifndef GHOSTLOCK_PROFILE_SCHEMA_H
#define GHOSTLOCK_PROFILE_SCHEMA_H

/* Owner side of the neutral profile container (ADR-0003 / S4 R1).
 *
 * A Schema declares, per owner, the FieldSpecs it owns and the View they bind
 * into. bind<Schema> validates the whole Document fail-closed (unknown
 * section/key, required presence, destination width) and only then materialises
 * the View; a failed bind leaves the caller's View untouched.
 *
 * S4 R1 (this batch) makes the owner Schema the single authority for
 * requiredness and defaults:
 *   - FieldSpec carries {required, default, source, wire, doc} exactly once;
 *   - DefaultValue is one of None | Literal(u64) | Derived(fn) | Convention(path);
 *   - bind_all(registry, document, sink, mode) applies the defaults while it
 *     materialises the Views and reports each triggered default through a
 *     no-allocation BindLog (default_used=<section>.<key>);
 *   - BindMode::Test relaxes required for host tests, Production is strict.
 * The legacy bind/bind_all keep their old (default-free) semantics so neither
 * the 43499 machine code nor the v2 manifest changes (R1 invariant).
 *
 * Ownership rule: a (section, key) pair has exactly one owner. A section may be
 * shared by several owners as long as their keys do not overlap. */

#include "profile/document.hpp"
// USER DIRECTIVE 2026-10-05: payload paused
// #include "profile/glkv3.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace ghostlock::profile {
    enum class DecodeMode : uint8_t { Production, Tooling };

    /* S4 R1 bind strictness. Production is fail-closed (unknown section/key and
     * missing required reject); Test additionally relaxes the required rule so a
     * unit test can probe the default machinery without spelling every field. */
    enum class BindMode : uint8_t { Production, Test };

    /* Where a field's effective value comes from (plan 4.2). The default
     * machinery only uses this for provenance/diagnostics; the priority rule is
     * profile value > derived > convention. */
    enum class FieldSource : uint8_t {
        Profile = 0,
        Derived,
        Convention,
        Platform,
    };

    /* Wire vocabulary (plan 4.2 / R2). S4 R4 enables String end to end: a
     * String field's View member is a std::string_view into the decode buffer and
     * its FieldSpec::store_text writes it (store stays null). The decoder already
     * caps a WireType::Str at glkv3::kMaxStringBytes (256); the bind re-checks
     * that bound and the value's is_text flag fail-closed. */
    enum class WireKind : uint8_t { UInt = 0, Int, Bool, String };

    enum class BindCode : uint8_t {
        Ok = 0,
        UnknownSection,
        UnknownKey,
        MissingRequired,
        WidthMismatch,
        /* Transport-level rejection the field walk cannot express: an unusable
         * route selection or a release string that does not fit the caller's
         * buffer. */
        Invalid,
    };

    struct BindStatus {
        BindCode code = BindCode::Ok;
        std::string_view section{};
        std::string_view key{};

        [[nodiscard]] bool ok() const noexcept { return code == BindCode::Ok; }
        explicit operator bool() const noexcept { return ok(); }
    };

    [[nodiscard]] constexpr std::string_view bind_code_name(BindCode code) noexcept {
        switch (code) {
            case BindCode::Ok:
                return "ok";
            case BindCode::UnknownSection:
                return "unknown_section";
            case BindCode::UnknownKey:
                return "unknown_key";
            case BindCode::MissingRequired:
                return "missing_required";
            case BindCode::WidthMismatch:
                return "width_mismatch";
            case BindCode::Invalid:
                return "invalid";
        }
        return "unknown";
    }

    /* S4 R1 field default. Literal stores a constant; Derived resolves from the
     * Document (e.g. KMI from release); Convention names an owner-side convention
     * (e.g. $GHOSTLOCK_HOME/helper.ko) and may carry a resolver so the convention
     * token is materialised too. resolve returns false when the derivation
     * cannot be made, in which case nothing is stored (fail-closed: the owner
     * keeps whatever it does today). A Convention with no resolver is a pure
     * declaration: it reports default_used and leaves the value to the owner. */
    struct DefaultValue final {
        enum class Kind : uint8_t { None = 0, Literal, Derived, Convention };

        Kind kind = Kind::None;
        uint64_t literal_value = 0;
        bool (*resolve)(const Document &, uint64_t &) noexcept = nullptr;
        std::string_view convention_path{};

        [[nodiscard]] static constexpr DefaultValue none() noexcept { return {}; }

        [[nodiscard]] static constexpr DefaultValue literal(uint64_t value) noexcept {
            DefaultValue out{};
            out.kind = Kind::Literal;
            out.literal_value = value;
            return out;
        }

        [[nodiscard]] static constexpr DefaultValue derived(
                bool (*fn)(const Document &, uint64_t &) noexcept) noexcept {
            DefaultValue out{};
            out.kind = Kind::Derived;
            out.resolve = fn;
            return out;
        }

        [[nodiscard]] static constexpr DefaultValue convention(
                std::string_view path) noexcept {
            DefaultValue out{};
            out.kind = Kind::Convention;
            out.convention_path = path;
            return out;
        }

        [[nodiscard]] static constexpr DefaultValue convention(
                std::string_view path,
                bool (*fn)(const Document &, uint64_t &) noexcept) noexcept {
            DefaultValue out{};
            out.kind = Kind::Convention;
            out.convention_path = path;
            out.resolve = fn;
            return out;
        }

        [[nodiscard]] constexpr bool declared() const noexcept {
            return kind != Kind::None;
        }
    };

    /* No-allocation default diagnostic sink. emit receives the (section, key)
     * pair so the caller can format the owner-qualified path without building a
     * temporary string; default_used=<section>.<key> is the stable shape. */
    struct BindLog final {
        void (*emit)(void *ctx, std::string_view section, std::string_view key,
                     FieldSource source) noexcept = nullptr;
        void *ctx = nullptr;

        void default_used(std::string_view section, std::string_view key,
                          FieldSource source) const noexcept {
            if (emit != nullptr) emit(ctx, section, key, source);
        }
    };

    /* True when raw is exactly representable at the destination width.
     * width 8 accepts every bit pattern; narrower widths require the unused high
     * bits to be zero (unsigned) or a correct sign extension (signed). */
    [[nodiscard]] constexpr bool value_fits_width(uint64_t raw, uint8_t width,
                                                  bool is_signed) noexcept {
        if (width == 0) return raw == 0;
        if (width >= 8) return true;
        const unsigned bits = static_cast<unsigned>(width) * 8u;
        const uint64_t mask = (uint64_t{1} << bits) - 1u;
        const uint64_t low = raw & mask;
        if (!is_signed) return raw == low;
        const uint64_t sign_bit = uint64_t{1} << (bits - 1u);
        const uint64_t extended = (low ^ sign_bit) - sign_bit;
        return raw == extended;
    }

    /* One owned field. width is the destination width in bytes (1/2/4/8);
     * store widens/truncates and writes the typed member. The R1 declaration
     * members are appended with defaults so every existing positional aggregate
     * initializer ({section,key,width,is_signed,required,store}) still compiles
     * and keeps its old semantics. The layout is therefore frozen; reordering to
     * the analyzer's optimal packing would rewrite every owner schema, so the
     * opt-in padding performance hint is suppressed here (R1 review note). */
    template<typename View>
    struct FieldSpec { // NOLINT(clang-analyzer-optin.performance.Padding)
        std::string_view section;
        std::string_view key;
        uint8_t width;
        bool is_signed;
        bool required;
        void (*store)(View &, uint64_t);
        DefaultValue default_value = DefaultValue::none();
        FieldSource source = FieldSource::Profile;
        WireKind wire = WireKind::UInt;
        std::string_view doc{};
        /* S4 R4: WireKind::String destination. The View member is a
         * std::string_view; the bind stores the decoded buffer view verbatim (no
         * copy). Null for every non-String field. */
        void (*store_text)(View &, std::string_view) = nullptr;
    };

    template<typename Schema>
    concept SchemaDefinition =
            requires {
                typename Schema::View;
                Schema::kFields;
            } && std::default_initializable<typename Schema::View>;

    namespace schema_detail {
        /* S4 R4: one value -> one field, fail-closed. A String field accepts only
         * a text value within kMaxStringBytes; every other field accepts only a
         * numeric value representable at its destination width. */
        template<typename View>
        [[nodiscard]] bool field_value_fits(const FieldSpec<View> &field,
                                            const Value &value) noexcept {
            if (field.wire == WireKind::String) {
                return value.is_text && value.text.size() <= kMaxStringBytes;
            }
            if (value.is_text) return false;
            return value_fits_width(value.raw, field.width, field.is_signed);
        }

        /* Writes an already-validated value into the staged View. Never copies a
         * string: the View's std::string_view aliases the Document's view. */
        template<typename View>
        void store_field(const FieldSpec<View> &field, View &view,
                         const Value &value) {
            if (field.wire == WireKind::String) {
                if (field.store_text != nullptr) field.store_text(view, value.text);
                return;
            }
            field.store(view, value.raw);
        }

        /* Validate + materialise one owner's View (steps 2/3 of the
         * single-owner bind). The caller owns the section/key ownership check. */
        template<SchemaDefinition Schema>
        [[nodiscard]] BindStatus materialize(const Document &document,
                                             typename Schema::View &out) {
            using View = typename Schema::View;
            constexpr auto &fields = Schema::kFields;
            for (const FieldSpec<View> &field : fields) {
                const Value *value = document.find_value(field.section, field.key);
                if (!value || !value->present) {
                    if (field.required) {
                        return BindStatus{BindCode::MissingRequired, field.section, field.key};
                    }
                    continue;
                }
                if (!field_value_fits(field, *value)) {
                    return BindStatus{BindCode::WidthMismatch, field.section, field.key};
                }
            }
            View staged{};
            for (const FieldSpec<View> &field : fields) {
                const Value *value = document.find_value(field.section, field.key);
                if (!value || !value->present) continue;
                store_field(field, staged, *value);
            }
            out = staged;
            return BindStatus{};
        }

        /* S4 R1 materialise: same validation as materialize(), but a missing
         * field falls back to its declared default. A triggered default is
         * reported through log; Test mode drops the required rule. */
        template<SchemaDefinition Schema>
        [[nodiscard]] BindStatus materialize_with_defaults(
                const Document &document, typename Schema::View &out, BindMode mode,
                const BindLog *log) {
            using View = typename Schema::View;
            constexpr auto &fields = Schema::kFields;
            for (const FieldSpec<View> &field : fields) {
                const Value *value = document.find_value(field.section, field.key);
                const bool present = value != nullptr && value->present;
                if (!present) {
                    if (field.required && mode != BindMode::Test) {
                        return BindStatus{BindCode::MissingRequired, field.section,
                                          field.key};
                    }
                    continue;
                }
                if (!field_value_fits(field, *value)) {
                    return BindStatus{BindCode::WidthMismatch, field.section, field.key};
                }
            }
            View staged{};
            for (const FieldSpec<View> &field : fields) {
                const Value *value = document.find_value(field.section, field.key);
                if (value != nullptr && value->present) {
                    store_field(field, staged, *value);
                    continue;
                }
                if (!field.default_value.declared()) continue;
                uint64_t resolved = 0;
                bool store_default = false;
                switch (field.default_value.kind) {
                    case DefaultValue::Kind::Literal:
                        resolved = field.default_value.literal_value;
                        store_default = true;
                        break;
                    case DefaultValue::Kind::Derived:
                    case DefaultValue::Kind::Convention:
                        if (field.default_value.resolve != nullptr) {
                            store_default = field.default_value.resolve(document, resolved);
                        }
                        break;
                    case DefaultValue::Kind::None:
                        break;
                }
                /* A String field has no numeric default channel; its declared
                 * Convention is a pure declaration (store_text owns the value),
                 * so only numeric defaults reach store(). */
                if (store_default && field.wire != WireKind::String) {
                    field.store(staged, resolved);
                }
                if (log != nullptr) {
                    log->default_used(field.section, field.key, field.source);
                }
            }
            out = staged;
            return BindStatus{};
        }

        template<SchemaDefinition Schema>
        [[nodiscard]] bool declares_section(std::string_view name) noexcept {
            for (const auto &field : Schema::kFields) {
                if (field.section == name) return true;
            }
            return false;
        }

        template<SchemaDefinition Schema>
        [[nodiscard]] bool declares_key(std::string_view section,
                                        std::string_view key) noexcept {
            for (const auto &field : Schema::kFields) {
                if (field.section == section && field.key == key) return true;
            }
            return false;
        }
    } // namespace schema_detail

    template<SchemaDefinition Schema>
    [[nodiscard]] BindStatus bind(const Document &document,
                                  typename Schema::View &out,
                                  DecodeMode mode = DecodeMode::Production) {
        using View = typename Schema::View;
        constexpr auto &fields = Schema::kFields;

        /* 1. Reject anything no spec in this Schema owns (production only). */
        if (mode == DecodeMode::Production) {
            for (const Section &section : document.sections) {
                bool section_known = false;
                for (const FieldSpec<View> &field : fields) {
                    if (field.section == section.name) {
                        section_known = true;
                        break;
                    }
                }
                if (!section_known) {
                    return BindStatus{BindCode::UnknownSection, section.name, {}};
                }
                for (const Entry &entry : section.entries) {
                    bool key_known = false;
                    for (const FieldSpec<View> &field : fields) {
                        if (field.section == section.name && field.key == entry.key) {
                            key_known = true;
                            break;
                        }
                    }
                    if (!key_known) {
                        return BindStatus{BindCode::UnknownKey, section.name, entry.key};
                    }
                }
            }
        }

        return schema_detail::materialize<Schema>(document, out);
    }

    /* Union bind for owners that share a section (ADR-0003 decisions 4/5, plan
     * section 3.3): validate the (section, key) union once, then materialise each
     * owner View without the single-schema unknown rejection. Legacy, default-free
     * semantics; the R1 registry bind below is the default-aware form. */
    template<SchemaDefinition... Schemas>
    [[nodiscard]] BindStatus bind_all(const Document &document, DecodeMode mode,
                                      typename Schemas::View &...views) {
        if (mode == DecodeMode::Production) {
            for (const Section &section : document.sections) {
                const bool section_known =
                        (schema_detail::declares_section<Schemas>(section.name) || ...);
                if (!section_known) {
                    return BindStatus{BindCode::UnknownSection, section.name, {}};
                }
                for (const Entry &entry : section.entries) {
                    const bool key_known =
                            (schema_detail::declares_key<Schemas>(section.name, entry.key) ||
                             ...);
                    if (!key_known) {
                        return BindStatus{BindCode::UnknownKey, section.name, entry.key};
                    }
                }
            }
        }
        BindStatus status{};
        const auto bind_one = [&](auto tag, auto &view) -> bool {
            if (!status.ok()) return false;
            status = schema_detail::materialize<typename decltype(tag)::type>(document,
                                                                             view);
            return status.ok();
        };
        (bind_one(std::type_identity<Schemas>{}, views) && ...);
        return status;
    }

//     /* S4 payload owner (contract-design 3.15): the GLKv3 path -> type mirror the
//      * manifest export is built from. The section is validated fail-closed by
//      * profile/glkv3_parse.cpp (validate_payload_section), not by this list: these
//      * rows exist so both manifest copies (and the Kotlin adapter) can tell a
//      * declared payload path from an ordinary key. The per-module rows use the
//      * SAME wildcard convention as the plugin owner (plugin/schema.hpp): an
//      * angle-bracketed placeholder, here the ko index <i> (0..7) under the ko.
//      * node -- so the per-module indexes sit beside ko.count, not at top level
//      * (design r3, ruling 2026-10-05). */
//     inline constexpr glkv3::FieldSpec kPayloadGlkv3Fields[] = {
//         {"payload", "tier", glkv3::WireType::Str, true},
//         {"payload", "exec.command", glkv3::WireType::Str, false},
//         {"payload", "exec.sha256", glkv3::WireType::Str, false},
//         {"payload", "script.path", glkv3::WireType::Str, false},
//         {"payload", "script.sha256", glkv3::WireType::Str, false},
//         {"payload", "ko.count", glkv3::WireType::UInt, false},
//         {"payload", "ko.<i>.path", glkv3::WireType::Str, false},
//         {"payload", "ko.<i>.sha256", glkv3::WireType::Str, false},
//     };
} // namespace ghostlock::profile

#endif
