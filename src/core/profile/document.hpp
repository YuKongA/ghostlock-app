#ifndef GHOSTLOCK_PROFILE_DOCUMENT_H
#define GHOSTLOCK_PROFILE_DOCUMENT_H

/* Neutral framing result for the GLK1 v2 object-section wire (ADR-0003).
 *
 * Document carries exactly what the container can decode without knowing any
 * owner: the release string, the header component ids, and the ordered sections
 * with their ordered key -> Value entries. It never names a field, never
 * interprets a route and never applies a default; presence is expressed by a
 * key's occurrence in its section. Owner schemas (profile/schema.hpp) turn
 * entries into typed Views at startup.
 *
 * `Value::width` is the width of the wire slot the value was decoded from. The
 * v2 entry is a fixed 64-bit value, so decode always records 8; the field lives
 * here so a future transport that carries narrower slots does not need a
 * Document change. A lookup miss is reported as a Value with present == false. */

#include <cstddef>
#include <cstdint>

#include <string>
#include <string_view>
#include <vector>

namespace ghostlock::profile {
    /* HOCON refactor: the root section. Root-level scalars (kernel_major /
     * kernel_minor / safe_mode) travel beside the component tokens in the GLKv3
     * document root, never inside an owner section; the neutral Document carries
     * them in the section with the EMPTY name -- the same marker the GLKv3
     * FieldSpec already uses for a root key (FieldSpec::section is empty).
     * Presence stays key occurrence: a scalar the wire did not carry has no
     * entry (no sentinel), and an owner FieldSpec whose section is empty binds
     * them through find_value(kRootSection, key) like any section entry.
     *
     * A new root key must be materialised here by glkv3_parse.cpp frame_v3 --
     * that step is the silent one: a key decoded into glkv3::Document but not
     * added to this section leaves the owner bind with no value and the field
     * keeps its default (see the checklist in profile/glkv3.hpp). */
    inline constexpr std::string_view kRootSection{};

    /* S4 R4 string bound, mirroring profile::glkv3::kMaxStringBytes. A String
     * field is UTF-8 text with at most this many bytes; the decoder enforces it
     * fail-closed and an owner bind re-checks it so a hand-built Document cannot
     * smuggle a longer value. */
    inline constexpr std::size_t kMaxStringBytes = 256U;

    struct Value {
        static constexpr uint8_t kWireWidth = 8;

        uint64_t raw = 0;
        uint8_t width = 0;
        bool present = false;
        /* WireKind::String payload. When is_text is true, text is the UTF-8 view
         * into the decode buffer (or the caller's literal for a hand-built
         * Document) and raw/width are unused; the buffer must outlive every View
         * the bind materialises. Never copied here. */
        bool is_text = false;
        std::string_view text{};
    };

    struct Entry {
        std::string key;
        Value value;
    };

    struct Section {
        std::string name;
        std::vector<Entry> entries;

        [[nodiscard]] const Value *find(std::string_view key) const noexcept {
            /* Last occurrence wins, mirroring the decoder's last-wins store. */
            for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
                if (it->key == key) return &it->value;
            }
            return nullptr;
        }

        [[nodiscard]] bool contains(std::string_view key) const noexcept {
            return find(key) != nullptr;
        }

        void add(std::string_view key, uint64_t raw,
                 uint8_t width = Value::kWireWidth) {
            entries.push_back(Entry{std::string(key), Value{raw, width, true}});
        }

        /* S4 R4: append a WireKind::String value. text is a non-owning view the
         * caller guarantees outlives the Document's View materialisation. */
        void add_text(std::string_view key, std::string_view text) {
            Value value{};
            value.present = true;
            value.is_text = true;
            value.text = text;
            entries.push_back(Entry{std::string(key), value});
        }
    };

    struct Document {
        std::string release;
        uint16_t terminal = 0;
        uint16_t backend = 0;
        uint16_t middleware = 0;
        uint16_t steps = 0;
        /* S4 R6b: the resolved combination token, as contract::CombinationKind.
         * profile/glkv3_parse.cpp resolves it from backend.<id>.steps (or the
         * legacy uint id + root route) and validates it fail-closed; the
         * composition root derives route/terminal/step set from it. */
        uint8_t combination = 0;
        /* GLKv3 root selection tokens. The v2 transport carries numeric ids in
         * its header, so these stay empty there; the GLKv3 container has no
         * header and names the components as text, which only the composition
         * root (which owns the component vocabulary) resolves to the ids. */
        std::string terminal_token;
        std::string backend_token;
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

        [[nodiscard]] const Value *find_value(std::string_view section_name,
                                              std::string_view key) const noexcept {
            const Value *found = nullptr;
            for (const Section &section : sections) {
                if (section.name != section_name) continue;
                if (const Value *value = section.find(key)) found = value;
            }
            return found;
        }

        [[nodiscard]] Value get(std::string_view section_name,
                                std::string_view key) const noexcept {
            if (const Value *value = find_value(section_name, key)) return *value;
            return Value{};
        }

        [[nodiscard]] bool empty() const noexcept { return sections.empty(); }

        /* Framing append: one wire section becomes one Document section.
         * Duplicate section names are preserved in order, exactly as framed. */
        Section &append_section(std::string_view name) {
            sections.push_back(Section{std::string(name), {}});
            return sections.back();
        }
    };
} // namespace ghostlock::profile

#endif
