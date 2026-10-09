#pragma once
/** @file backend/cve_2026_43499/leak/scan_bounds.hpp
 *  How far a scan may step: one stride, and never past the end.
 *
 *  The leak search walks address ranges a stride at a time, and the walk has to stop
 *  at a boundary that came from the kernel rather than run off the end of the direct
 *  map. It also has to survive the arithmetic of reaching for the last byte: start
 *  plus step can wrap, and a wrapped address is the beginning of the direct map or of
 *  nothing at all -- either way not the place the scan meant to look.
 *
 *  So the rule is two steps in one: add saturating, then clamp to the end. The
 *  retired tree kept this in its own header with the same name, and its own test
 *  pinned four cases including the one that wraps; both are carried over here.
 */
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ghostlock::backend::cve_2026_43499::leak {
    /** The next address a scan looks at, given where it is, how far it steps and
     *  where it must not pass. A step that would wrap stops at the largest address,
     *  and the result never exceeds the end. */
    [[nodiscard]] constexpr std::size_t scan_limit(std::size_t start, std::size_t step,
                                                  std::size_t end) noexcept {
        const std::size_t room = std::numeric_limits<std::size_t>::max() - start;
        const std::size_t next = step > room ? std::numeric_limits<std::size_t>::max() : start + step;
        return next < end ? next : end;
    }
}  // namespace ghostlock::backend::cve_2026_43499::leak
