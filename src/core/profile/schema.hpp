#ifndef GHOSTLOCK_PROFILE_SCHEMA_H
#define GHOSTLOCK_PROFILE_SCHEMA_H

/* Owner side of the neutral profile container (ADR-0003).
 *
 * A Schema declares, per owner, the FieldSpecs it owns and the View they bind
 * into. bind<Schema> validates the whole Document fail-closed (unknown
 * section/key, required presence, destination width) and only then materialises
 * the View; a failed bind leaves the caller's View untouched. DecodeMode selects
 * production strictness versus tooling tolerance.
 *
 * Ownership rule: a (section, key) pair has exactly one owner. A section may be
 * shared by several owners as long as their keys do not overlap. */

#include "profile/document.hpp"

#include <concepts>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace ghostlock::profile {
    enum class DecodeMode : uint8_t { Production, Tooling };

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

    /* True when `raw` is exactly representable at the destination width.
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

    /* One owned field. `width` is the destination width in bytes (1/2/4/8);
     * `store` widens/truncates and writes the typed member. */
    template<typename View>
    struct FieldSpec {
        std::string_view section;
        std::string_view key;
        uint8_t width;
        bool is_signed;
        bool required;
        void (*store)(View &, uint64_t);
    };

    template<typename Schema>
    concept SchemaDefinition =
            requires {
                typename Schema::View;
                Schema::kFields;
            } && std::default_initializable<typename Schema::View>;

    namespace schema_detail {
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
                if (!value_fits_width(value->raw, field.width, field.is_signed)) {
                    return BindStatus{BindCode::WidthMismatch, field.section, field.key};
                }
            }
            View staged{};
            for (const FieldSpec<View> &field : fields) {
                const Value *value = document.find_value(field.section, field.key);
                if (!value || !value->present) continue;
                field.store(staged, value->raw);
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
     * owner View without the single-schema unknown rejection. */
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
} // namespace ghostlock::profile

#endif
