#include "backend/cve_2026_43499/backend_profile.hpp"

#include "backend/cve_2026_43499/schema.hpp"
#include "platform/abi.hpp"
#include "profile/document.hpp"
#include "backend/cve_2026_43499/backend_profile/model.hpp"
#include "profile/schema.hpp"

#include <cstring>
#include <string_view>

namespace ghostlock::backend::cve_2026_43499::backend_profile {
    namespace {
        constexpr std::string_view kStepsSection = "backend.cve_2026_43499";
        constexpr std::string_view kStepsKey = "steps";

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

    profile::BindStatus bind(const profile::Document &document, uint8_t route,
                             profile::kernel_offsets *out, char *release_buf,
                             size_t release_buf_cap) {
        if (out == nullptr || release_buf == nullptr) {
            return profile::BindStatus{profile::BindCode::Invalid, {}, {}};
        }
        /* The 43499 backend always has an explicit route (the route-less value
         * is reserved for 43284), so an unresolved route is rejected. */
        const std::string_view active_route = route_section_name(route);
        if (active_route.empty()) {
            return profile::BindStatus{profile::BindCode::Invalid, {}, {}};
        }
        if (document.release.size() + 1 > release_buf_cap) {
            return profile::BindStatus{profile::BindCode::Invalid, {}, {}};
        }

        /* Only the document's own route section is materialised; the other
         * known route sections are dropped exactly as the legacy field walk
         * skipped them. Unknown sections stay in for the mode to judge. */
        profile::Document active;
        active.release = document.release;
        active.terminal = document.terminal;
        active.backend = document.backend;
        active.middleware = document.middleware;
        for (const profile::Section &section : document.sections) {
            const bool known_route = section.name == "backend.cve_2026_43499.route.tcp_zerocopy" ||
                                     section.name == "backend.cve_2026_43499.route.select_stack" ||
                                     section.name == "backend.cve_2026_43499.route.multicast_waiter";
            if (known_route && section.name != active_route) continue;
            active.sections.push_back(section);
        }

        /* Two owners share sections (cred/offset/kernel): bind the union so
         * strict validation sees one ownership picture, then mechanically merge
         * the platform View into the frozen transport. */
        platform::abi::View abi_view{};
        Cve2026_43499View view{};
        const profile::BindStatus status =
                profile::bind_all<platform::abi::Schema, Cve2026_43499Schema>(
                        active, profile::DecodeMode::Production, abi_view, view);
        if (!status.ok()) return status;
        platform::abi::apply_to(abi_view, view.values);

        memcpy(release_buf, document.release.data(), document.release.size());
        release_buf[document.release.size()] = '\0';
        *out = view.values;
        out->uname_r = release_buf;
        out->route = route;
        return status;
    }

    uint16_t steps_from(const profile::Document &document) noexcept {
        const profile::Value *value = document.find_value(kStepsSection, kStepsKey);
        return value != nullptr ? static_cast<uint16_t>(value->raw) : 0;
    }
} // namespace ghostlock::backend::cve_2026_43499::backend_profile
