#ifndef KERNELSNITCH_SCAN_BOUNDS_H
#define KERNELSNITCH_SCAN_BOUNDS_H

#include <cstddef>

/* Return min(start + span, range_end) without overflowing size_t. */
namespace ghostlock::kernelsnitch {

static inline size_t scan_limit(
    size_t start, size_t span, size_t range_end) {
    if (start >= range_end || span >= range_end - start) return range_end;
    return start + span;
}

} // namespace ghostlock::kernelsnitch

#endif
