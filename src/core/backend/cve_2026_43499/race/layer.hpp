#pragma once
/** @file backend/cve_2026_43499/race/layer.hpp
 *  The PI-futex race of the 43499 backend.
 *
 *  MOVED (2026-10-08, corrected after review): this was src/core/race, a
 *  top-level layer, which was wrong. The race is 43499's own mechanism -- the
 *  retired tree kept its futex machinery under this backend -- and the other
 *  backend has no PI-futex in it at all. A mechanism that belongs to one backend
 *  belongs inside that backend, so the layer list lost an entry rather than the
 *  race gaining a host of imaginary users. */

namespace ghostlock::backend::cve_2026_43499::race {
    /** The layer's design-family name, as frozen by the layer catalog. */
    inline constexpr const char *kLayerName = "race";
}  // namespace ghostlock::backend::cve_2026_43499::race
