/*
 * GhostLock - vr.ko per-task tag removal execution (Android only).
 *
 * The plan (`plan_vr_task_tag` in vr_task_tag.hpp) is pure address arithmetic;
 * this unit carries the parts that need the device: the runtime applicability
 * check and the two kernel writes, which arrive through `AncillaryOps` injected
 * by the backend call site, so this module names no backend type.
 *
 * The `/proc/modules` direction is deliberately opposite the guard's: an
 * unreadable file counts as "present" here, because this behavior is the
 * per-task backstop for a device that would otherwise kill the rooted child.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "platform/vivo/vr_task_tag.hpp"

#if defined(__ANDROID__)

#include <array>
#include <cstdio>
#include <string_view>

#include "support/log.hpp"
#include "support/native_resource.hpp"

namespace ghostlock::platform::vivo {
    namespace {
        /* vr.ko present in /proc/modules? Cached: the answer cannot change while
         * the run lasts. An unreadable /proc/modules counts as "present" - the
         * tag must be stripped if there is any chance vr.ko is enforcing. */
        bool vr_module_present() {
            static int32_t cached = -1;
            if (cached >= 0) return cached != 0;
            cached = 1;
            if (FILE *modules = fopen("/proc/modules", "r")) {
                auto close_modules = ghostlock::support::make_scope_exit(
                    [modules]() noexcept { fclose(modules); });
                std::array<char, 256> line{};
                cached = 0;
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
    } // namespace

    Status execute_vr_task_tag(AncillaryStage stage, AncillaryOps &ops) noexcept {
        /* One stage only: the rooted child exists, W2 verify has not read its
         * uid yet. */
        if (stage != AncillaryStage::PostSpawn) return true;

        if (!vr_module_present()) {
            pr_info("vr.ko not loaded; skipping per-task tag clear\n");
            return true;
        }
        if (ops.child_task == 0) {
            pr_warning("vr guard: no child task to clear; per-task tags left in "
                       "place\n");
            return false;
        }
        if (!ops.write_available || ops.write_zero == nullptr) {
            pr_warning("vr guard: no write primitive available; per-task tags left "
                       "in place (child may be killed during W2 verify)\n");
            return false;
        }

        const VrTaskTagPlan plan = plan_vr_task_tag(ops.child_task);
        pr_info("vr.ko loaded; clearing per-task tags child_task=%016zx "
                "flags=%016zx tagB=%016zx\n",
                static_cast<size_t>(ops.child_task),
                static_cast<size_t>(plan.flags_word),
                static_cast<size_t>(plan.tag_b_word));

        /* 1) Clear thread_info.flags word (covers tag A + tracepoint bit). */
        Status ok = ops.write_zero(plan.flags_word, "VR: flags+tagA");
        /* 2) Clear tag B (64-bit aligned down). Belt-and-suspenders. */
        if (ok) ok = ops.write_zero(plan.tag_b_word, "VR: tagB");

        if (ok) {
            pr_success("VR.ko per-task tags cleared\n");
        } else {
            pr_warning("VR.ko tag clear failed; child may be killed during W2 verify\n");
        }
        return ok;
    }
} // namespace ghostlock::platform::vivo

#endif
