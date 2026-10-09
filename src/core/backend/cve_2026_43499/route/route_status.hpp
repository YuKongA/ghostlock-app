#pragma once
/** @file backend/cve_2026_43499/route/route_status.hpp
 *  What a route reports when it runs one write.
 *
 *  PORTED (2026-10-08) from the retired tree's route/route_status.h, kept because the
 *  distinction it draws is the one that matters after a failure: whether the kernel was
 *  left armed and whether the userspace side was left clean. A route that failed is a
 *  route that failed; a route that failed while the kernel still held something is a
 *  different problem, and the caller acts on them differently.
 *
 *  The layout assertions are the retired tree's own and are kept as they were: this struct
 *  is small, standard-layout and trivially copyable on purpose, and a field reordered by
 *  accident should stop the build rather than quietly change what a caller reads.
 */
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ghostlock::backend::cve_2026_43499::route {
    /** How a write ended, in the terms the caller has to act on. */
    enum class RouteResultCode : std::int32_t {
        /** The write went through. */
        Ok = 0,
        /** Nothing was written and nothing is held; trying again is reasonable. */
        Retryable = 1,
        /** Nothing was written, but the route can be left for another to try. */
        FallbackSafe = 2,
        /** Something was left behind; the next attempt must not assume a clean kernel. */
        DirtyFailure = 3,
        /** The route cannot run at all against what it was given. */
        Unsupported = 4,
    };

    /** One write's outcome. */
    struct RouteStatus final {
        RouteResultCode code = RouteResultCode::Retryable;
        /** Which step of the route was running. */
        std::int32_t step = 0;
        /** The errno beside the failure, when there was one. */
        std::int32_t error_number = 0;
        /** Whether the userspace side was put back. */
        std::int32_t userspace_clean = 0;
        /** Whether the kernel side was disarmed. */
        std::int32_t kernel_disarmed = 0;

        [[nodiscard]] constexpr bool is_clean() const noexcept {
            return userspace_clean != 0 && kernel_disarmed != 0;
        }

        /** Dirty is wider than a dirty code: a failure that left the kernel armed is dirty
         *  whatever it called itself. */
        [[nodiscard]] constexpr bool is_dirty() const noexcept {
            return code == RouteResultCode::DirtyFailure || !is_clean();
        }
    };

    static_assert(std::is_standard_layout_v<RouteStatus>, "a status is a plain record");
    static_assert(std::is_trivially_copyable_v<RouteStatus>, "and can be copied as bits");
    static_assert(sizeof(RouteStatus) == sizeof(std::int32_t) * 5U, "five fields, no padding");
    static_assert(alignof(RouteStatus) == alignof(std::int32_t), "aligned as its fields are");
    static_assert(offsetof(RouteStatus, code) == 0U, "the code comes first");
    static_assert(offsetof(RouteStatus, step) == sizeof(std::int32_t), "then the step");
    static_assert(offsetof(RouteStatus, error_number) == sizeof(std::int32_t) * 2U,
                  "then the errno");
    static_assert(offsetof(RouteStatus, userspace_clean) == sizeof(std::int32_t) * 3U,
                  "then whether userspace was put back");
    static_assert(offsetof(RouteStatus, kernel_disarmed) == sizeof(std::int32_t) * 4U,
                  "and whether the kernel was disarmed");
}  // namespace ghostlock::backend::cve_2026_43499::route
