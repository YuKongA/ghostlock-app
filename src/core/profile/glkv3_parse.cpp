#include "profile/glkv3_parse.hpp"

#include "contract/identity.hpp"
#include "contract/model.hpp"
#include "profile/document.hpp"
#include "profile/glkv3.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace ghostlock::profile {
    namespace {
        /* R2 owner-qualified sections: a section must name a known owner
         * prefix ("backend.<id>.*", "platform.<module>.*",
         * "countermeasure.<id>.*") or be the public "common" section. An
         * unknown prefix is rejected here, fail-closed, before any owner bind. */
        bool known_owner_section(std::string_view name) {
            /* Owner prefixes: common + the three owner-qualified families.
             * plugin is the third top-level owner (S4 P1): one plugin serves
             * several backends, so its section is orthogonal to them. */
            return name == "common" || name.starts_with("backend.") ||
                   name.starts_with("platform.") ||
                   name.starts_with("countermeasure.") ||
                   name.starts_with("plugin.");
        }

        /* S4 R6b legacy compatibility: the old uint step id (1 = W1W2,
         * 2 = W1W3, 3 = PageCacheWrite) maps onto the equivalent token using the
         * root route, so a pre-R6b document keeps its exact behaviour. */
        bool legacy_43499_kind(RouteKind route, uint64_t id,
                               contract::CombinationKind &out) noexcept {
            const bool shizuku = id == 1;  /* W1W2 */
            const bool rootchild = id == 2; /* W1W3 */
            if (!shizuku && !rootchild) return false;
            switch (route) {
                case RouteKind::MulticastWaiter:
                    out = shizuku ? contract::CombinationKind::McastShizuku
                                  : contract::CombinationKind::McastRootchild;
                    return true;
                case RouteKind::SelectStack:
                    out = shizuku ? contract::CombinationKind::PselectShizuku
                                  : contract::CombinationKind::PselectRootchild;
                    return true;
                case RouteKind::TcpZerocopy:
                    out = shizuku ? contract::CombinationKind::TcpShizuku
                                  : contract::CombinationKind::TcpRootchild;
                    return true;
                default:
                    return false;
            }
        }

        bool legacy_43284_kind(uint64_t id, contract::CombinationKind &out) noexcept {
            if (id != 3) return false; /* PageCacheWrite */
            out = contract::CombinationKind::Umh;
            return true;
        }

        /* Resolve backend.<id>.steps to a combination token and install the
         * derived route / terminal / step set. A new string value is used
         * verbatim; a legacy uint is mapped through the root route and the
         * legacy id is reported as a diagnostic. Unknown tokens, a route or a
         * terminal that disagrees with the token, and an absent step key all
         * fail closed. */
        int32_t resolve_combination(Document *out,
                                    const glkv3::Document &decoded) noexcept {
            contract::BackendKind backend{};
            if (!contract::backend_kind_from_token(decoded.backend, backend)) return -1;

            std::string section_name = "backend.";
            section_name.append(contract::backend_token_name(backend));
            Section *section = out->find_section(section_name);
            if (section == nullptr) return -1;
            Entry *steps_entry = nullptr;
            for (Entry &entry : section->entries) {
                if (entry.key == "steps") steps_entry = &entry;
            }
            if (steps_entry == nullptr) return -1;

            contract::CombinationKind combination = contract::CombinationKind::Unknown;
            if (steps_entry->value.is_text) {
                if (!contract::combination_resolve(backend, steps_entry->value.text,
                                                   combination)) {
                    (void)std::fprintf(stderr, "unknown combination token=%.*s\n",
                                       static_cast<int>(steps_entry->value.text.size()),
                                       steps_entry->value.text.data());
                    return -1;
                }
            } else {
                const uint64_t legacy_id = steps_entry->value.raw;
                (void)std::fprintf(stderr, "legacy_steps_id=%llu\n",
                                   static_cast<unsigned long long>(legacy_id));
                /* Legacy v1/v2 decode: an absent root route means "no route
                 * declared" (None). The legacy id resolver only accepts a
                 * catalogue route, so a 43499 legacy id without geometry fails
                 * closed and a 43284 legacy id is route-less by construction. */
                const RouteKind route =
                        decoded.has_route
                                ? static_cast<RouteKind>(
                                          route_kind_from_string(decoded.route))
                                : RouteKind::None;
                const bool legacy_ok =
                        backend == contract::BackendKind::Cve2026_43499
                                ? legacy_43499_kind(route, legacy_id, combination)
                                : (backend == contract::BackendKind::Cve2026_43284 &&
                                   legacy_43284_kind(legacy_id, combination));
                if (!legacy_ok) return -1;
            }

            const contract::CombinationSpec *spec = contract::combination_spec(combination);
            if (spec == nullptr) return -1;

            /* F3 fail-closed route agreement: the root route, when present,
             * must name the token's declared route; an absent route is None,
             * which a token that requires a route never matches. A backend
             * without a route axis (None) must not name one, and an unknown
             * route token resolves to None instead of the legacy Auto. */
            const RouteKind declared_route =
                    decoded.has_route
                            ? static_cast<RouteKind>(
                                      route_kind_from_string(decoded.route))
                            : RouteKind::None;
            if (declared_route != spec->route) return -1;

            contract::TerminalKind terminal{};
            if (!contract::terminal_kind_from_token(decoded.terminal, terminal) ||
                terminal != spec->terminal) {
                return -1;
            }

            out->combination = static_cast<uint8_t>(combination);
            out->middleware = static_cast<uint16_t>(spec->route);
            const std::string_view terminal_token =
                    contract::terminal_token_name(spec->terminal);
            out->terminal_token.assign(terminal_token.data(), terminal_token.size());

            /* Canonicalise the owner slot to the resolved token text: the owner
             * Schema declares steps as a String, so a legacy uint is rewritten
             * to its equivalent token (the token literal has static lifetime). */
            steps_entry->value = Value{};
            steps_entry->value.present = true;
            steps_entry->value.is_text = true;
            steps_entry->value.text = spec->token;
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
        /* No route is selected yet; resolve_combination installs the token's
         * declared route (None for a backend without a route axis). */
        out->middleware = profile::kRouteNone;

        for (const profile::glkv3::Section &section : decoded.sections) {
            if (!known_owner_section(section.name)) return -1;
            profile::Section &target = out->append_section(section.name);
            for (const profile::glkv3::Entry &entry : section.entries) {
                /* S4 R4: preserve the wire type. Numeric slots become raw
                 * values; a str becomes a non-owning text view into the same
                 * decode buffer (frame_v3's caller owns it). bin/array are not
                 * part of the owner section vocabulary and fail closed. */
                switch (entry.value.type) {
                    /* S4 P1: Union is a manifest-level declaration for the
                     * dynamic plugin keys; a concrete value always carries its
                     * scalar kind, so a Union here is rejected. */
                    case profile::glkv3::WireType::Union:
                        return -1;
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
        return resolve_combination(out, decoded);
    }
} // namespace ghostlock::profile
