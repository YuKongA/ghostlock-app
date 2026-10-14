#pragma once
/** @file jni/route_result.hpp
 *  The five fields one route attempt reports, in one place.
 *
 *  Two files need this shape: the route that produces it and the context the three threads
 *  carry while a run is in flight. Writing it twice would be the same mistake this tree keeps
 *  naming -- a second copy of one fact. The field names and order match the retired
 *  RouteStatus: code, step, error_number, userspace_clean, kernel_disarmed.
 */
#include <cstdint>

namespace ghostlock::jni {
    /** What one attempt reports. code == 0 means success. */
    struct RouteResult final {
        std::int32_t code = 0;
        std::int32_t step = 0;
        std::int32_t error_number = 0;
        bool userspace_clean = false;
        bool kernel_disarmed = false;
    };
}  // namespace ghostlock::jni
