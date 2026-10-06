#include "profile_bind_compat.hpp"

#include "backend/cve_2026_43284/schema.hpp"
#include "backend/cve_2026_43499/schema.hpp"
#include "pipeline/component_catalog.hpp"
#include "platform/abi.hpp"
#include "profile/glkv3_parse.hpp"
#include "profile/schema.hpp"

#include <cstring>
#include <string_view>
#include <utility>

namespace ghostlock::tests::profile_bind {
    namespace {
        std::string_view route_section_name(uint8_t route) {
            switch (route) {
                case profile::kRouteTcpZerocopy:
                    return "backend.cve_2026_43499.route.tcp_zerocopy";
                case profile::kRouteSelectStack:
                    return "backend.cve_2026_43499.route.select_stack";
                case profile::kRouteMulticastWaiter:
                    return "backend.cve_2026_43499.route.multicast_waiter";
                default:
                    return {};
            }
        }
    } // namespace

    int32_t bind_document(profile::Document &&document, uint8_t route,
                          uint16_t terminal, uint16_t backend,
                          uint16_t middleware, profile::kernel_offsets *out,
                          char *release_buf, size_t release_buf_cap,
                          component_ids *ids, profile::Document *document_out,
                          ghostlock::backend::Cve2026_43284Profile *profile_43284_out) {
        if (!out || !release_buf) return -1;
        if (!terminal_known(terminal) || !backend_known(backend)) return -1;
        if (middleware > 0xff) return -1;
        const bool route_known =
                route == profile::kRouteTcpZerocopy ||
                route == profile::kRouteSelectStack ||
                route == profile::kRouteMulticastWaiter;
        /* F3: a 43284 document is route-less. The current value is kRouteNone;
         * the legacy v1/v2 wire value 0 (kRouteAuto) is still accepted by this
         * test-only compatibility shim. */
        const bool route_less_43284 =
                backend == static_cast<uint16_t>(contract::BackendKind::Cve2026_43284) &&
                (route == profile::kRouteNone || route == profile::kRouteAuto);
        if (!route_known && !route_less_43284) return -1;
        if (document.release.size() + 1 > release_buf_cap) return -1;

        profile::Document active;
        active.release = document.release;
        active.terminal = document.terminal;
        active.backend = document.backend;
        active.middleware = document.middleware;
        const std::string_view active_route = route_section_name(route);
        for (const profile::Section &section : document.sections) {
            const bool known_route = section.name == "backend.cve_2026_43499.route.tcp_zerocopy" ||
                                     section.name == "backend.cve_2026_43499.route.select_stack" ||
                                     section.name == "backend.cve_2026_43499.route.multicast_waiter";
            if (known_route && section.name != active_route) continue;
            active.sections.push_back(section);
        }

        ghostlock::backend::Cve2026_43284Profile profile_43284{};
        if (backend == static_cast<uint16_t>(contract::BackendKind::Cve2026_43284)) {
            profile::Document owned_43284;
            owned_43284.release = active.release;
            owned_43284.terminal = active.terminal;
            owned_43284.backend = active.backend;
            owned_43284.middleware = active.middleware;
            for (auto it = active.sections.begin(); it != active.sections.end();) {
                /* Both owner sections (backend + execution) move to the 43284
                 * copy; the predicate is the same one production uses. */
                if (ghostlock::backend::is_cve_2026_43284_section(it->name)) {
                    owned_43284.sections.push_back(*it);
                    it = active.sections.erase(it);
                } else {
                    ++it;
                }
            }
            const profile::BindStatus status_43284 =
                    profile::bind<ghostlock::backend::Cve2026_43284Schema>(
                            owned_43284, profile_43284,
                            profile::DecodeMode::Production);
            if (!status_43284.ok()) return -1;
        }

        /* Same composition as production backend_profile::bind: union bind of
         * the platform ABI owner and the 43499 backend owner, then the
         * mechanical platform-View merge into the frozen transport. */
        platform::abi::View abi_view{};
        backend::Cve2026_43499View view{};
        const profile::BindStatus status =
                profile::bind_all<platform::abi::Schema, backend::Cve2026_43499Schema>(
                        active, profile::DecodeMode::Production, abi_view, view);
        if (!status.ok()) return -1;
        platform::abi::apply_to(abi_view, view.values);

        memcpy(release_buf, document.release.data(), document.release.size());
        release_buf[document.release.size()] = '\0';

        *out = view.values;
        out->uname_r = release_buf;
        out->route = route;
        uint16_t steps = view.steps;
        if (backend == static_cast<uint16_t>(contract::BackendKind::Cve2026_43284) && profile_43284.steps) {
            steps = *profile_43284.steps;
        }
        document.steps = steps;
        if (ids) *ids = {terminal, backend, middleware, steps};
        if (profile_43284_out) *profile_43284_out = profile_43284;
        if (document_out) *document_out = std::move(document);
        return 0;
    }

    int32_t parse_v3(std::string_view document, profile::kernel_offsets *out,
                     char *release_buf, size_t release_buf_cap, component_ids *ids,
                     profile::Document *document_out,
                     ghostlock::backend::Cve2026_43284Profile *profile_43284_out) {
        profile::Document framed;
        if (profile::frame_v3(document, &framed) != 0) return -1;
        contract::TerminalKind terminal_kind{};
        contract::BackendKind backend_kind{};
        if (!pipeline::terminal_from_token(framed.terminal_token, terminal_kind)) {
            return -1;
        }
        if (!pipeline::backend_from_token(framed.backend_token, backend_kind)) {
            return -1;
        }
        const uint8_t route = static_cast<uint8_t>(framed.middleware);
        const uint16_t terminal = static_cast<uint16_t>(terminal_kind);
        const uint16_t backend = static_cast<uint16_t>(backend_kind);
        return bind_document(std::move(framed), route, terminal, backend, route,
                             out, release_buf, release_buf_cap, ids, document_out,
                             profile_43284_out);
    }
} // namespace ghostlock::tests::profile_bind
