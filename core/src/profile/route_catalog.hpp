#pragma once
/** @file profile/route_catalog.hpp
 *  The route vocabulary of the wire contract: token <-> wire value.
 *
 *  Mechanism constant exception (Q2 rule 2): the wire values below are protocol
 *  identity -- the GLK profile carries the route token and the runtime maps it
 *  to this number -- so they belong in C++. They are NOT tuning knobs, and no
 *  route geometry (addresses, offsets, timings, attempt counts) appears here:
 *  that data comes from the GLK profile assets and their HOCON defaults.
 *
 *  Single authority: a route token exists here once. The op registry
 *  (contract/op_registry.hpp) derives its route bits from this catalogue, so a
 *  route can never be added in two places by mistake.
 */
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ghostlock::profile {
    /** One catalogue row: the token the profile spells and its wire value. */
    struct RouteCatalogEntry final {
        std::string_view token;
        std::uint8_t wire;
    };

    /** The wired routes (protocol identity; see the file comment). */
    inline constexpr RouteCatalogEntry kRouteCatalog[] = {
        {"tcp_zerocopy", 1},
        {"select_stack", 2},
        {"multicast_waiter", 3},
    };
    inline constexpr std::size_t kRouteCatalogCount = sizeof(kRouteCatalog) / sizeof(kRouteCatalog[0]);


    /** The routes, as a type, bound to the catalogue above the way the backend ids are bound to
     *  the component catalogue: a route added there without a row here stops the build. The
     *  wire values are the catalogue's own and are not repeated here; this is an identity for
     *  callers, not a second table. */
    enum class RouteId : std::uint8_t {
        /** Not decided; what a parameter set carries until a caller says which route. */
        Unset = 0,
        TcpZerocopy = 1,
        SelectStack = 2,
        MulticastWaiter = 3,
    };
    inline constexpr std::size_t kRouteIdCount = static_cast<std::size_t>(RouteId::MulticastWaiter) + 1U;

    /** The token a route id spells, or empty for Unset. */
    [[nodiscard]] constexpr std::string_view route_id_token(RouteId id) noexcept {
        switch (id) {
            case RouteId::Unset: return {};
            case RouteId::TcpZerocopy: return "tcp_zerocopy";
            case RouteId::SelectStack: return "select_stack";
            case RouteId::MulticastWaiter: return "multicast_waiter";
        }
        return {};
    }

    /** The catalogue token for a route id is the row with the same wire value. Bound, not
     *  copied: a rename on either side stops the build. */
    [[nodiscard]] constexpr bool route_ids_match_the_catalogue() noexcept {
        for (std::size_t i = 0U; i < kRouteCatalogCount; ++i) {
            const RouteId id = static_cast<RouteId>(kRouteCatalog[i].wire);
            const std::string_view known = route_id_token(id);
            const std::string_view token = kRouteCatalog[i].token;
            if (known.size() != token.size()) { return false; }
            for (std::size_t c = 0U; c < known.size(); ++c) {
                if (known[c] != token[c]) { return false; }
            }
        }
        return kRouteCatalogCount + 1U == kRouteIdCount;
    }
    static_assert(route_ids_match_the_catalogue(),
                  "the route ids no longer match the route catalogue");

    /** Sentinel for "this token names no route". */
    inline constexpr std::size_t kNoRoute = static_cast<std::size_t>(-1);

    /** Position of a token in the catalogue above; kNoRoute when absent. */
    [[nodiscard]] constexpr std::size_t route_position(std::string_view token) noexcept {
        for (std::size_t i = 0; i < kRouteCatalogCount; ++i) {
            if (token == kRouteCatalog[i].token) {
                return i;
            }
        }
        return kNoRoute;
    }

    /* Named positions: derived here so that every other file refers to a route
     * by this constant instead of spelling the token again (single authority). */
    inline constexpr std::size_t kRouteTcpZerocopy = route_position("tcp_zerocopy");
    inline constexpr std::size_t kRouteSelectStack = route_position("select_stack");
    inline constexpr std::size_t kRouteMulticastWaiter = route_position("multicast_waiter");
    static_assert(kRouteTcpZerocopy == 0 && kRouteSelectStack == 1 && kRouteMulticastWaiter == 2,
                  "the named route positions must track the catalogue order");

    /** Wire values are non-zero and distinct: two rows cannot alias one route. */
    [[nodiscard]] constexpr bool route_wires_are_distinct() noexcept {
        for (std::size_t i = 0; i < kRouteCatalogCount; ++i) {
            if (kRouteCatalog[i].wire == 0 || kRouteCatalog[i].token.empty()) {
                return false;
            }
            for (std::size_t j = i + 1; j < kRouteCatalogCount; ++j) {
                if (kRouteCatalog[i].wire == kRouteCatalog[j].wire ||
                    kRouteCatalog[i].token == kRouteCatalog[j].token) {
                    return false;
                }
            }
        }
        return true;
    }

    static_assert(kRouteCatalogCount == 3, "the wired route set is frozen at three");
    static_assert(route_wires_are_distinct(), "route tokens and wire values must be unique");
}  // namespace ghostlock::profile
