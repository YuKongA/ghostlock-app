#pragma once
/** @file backend/cve_2026_43499/route/route_policy.hpp
 *  What a route policy is, and where its compile-time capabilities come from.
 *
 *  A policy is a tag type: its capabilities are constants on the type and its run() is the
 *  per-write entry point. Stages read the capabilities of the concrete policy they were
 *  instantiated with, so every branch a route used to make at run time is a direct compile-time
 *  compare, and no indirect dispatch ever enters the window where the PI waiter is alive.
 *
 *  Adding a capability is one constant here, defaulted false, so a policy that needs it
 *  redeclares it and every other policy inherits the default untouched. This is the part of the
 *  retired file that carries no dependency beyond the result it reports; the parts that needed
 *  the write request and the target profile are a later piece, and the ported concept below is
 *  written so that they can arrive without the shape changing.
 */
#include <concepts>

#include "backend/cve_2026_43499/route/route_status.hpp"
#include <string_view>

namespace ghostlock::backend::cve_2026_43499::route {
    /** The result a route reports. Named here so the concept does not reach for the profile
     *  layer: what a policy produces is a route status and nothing about where it ran. */
    using RouteReport = RouteStatus;

    /** Capabilities, all false here. A policy that has one redeclares it; every other policy
     *  inherits the default and needs no edit when a new one is introduced. */
    struct RoutePolicyDefaults {
        static constexpr bool multicast = false;
        static constexpr bool w2_fast_repair = false;
        static constexpr bool w3_exact_target = false;
        static constexpr bool needs_two_words = false;
    };

    /** One route's behaviour, as the stages see it. A policy must say how it fails, offer a
     *  compile-time decision about a device profile, and run one write. The profile is a
     *  template parameter rather than an include, because the decision is taken at compile time
     *  against a type the composition root chooses. */
    template <class Policy, class Profile>
    concept RoutePolicy = requires(const Profile &profile) {
        { Policy::kind } -> std::convertible_to<std::string_view>;
        { Policy::supported(profile) } -> std::same_as<bool>;
    };

    /** The kind is a name rather than an enumeration: the vocabulary lives in the contract and
     *  the route table spells it as a string, and a policy here must not reach for either. A
     *  caller that needs the table looks it up by this name; a policy only has to say it. */
}  // namespace ghostlock::backend::cve_2026_43499::route
