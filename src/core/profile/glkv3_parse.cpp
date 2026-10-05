#include "profile/glkv3_parse.hpp"

#include "profile/document.hpp"
#include "profile/glkv3.hpp"
#include "contract/model.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace ghostlock::binary_profile {
    namespace {
        uint64_t raw_value(const profile::glkv3::Value &value, bool &ok) {
            switch (value.type) {
                case profile::glkv3::WireType::UInt:
                    ok = true;
                    return value.uint_value;
                case profile::glkv3::WireType::Int:
                    ok = true;
                    return static_cast<uint64_t>(value.int_value);
                case profile::glkv3::WireType::Bool:
                    ok = true;
                    return value.bool_value ? uint64_t{1} : uint64_t{0};
                case profile::glkv3::WireType::Str:
                case profile::glkv3::WireType::Bin:
                case profile::glkv3::WireType::Array:
                    break;
            }
            ok = false;
            return 0;
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
            profile::Section &target = out->append_section(section.name);
            for (const profile::glkv3::Entry &entry : section.entries) {
                bool ok = false;
                const uint64_t raw = raw_value(entry.value, ok);
                if (!ok) return -1;
                target.add(entry.key, raw);
            }
        }
        return 0;
    }
} // namespace ghostlock::binary_profile
