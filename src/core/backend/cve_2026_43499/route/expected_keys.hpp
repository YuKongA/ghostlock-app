#pragma once
/** @file backend/cve_2026_43499/route/expected_keys.hpp
 *  What each route needs from the document, declared where the route lives.
 *
 *  A route reads a handful of keys out of its own segment, and each key has a
 *  kind: a count, a signed shift, or a flag. The declaration belongs here, with
 *  the route, and the kinds are spelled in a local enum so this layer does not
 *  reach for the profile layer (which reads the document and sits after every
 *  other layer). The composition root, which sees both, is what matches a
 *  document against this table.
 *
 *  A key that is absent and a key of the wrong kind are different failures, and
 *  a route cannot tell them apart from a value alone -- which is why the kind is
 *  declared rather than inferred.
 */
#include <cstddef>

namespace ghostlock::backend::cve_2026_43499::route {
    /** The kinds a route key may have, mirroring what the wire carries. */
    enum class KeyKind : unsigned char {
        Uint = 0,
        Int = 1,
        Bool = 2,
    };

    [[nodiscard]] constexpr const char *key_kind_name(KeyKind kind) noexcept {
        switch (kind) {
            case KeyKind::Uint: return "uint";
            case KeyKind::Int: return "int";
            case KeyKind::Bool: return "bool";
        }
        return "unknown";
    }

    struct ExpectedKey final {
        const char *key;
        KeyKind kind;
    };

    /* Provenance: the per-route key list and the kind of each key are the ones
     * the tree already declared for these segments (the retired schema named
     * them together with their wire types). Nothing here is a new key. */

    inline constexpr ExpectedKey kTcpZerocopyKeys[] = {
        {"post_receive_hold_iterations", KeyKind::Uint},
        {"payload_delta", KeyKind::Int},
        {"chunk_bias", KeyKind::Uint},
        {"fake_task_off", KeyKind::Uint},
        {"cred_copy_off", KeyKind::Uint},
    };

    inline constexpr ExpectedKey kSelectStackKeys[] = {
        {"enter_delay_us", KeyKind::Uint},
        {"timeout_us", KeyKind::Uint},
        {"waiter_shift", KeyKind::Int},
        {"compact_waiter", KeyKind::Bool},
    };

    inline constexpr ExpectedKey kMulticastWaiterKeys[] = {
        {"attempts", KeyKind::Uint},
        {"arm_sequence", KeyKind::Uint},
        {"arm_hold", KeyKind::Uint},
        {"waiter_off", KeyKind::Int},
        {"buffer_size", KeyKind::Uint},
        {"task_offset", KeyKind::Uint},
        {"lock_offset", KeyKind::Uint},
    };

    /** One route's segment and the keys it expects there. The route token comes
     *  from the single route catalogue and is only referenced here. */
    struct RouteKeys final {
        const char *segment;
        const ExpectedKey *keys;
        std::size_t count;
    };

    inline constexpr RouteKeys kRouteKeys[] = {
        {"backend.cve_2026_43499.route.tcp_zerocopy", kTcpZerocopyKeys,
         sizeof(kTcpZerocopyKeys) / sizeof(kTcpZerocopyKeys[0])},
        {"backend.cve_2026_43499.route.select_stack", kSelectStackKeys,
         sizeof(kSelectStackKeys) / sizeof(kSelectStackKeys[0])},
        {"backend.cve_2026_43499.route.multicast_waiter", kMulticastWaiterKeys,
         sizeof(kMulticastWaiterKeys) / sizeof(kMulticastWaiterKeys[0])},
    };
    inline constexpr std::size_t kRouteKeysCount = sizeof(kRouteKeys) / sizeof(kRouteKeys[0]);
}  // namespace ghostlock::backend::cve_2026_43499::route
