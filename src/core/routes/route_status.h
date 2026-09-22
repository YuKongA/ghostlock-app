#ifndef ROUTE_STATUS_H
#define ROUTE_STATUS_H

#include <cstddef>
#include <type_traits>

namespace ghostlock::route {
    enum class RouteResultCode : int {
        Ok = 0,
        Retryable,
        FallbackSafe,
        DirtyFailure,
        Unsupported,
    };

    struct RouteStatus final {
        RouteResultCode code = RouteResultCode::Retryable;
        int step = 0;
        int error_number = 0;
        int userspace_clean = 0;
        int kernel_disarmed = 0;

        [[nodiscard]] constexpr bool is_clean() const noexcept {
            return userspace_clean != 0 && kernel_disarmed != 0;
        }

        [[nodiscard]] constexpr bool is_dirty() const noexcept {
            return code == RouteResultCode::DirtyFailure || !is_clean();
        }

        [[nodiscard]] constexpr bool can_fallback() const noexcept {
            return code == RouteResultCode::FallbackSafe && is_clean();
        }
    };

    static_assert(std::is_standard_layout_v<RouteStatus>);
    static_assert(std::is_trivially_copyable_v<RouteStatus>);
    static_assert(sizeof(RouteStatus) == sizeof(int) * 5);
    static_assert(alignof(RouteStatus) == alignof(int));
    static_assert(offsetof(RouteStatus, code) == 0);
    static_assert(offsetof(RouteStatus, step) == sizeof(int));
    static_assert(offsetof(RouteStatus, error_number) == sizeof(int) * 2);
    static_assert(offsetof(RouteStatus, userspace_clean) == sizeof(int) * 3);
    static_assert(offsetof(RouteStatus, kernel_disarmed) == sizeof(int) * 4);

    inline constexpr RouteResultCode ROUTE_OK = RouteResultCode::Ok;
    inline constexpr RouteResultCode ROUTE_RETRYABLE = RouteResultCode::Retryable;
    inline constexpr RouteResultCode ROUTE_FALLBACK_SAFE =
            RouteResultCode::FallbackSafe;
    inline constexpr RouteResultCode ROUTE_DIRTY_FAILURE =
            RouteResultCode::DirtyFailure;
    inline constexpr RouteResultCode ROUTE_UNSUPPORTED =
            RouteResultCode::Unsupported;
} // namespace ghostlock::route

#endif
