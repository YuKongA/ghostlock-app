/**
 * @file dev_profile.h
 * @brief 写死的 5.15.189 dev profile（A301SO），仅供真机实验与 JNI v0 对拍。
 *
 * @note **临时物**：profile blob（Kotlin → native 的配置序列化）落地后删除，
 *       由调用方传入。当前它被 `dev/main_dev.cpp`、`dev/api_probe.cpp` 与
 *       `core/api.cpp` 共用，保证三条路径读到的是同一份参数、不会漂移。
 * @note 参数来源：`very_legacy/kernels/5.15.189-8100016-g51bba4309aac-ab14546557/offsets.h`
 *       的 `STRUCT_OFFSETS_5_15` 与该机型的 mcast 几何。
 * @note 阶段：P-A。
 */
#ifndef GHOSTLOCK_CORE_DEV_PROFILE_H
#define GHOSTLOCK_CORE_DEV_PROFILE_H

#include <cstdint>

#include "profile.h"

namespace ghostlock::core {
    /** @brief 本机 release 串（profile 匹配与日志用）。 */
    inline constexpr const char *kDevRelease =
        "5.15.189-android13-8-00016-g51bba4309aac-ab14546557";

    /** @brief 该机型的 selinux_enforcing 偏移（dev W1 的目标）。 */
    inline constexpr std::uint64_t kDevSelinuxEnforcingOff = 47885704ULL;

    /**
     * @brief 用写死的 5.15.189 参数填满 `kernel_offsets`。
     *
     * @param decoded 输出；调用方负责随后 `TargetProfile::from(&decoded)`。
     * @note 任何字段缺失都会让 route 拒绝发射或 panic，因此这里逐项写全。
     */
    inline void build_dev_515_profile(profile::kernel_offsets &decoded) {
        decoded.uname_r = kDevRelease;
        decoded.route = static_cast<std::uint8_t>(profile::RouteKind::MulticastWaiter);
        decoded.meta.kernel_major = 5;
        decoded.meta.recommend_shizuku = 1;
        decoded.meta.fallback_route =
            static_cast<std::uint8_t>(profile::RouteKind::Auto);
        decoded.meta.safe_mode = 0;

        decoded.offsets.init_task = 46412800ULL;
        decoded.offsets.init_cred = 46126472ULL;
        decoded.offsets.empty_zero_page = 47529984ULL;
        decoded.offsets.root_task_group = 47549120ULL;
        decoded.offsets.selinux_enforcing = kDevSelinuxEnforcingOff;
        decoded.offsets.selinux_blob_sizes = 35027656ULL;
        decoded.offsets.security_hook_heads = 35018304ULL;
        decoded.offsets.slide_nfulnl_logger = 45096488ULL;
        decoded.offsets.slide_boot_id = 47999001ULL;
        decoded.offsets.slide_loggers_0_1 = 45096280ULL;

        decoded.geometry.mcast_waiter_off = 96;
        decoded.geometry.mcast_buffer_size = 264;
        decoded.geometry.mcast_task_offset = 48;
        decoded.geometry.mcast_lock_offset = 56;
        decoded.misc.compact_waiter = 1;
        decoded.misc.kernelsnitch_collisions = 8;
        decoded.misc.mm_struct_sz = 0x400;

        /* 5.15.189 task_struct（STRUCT_OFFSETS_5_15, compact waiter）。 */
        decoded.task.prio = 0x7C;
        decoded.task.normal_prio = 0x84;
        decoded.task.sched_task_group = 0x400;
        decoded.task.pi_lock = 0x884;
        decoded.task.pi_waiters = 0x898;
        decoded.task.pi_top_task = 0x8A8;
        decoded.task.pi_blocked_on = 0x8B0;
        decoded.task.pid = 0x5D8;
        decoded.task.tgid = 0x5DC;
        decoded.task.atomic_flags = 0x598;
        decoded.task.real_cred = 0x790;
        decoded.task.cred = 0x798;
        decoded.task.comm = 0x7A8;
        decoded.task.tasks = 0x4D0;
        decoded.task.seccomp = 0x860;

        decoded.credential.copy_size = 0xb0;
        decoded.credential.usage_value = 0x100;
        decoded.credential.caps_offset = 0x30;
        decoded.credential.caps_count = 3;
        decoded.credential.caps_value = 0x000001ffffffffffULL;
        decoded.credential.ref_count = 4;
        decoded.credential.ref0_offset = 0x80;
        decoded.credential.ref1_offset = 0x88;
        decoded.credential.ref2_offset = 0x90;
        decoded.credential.ref3_offset = 0x98;
        decoded.credential.ref0_image = 0xffffffc00ab23a80ULL;
        decoded.credential.ref1_image = 0xffffffc00acce110ULL;
        decoded.credential.ref2_image = 0xffffffc00ab23ff0ULL;
        decoded.credential.ref3_image = 0xffffffc00ab23b28ULL;

        decoded.execution.recommended_main_cpu = 0;
        decoded.execution.recommended_consumer_cpu = 1;
        decoded.execution.heap_prepare_max_attempts = 6;
        decoded.execution.heap_prepare_timeout_ms = 240000;
        decoded.execution.heap_kernelsnitch_timeout_ms = 120000;
        decoded.execution.race_route_wait_ms = 1000;
        decoded.execution.race_route_done_timeout_ms = 300000;
        decoded.execution.race_setup_settle_us = 100000;
        decoded.execution.race_state_poll_interval_us = 1000;
        decoded.execution.select_enter_delay_us = 50000;
        decoded.execution.select_timeout_us = 200000;
        decoded.execution.select_consumer_burst_calls = 1;
        decoded.execution.select_consumer_max_calls = 1;
        decoded.execution.tcp_attempts = 2000;
        decoded.execution.tcp_arm_sequence = 16;
        decoded.execution.tcp_post_receive_hold_iterations = 20000;
        decoded.execution.mcast_attempts = 1;
        decoded.execution.mcast_arm_sequence = 1;
        decoded.execution.mcast_arm_hold = 100000000;
    }
} // namespace ghostlock::core

#endif // GHOSTLOCK_CORE_DEV_PROFILE_H
