#pragma once
/** @file contract/layer.hpp
 *  A0 skeleton placeholder for the contract layer (rewrite-plan-v2 section 2).
 *
 *  Purpose: give the layer a translation-unit-visible identity so the host
 *  entry can enumerate the design family and so later phases can fill the
 *  layer in place. It deliberately declares no behaviour and no numeric value
 *  (Q2 rule 1: numbers live in the profile, never in C++).
 */

namespace ghostlock::contract {
    /** The layer's design-family name, as frozen by the layer catalog. */
    inline constexpr const char *kLayerName = "contract";
}  // namespace ghostlock::contract
