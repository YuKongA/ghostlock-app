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
    struct Value {
        static constexpr uint8_t kWireWidth = 8;

        uint64_t raw = 0;
        uint8_t width = 0;
        bool present = false;
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
    };

    struct Document {
        std::string release;
        uint16_t terminal = 0;
        uint16_t backend = 0;
        uint16_t middleware = 0;
        uint16_t steps = 0;
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
