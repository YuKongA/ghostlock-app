#include "profile/glkv3_parse.hpp"

#include "backend/cve_2026_43284/glkv3_schema.hpp"
#include "backend/cve_2026_43499/glkv3_schema.hpp"
#include "pipeline/component_catalog.hpp"
#include "profile/document.hpp"
#include "profile/glkv3.hpp"
#include "profile/model.h"

#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace ghostlock::binary_profile {
    namespace {
        using ghostlock::profile::glkv3::DecodeMode;
        using ghostlock::profile::glkv3::DecodeStatus;
        using ghostlock::profile::glkv3::FieldSpec;
        using ghostlock::profile::glkv3::Schema;
        using ghostlock::profile::glkv3::Value;
        using ghostlock::profile::glkv3::WireType;

        /* Root keys the GLKv3 document declares. Roots are not sections: their
         * `section` is empty, so glkv3::decode treats them specially. release /
         * terminal / backend are required; route is absent for the route-less
         * 43284 backend and required by bind_document otherwise. */
        constexpr FieldSpec kRootFields[] = {
            {"", "schema", WireType::UInt, true},
            {"", "release", WireType::Str, true},
            {"", "terminal", WireType::Str, true},
            {"", "backend", WireType::Str, true},
            {"", "route", WireType::Str, false},
        };

        /* Combined root + owner declaration: one decode pass validates the whole
         * document, including both backend-private sections. Whether the 43284
         * section is legal for the declared backend is decided by
         * bind_document(), exactly as on the v2 path. */
        const Schema &combined_schema() {
            static const std::vector<FieldSpec> fields = [] {
                std::vector<FieldSpec> out;
                out.insert(out.end(), std::begin(kRootFields),
                           std::end(kRootFields));
                out.insert(out.end(),
                           std::begin(ghostlock::backend::kCve2026_43499Glkv3Fields),
                           std::end(ghostlock::backend::kCve2026_43499Glkv3Fields));
                out.insert(out.end(),
                           std::begin(ghostlock::backend::kCve2026_43284Glkv3Fields),
                           std::end(ghostlock::backend::kCve2026_43284Glkv3Fields));
                return out;
            }();
            static const Schema schema{fields};
            return schema;
        }

        bool terminal_from_token(std::string_view token, uint16_t &out) {
            for (const ghostlock::pipeline::TerminalKind kind :
                 {ghostlock::pipeline::TerminalKind::RootChild,
                  ghostlock::pipeline::TerminalKind::UmhForward}) {
                if (token == ghostlock::pipeline::terminal_name(kind)) {
                    out = static_cast<uint16_t>(kind);
                    return true;
                }
            }
            return false;
        }

        bool backend_from_token(std::string_view token, uint16_t &out) {
            for (const ghostlock::pipeline::BackendKind kind :
                 {ghostlock::pipeline::BackendKind::Cve2026_43499,
                  ghostlock::pipeline::BackendKind::Cve2026_64560,
                  ghostlock::pipeline::BackendKind::Cve2026_31431,
                  ghostlock::pipeline::BackendKind::Cve2026_43503,
                  ghostlock::pipeline::BackendKind::Cve2026_23274,
                  ghostlock::pipeline::BackendKind::Cve2026_43284}) {
                if (token == ghostlock::pipeline::backend_name(kind)) {
                    out = static_cast<uint16_t>(kind);
                    return true;
                }
            }
            return false;
        }

        /* One typed GLKv3 value as the neutral u64 slot the v2 owner Schema
         * binds. Bool becomes 0/1 and Int becomes its twos-complement bits; the
         * destination width/sign check in bind() does the rest. Text, binary and
         * array values never reach a section field under the current schema, so
         * they are rejected. */
        uint64_t raw_value(const Value &value, bool &ok) {
            switch (value.type) {
                case WireType::UInt:
                    ok = true;
                    return value.uint_value;
                case WireType::Int:
                    ok = true;
                    return static_cast<uint64_t>(value.int_value);
                case WireType::Bool:
                    ok = true;
                    return value.bool_value ? uint64_t{1} : uint64_t{0};
                case WireType::Str:
                case WireType::Bin:
                case WireType::Array:
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

    int32_t parse_v3(std::string_view document, profile::kernel_offsets *out,
                     char *release_buf, size_t release_buf_cap, component_ids *ids,
                     profile::Document *document_out,
                     ghostlock::backend::Cve2026_43284Profile *profile_43284_out) {
        if (!out || !release_buf || release_buf_cap == 0) return -1;

        ghostlock::profile::glkv3::Document decoded;
        const DecodeStatus status = ghostlock::profile::glkv3::decode(
                document, combined_schema(), decoded, DecodeMode::Production);
        if (!status.ok()) return -1;
        if (!decoded.has_release || !decoded.has_terminal || !decoded.has_backend) {
            return -1;
        }

        uint16_t terminal = 0;
        uint16_t backend = 0;
        if (!terminal_from_token(decoded.terminal, terminal)) return -1;
        if (!backend_from_token(decoded.backend, backend)) return -1;

        uint8_t route = profile::kRouteAuto;
        if (decoded.has_route) {
            route = profile::route_kind_from_string(decoded.route);
            if (route == profile::kRouteAuto) return -1;
        }

        profile::Document framed;
        framed.release.assign(decoded.release.data(), decoded.release.size());
        framed.terminal = terminal;
        framed.backend = backend;
        framed.middleware = route;
        for (const ghostlock::profile::glkv3::Section &section : decoded.sections) {
            profile::Section &target = framed.append_section(section.name);
            for (const ghostlock::profile::glkv3::Entry &entry : section.entries) {
                bool ok = false;
                const uint64_t raw = raw_value(entry.value, ok);
                if (!ok) return -1;
                target.add(entry.key, raw);
            }
        }

        return bind_document(std::move(framed), route, terminal, backend, route,
                             out, release_buf, release_buf_cap, ids, document_out,
                             profile_43284_out);
    }
} // namespace ghostlock::binary_profile
