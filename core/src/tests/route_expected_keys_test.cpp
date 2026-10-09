/** @file tests/route_expected_keys_test.cpp
 *  What each route asks the document for, and that the asking is well formed.
 *
 *  Sections: 1. each route names a segment whose route token the catalogue knows,
 *  and the table covers exactly the routes there are; 2. no route asks for the
 *  same key twice and none asks for nothing; 3. the kinds are the ones the tree
 *  declared, spot-checked at the keys where getting it wrong would be silent.
 */
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "backend/cve_2026_43499/route/expected_keys.hpp"
#include "profile/route_catalog.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::route::ExpectedKey;
    using ghostlock::backend::cve_2026_43499::route::KeyKind;
    using ghostlock::backend::cve_2026_43499::route::key_kind_name;
    using ghostlock::backend::cve_2026_43499::route::kRouteKeys;
    using ghostlock::backend::cve_2026_43499::route::kRouteKeysCount;
    using ghostlock::backend::cve_2026_43499::route::RouteKeys;
    using ghostlock::profile::kNoRoute;
    using ghostlock::profile::kRouteCatalogCount;
    using ghostlock::profile::route_position;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** The route token inside a segment name: the part after the last dot of the
     *  route segment. */
    std::string_view route_token_of(std::string_view segment) {
        const std::string_view marker("route.");
        const std::size_t at = segment.find(marker);
        if (at == std::string_view::npos) {
            return {};
        }
        return segment.substr(at + marker.size());
    }

    /* ---- 1: the segments name real routes ---- */
    void the_segments_name_real_routes() {
        expect(kRouteKeysCount == kRouteCatalogCount,
               "there is one entry per route in the catalogue");
        for (std::size_t i = 0U; i < kRouteKeysCount; ++i) {
            const std::string_view segment(kRouteKeys[i].segment);
            expect(!segment.empty(), "a route names a segment");
            const std::string_view token = route_token_of(segment);
            expect(!token.empty(), "and the segment names a route token");
            expect(route_position(token) != kNoRoute,
                   "a token the catalogue does not know would be a route that does not exist");
            for (std::size_t j = i + 1U; j < kRouteKeysCount; ++j) {
                expect(std::strcmp(kRouteKeys[i].segment, kRouteKeys[j].segment) != 0,
                       "two routes do not share a segment");
            }
        }
    }

    /* ---- 2: no route asks twice, none asks for nothing ---- */
    void the_keys_are_well_formed() {
        for (std::size_t i = 0U; i < kRouteKeysCount; ++i) {
            const RouteKeys &route = kRouteKeys[i];
            expect(route.keys != nullptr && route.count > 0U, "a route asks for something");
            for (std::size_t a = 0U; a < route.count; ++a) {
                expect(route.keys[a].key != nullptr && route.keys[a].key[0U] != '\0',
                       "a key has a name");
                for (std::size_t b = a + 1U; b < route.count; ++b) {
                    expect(std::strcmp(route.keys[a].key, route.keys[b].key) != 0,
                           "a route does not ask for the same key twice");
                }
            }
        }
    }

    /* ---- 3: the kinds where a wrong one would be silent ---- */
    void the_kinds_are_the_declared_ones() {
        const auto kind_of = [](std::string_view segment_token, std::string_view key) {
            for (std::size_t i = 0U; i < kRouteKeysCount; ++i) {
                if (route_token_of(std::string_view(kRouteKeys[i].segment)) != segment_token) {
                    continue;
                }
                for (std::size_t k = 0U; k < kRouteKeys[i].count; ++k) {
                    const ExpectedKey &entry = kRouteKeys[i].keys[k];
                    if (std::string_view(entry.key) == key) {
                        return entry.kind;
                    }
                }
            }
            return KeyKind::Uint;
        };
        /* A signed shift read as unsigned would be a plausible number; a flag read
         * as a count likewise. These are the keys where that would go unnoticed. */
        expect(kind_of(std::string_view("multicast_waiter"), std::string_view("waiter_off")) ==
                   KeyKind::Int,
               "a shift offset is signed");
        expect(kind_of(std::string_view("select_stack"), std::string_view("waiter_shift")) ==
                   KeyKind::Int,
               "and so is the other shift");
        expect(kind_of(std::string_view("select_stack"), std::string_view("compact_waiter")) ==
                   KeyKind::Bool,
               "the compact-waiter choice is a flag");
        expect(kind_of(std::string_view("tcp_zerocopy"), std::string_view("payload_delta")) ==
                   KeyKind::Int,
               "a delta is signed");
        expect(kind_of(std::string_view("multicast_waiter"), std::string_view("arm_hold")) ==
                   KeyKind::Uint,
               "a hold is a count");
        expect(std::string_view(key_kind_name(KeyKind::Bool)) == std::string_view("bool"),
               "the kinds keep their names");
    }
}  // namespace

int main() {
    std::printf("route keys: the segments\n");
    the_segments_name_real_routes();
    std::printf("route keys: the keys\n");
    the_keys_are_well_formed();
    std::printf("route keys: the kinds\n");
    the_kinds_are_the_declared_ones();
    if (g_failures != 0U) {
        std::printf("route keys: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("route keys: PASS (segments are real routes + keys unique + kinds declared)\n");
    return 0;
}
