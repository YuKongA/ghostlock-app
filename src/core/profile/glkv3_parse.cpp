#include "profile/glkv3_parse.hpp"

#include "profile/document.hpp"
#include "profile/glkv3.hpp"
#include "contract/model.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace ghostlock::profile {
    namespace {
        /* R2 owner-qualified sections: a section must name a known owner
         * prefix ("backend.<id>.*", "platform.<module>.*",
         * "countermeasure.<id>.*") or be the public "common" section. An
         * unknown prefix is rejected here, fail-closed, before any owner bind. */
        bool known_owner_section(std::string_view name) {
            return name == "common" || name.starts_with("backend.") ||
                   name.starts_with("platform.") ||
                   name.starts_with("countermeasure.");
        }

    } // namespace

    bool looks_like_glkv3(std::string_view document) noexcept {
        if (document.empty()) return false;
        const auto first = static_cast<uint8_t>(document.front());
        return (first & 0xF0u) == 0x80u || first == 0xDEu || first == 0xDFu;
    }

    int32_t frame_v3(std::string_view document, profile::Document *out) {
        if (out == nullptr) return -1;

        profile::glkv3::Document decoded;
        const profile::glkv3::DecodeStatus status =
                profile::glkv3::decode_neutral(document, decoded,
                                               profile::glkv3::DecodeMode::Production);
        if (!status.ok()) return -1;
        if (!decoded.has_release || !decoded.has_terminal || !decoded.has_backend) {
            return -1;
        }

        out->release.assign(decoded.release.data(), decoded.release.size());
        out->terminal_token.assign(decoded.terminal.data(), decoded.terminal.size());
        out->backend_token.assign(decoded.backend.data(), decoded.backend.size());
        out->middleware = profile::kRouteAuto;
        if (decoded.has_route) {
            const uint8_t route = profile::route_kind_from_string(decoded.route);
            if (route == profile::kRouteAuto) return -1;
            out->middleware = route;
        }

        for (const profile::glkv3::Section &section : decoded.sections) {
            if (!known_owner_section(section.name)) return -1;
            profile::Section &target = out->append_section(section.name);
            for (const profile::glkv3::Entry &entry : section.entries) {
                /* S4 R4: preserve the wire type. Numeric slots become raw
                 * values; a str becomes a non-owning text view into the same
                 * decode buffer (frame_v3's caller owns it). bin/array are not
                 * part of the owner section vocabulary and fail closed. */
                switch (entry.value.type) {
                    case profile::glkv3::WireType::UInt:
                        target.add(entry.key, entry.value.uint_value);
                        break;
                    case profile::glkv3::WireType::Int:
                        target.add(entry.key,
                                   static_cast<uint64_t>(entry.value.int_value));
                        break;
                    case profile::glkv3::WireType::Bool:
                        target.add(entry.key,
                                   entry.value.bool_value ? uint64_t{1} : uint64_t{0});
                        break;
                    case profile::glkv3::WireType::Str:
                        target.add_text(entry.key, entry.value.bytes);
                        break;
                    case profile::glkv3::WireType::Bin:
                    case profile::glkv3::WireType::Array:
                        return -1;
                }
            }
        }
        return 0;
    }
} // namespace ghostlock::profile
