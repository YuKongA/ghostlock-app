/**
 * @file route_multicast.h
 * @brief multicast（`MCAST_BLOCK_SOURCE`）物理机制的资源所有权与生命周期。
 */
#ifndef GHOSTLOCK_CORE_ROUTE_MULTICAST_H
#define GHOSTLOCK_CORE_ROUTE_MULTICAST_H

#include "route.h"
#include "memory.h"
#include "support.h"
#include "params.h"

/* ==== route/multicast_route.h ==== */
#ifndef MULTICAST_ROUTE_H
#define MULTICAST_ROUTE_H

namespace ghostlock::route::multicast {
    /**
     * @brief 一次 multicast 单发尝试的全部用户态资源与生命周期。
     *
     * 生命周期与另外两条 route 相同：`prepare() -> execute() -> disarm() ->
     * destroy()`，由 `run_route_lifecycle()` 驱动。所有系统调用都封在本类内
     * （socket/setsockopt/close）；路由入口只做装配与结果日志，不再直接持有
     * fd，也不再直接调用 `socket()`。
     *
     * @note 关键不变量（真机付过代价）：`execute()` 内 `MCAST_BLOCK_SOURCE`
     *       的拷贝与 `consumer_go` 的置位之间**不得出现任何 syscall**，否则
     *       waiter 的内核栈帧被冲掉，通常直接 panic。该窗口因此只能在同一线程
     *       的同一帧内完成。
     */
    class MulticastRoute final {
    public:
        explicit MulticastRoute(const ghostlock::core::MulticastInputs &inputs) noexcept;

        ~MulticastRoute() noexcept = default;

        MulticastRoute(const MulticastRoute &) = delete;

        MulticastRoute &operator=(const MulticastRoute &) = delete;

        MulticastRoute(MulticastRoute &&) = delete;

        /**
         * @brief 校验几何/执行参数并取得 socket。
         *
         * @return 0 表示可以发射；非 0 时 `status` 已带 step/errno，调用方不得
         *         继续 `execute()`（`run_route_lifecycle` 已按此约定实现）。
         * @note 拒绝发射不消耗尝试次数，也不建立任何可泄漏资源。
         */
        [[nodiscard]] int32_t prepare() noexcept;

        /**
         * @brief 下毒 + arm + 等 consumer 走链。
         *
         * @return 本次尝试的 `RouteStatus`（成功为 `ROUTE_OK`）。
         */
        [[nodiscard]] ghostlock::route::RouteStatus execute() noexcept;

        /** @brief 收 consumer（go=0 并排空 inflight），标记内核侧已解除。 */
        void disarm() noexcept;

        /** @brief 关闭 socket 并标记用户态干净；未成功且已解除则降级为 fallback-safe。 */
        void destroy() noexcept;

        [[nodiscard]] int32_t fail(int32_t step, int32_t error_number) noexcept;

        ghostlock::core::MulticastInputs in;
        ghostlock::support::UniqueFd socket_fd;
        ghostlock::route::RouteStatus status{};
        /** @brief 实际用掉的尝试次数（日志与判据用）。 */
        int32_t attempts_used = 0;
        /** @brief 最后一次 `setsockopt` 的返回值与 errno。 */
        int32_t stamp_result = -1;
        int32_t stamp_errno = 0;
    };
} // namespace ghostlock::route::multicast

#endif

#endif // GHOSTLOCK_CORE_ROUTE_MULTICAST_H
