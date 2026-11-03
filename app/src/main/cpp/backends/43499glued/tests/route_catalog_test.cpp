#include "profile/model.h"

#include <cassert>
#include <cstdio>
#include <iterator>
#include <string_view>

using namespace ghostlock;

/* Cross-language agreement anchor: the canonical (token, wire) set. The Kotlin
 * RouteCatalogAgreementTest asserts the same list; drift on either side fails
 * its own test. */
int32_t main(void) {
    struct Row {
        const char *token;
        uint8_t wire;
    };
    const Row expected[] = {
        {"tcp_zerocopy", 1},
        {"select_stack", 2},
        {"multicast_waiter", 3},
    };
    assert(std::size(profile::kRouteCatalog) == std::size(expected));
    for (size_t i = 0; i < std::size(expected); i++) {
        assert(std::string_view(profile::kRouteCatalog[i].token) == expected[i].token);
        assert(profile::kRouteCatalog[i].wire == expected[i].wire);
        assert(profile::route_kind_from_string(expected[i].token) == expected[i].wire);
    }
    puts("route_catalog_test: ok");
    return 0;
}
