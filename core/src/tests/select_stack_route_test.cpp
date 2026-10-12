/** @file tests/select_stack_route_test.cpp
 *  `select_stack` route 的**纯函数**部分：词表 → fd_set 词位 的映射。
 *
 *  本条 route 与 multicast 的机制差异只在"载荷载体"（pselect 的 in/out/ex 三组 fd_set ✓），
 *  所以可先在主机上把"哪个 waiter 词落在哪一组哪一个词"钉死 ✓，再谈接线。
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/select.h>

#include "backend/cve_2026_43499/seam/select_stack_route.hpp"

namespace {
    int failures = 0;
    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++failures;
        }
    }
}  // namespace

int main() {
    using ghostlock::backend::cve_2026_43499::seam::SelectStackConfig;
    using ghostlock::backend::cve_2026_43499::seam::SelectStackWordSite;
    using ghostlock::backend::cve_2026_43499::seam::SelectStackWord;
    using ghostlock::backend::cve_2026_43499::seam::SelectStackOffsets;
    using ghostlock::backend::cve_2026_43499::seam::fill_select_stack_words;
    using ghostlock::backend::cve_2026_43499::seam::select_stack_build_fdsets;
    using ghostlock::backend::cve_2026_43499::seam::SelectStackFdPlan;
    using ghostlock::backend::cve_2026_43499::seam::select_stack_plan_fds;
    using ghostlock::backend::cve_2026_43499::seam::select_stack_compact_words;
    using ghostlock::backend::cve_2026_43499::seam::select_stack_word_site;

    SelectStackConfig config{};
    config.fd_count = 320U;   /* 320 个 fd ⇒ 每组 5 个 64-bit 词 ✓（不是 320 个词 ✗） */
    config.waiter_shift = -2;
    config.compact = true;
    config.fake_waiter_prio = 0x2aU;
    config.fake_task = 0x1000U;
    config.fake_lock = 0x2000U;
    config.target = 0xffffff802adaad88ULL;
    config.value = 0xffff8000000b0100ULL;
    /* 词号全部来自文档 ✓ —— 这里用**本机几何**推出的那一套（task 0x30/8=6 ✓、lock 0x38/8=7 ✓），
     *  正说明"词号必须是数据" ✓：快照 6.1 的那套是 8/9 ✗。 */
    config.word_tree_pc = 2;
    config.word_tree_right = 3;
    config.word_tree_left = 4;
    config.word_pi_pc = 5;
    config.word_pi_right = 6;
    config.word_pi_left = 7;
    config.word_task = 6;
    config.word_lock = 7;
    config.word_wake_prio = 10;
    config.word_deadline = 11;
    config.word_ww_ctx = 12;

    /* waiter 词 2..12（compact ✓）⇒ 组 0、词 0..10（shift = -2 ✓）。 */
    const SelectStackWordSite first = select_stack_word_site(2, config);
    expect(first.set_index == 0 && first.word_index == 0, "waiter word 2 lands at group 0 word 0");
    /* 每组只有 5 个词 ✓（320 fd / 64 ✓）⇒ waiter 词会**跨组** ✓：词 12 ⇒ 全局 10 ⇒ 组 2、词 0 ✓。 */
    const SelectStackWordSite last = select_stack_word_site(12, config);
    expect(last.set_index == 2 && last.word_index == 0, "waiter word 12 lands at group 2 word 0");

    /* 负位移的词被忽略（shift 之前的词 ✓）。 */
    const SelectStackWordSite below = select_stack_word_site(0, config);
    expect(below.set_index < 0, "a word before the waiter start is ignored");

    /* 越界（组 > 2）被拒绝 ✓。 */
    const SelectStackWordSite over = select_stack_word_site(2000, config);
    expect(over.set_index < 0, "a word past the third set is refused");

    /* 非 compact ⇒ 本文件不表（词表两套 ✓，6.6 那套另行 ✓）。 */
    SelectStackConfig other = config;
    other.compact = false;
    const SelectStackWordSite other_site = select_stack_word_site(2, other);
    expect(other_site.set_index < 0, "the non-compact word table is not served here");

    /* 词值：与快照 414-430 同序 ✓（tree/pi 双侧 + task/lock/prio ✓）。 */
    std::array<SelectStackWord, 11U> words{};
    (void) select_stack_compact_words(config, words);
    expect(words[0].word == 2 && words[0].value == config.value, "tree_pc carries the page-derived value");
    expect(words[2].word == 4 && words[2].value == config.target, "tree_left carries the target");
    expect(words[6].word == config.word_task && words[6].value == config.fake_task, "task word comes from the data");
    expect(words[7].word == config.word_lock && words[7].value == config.fake_lock, "lock word comes from the data");
    expect(words[8].value == ((static_cast<std::uint64_t>(0x2aU) << 32U) | 3U), "wake_prio is prio<<32 | 3");

    /* 偏移 → 词号（round 171 ✓）：本机几何（5.15 ✓）应给 pc=3 / task=6 / lock=7 ✓。 */
    SelectStackOffsets offsets{};
    offsets.head_word = 0x00U;
    offsets.head_next = 0x08U;
    offsets.head_prev = 0x10U;
    offsets.pc = 0x18U;
    offsets.right = 0x20U;
    offsets.left = 0x28U;
    offsets.task = 0x30U;
    offsets.lock = 0x38U;
    offsets.wake_state = 0x40U;
    offsets.prio = 0x44U;
    offsets.deadline = 0x48U;
    offsets.ww_ctx = 0x50U;
    SelectStackConfig filled{};
    expect(fill_select_stack_words(offsets, filled) == nullptr, "device offsets all land on words");
    expect(filled.word_pi_pc == 3 && filled.word_tree_pc == 3, "pc is word 3 on this device");
    expect(filled.word_task == 6, "task is word 6 here (the 6.1 snapshot said 8)");
    expect(filled.word_lock == 7, "lock is word 7 here (the 6.1 snapshot said 9)");
    expect(filled.word_deadline == 9 && filled.word_ww_ctx == 10, "deadline and ww_ctx follow");
    SelectStackOffsets ragged = offsets;
    ragged.task = 0x31U;
    expect(fill_select_stack_words(ragged, filled) != nullptr, "a non-word offset is refused");

    /* fd_set 组装（round 173 ✓）：词号 3 的值应出现在 in_set 的第 3 个词 ✓。 */
    fd_set in_set{};
    fd_set out_set{};
    fd_set ex_set{};
    SelectStackConfig shaped = config;
    shaped.word_tree_pc = 0;
    shaped.word_tree_left = 1;
    shaped.word_task = 3;    /* 词 3 ⇒ in_set 的第 3 个 64-bit 词 ✓ */
    shaped.word_lock = 4;
    shaped.word_pi_pc = 5;
    shaped.word_pi_right = 6;
    shaped.word_pi_left = 7;
    shaped.word_wake_prio = 8;
    shaped.word_deadline = 9;
    shaped.word_ww_ctx = 10;
    expect(select_stack_build_fdsets(shaped, in_set, out_set, ex_set) == nullptr,
           "the three fd_sets are built");
    /* 注意 `waiter_shift = -2` ✓：waiter 词 w ⇒ 词位 w - 2 ✓（这正是 shift 的意义 ✓）。 */
    std::uint64_t seen = 0U;
    std::memcpy(&seen, reinterpret_cast<const std::uint8_t *>(&in_set) + 1U * sizeof(std::uint64_t),
                sizeof(seen));
    expect(seen == shaped.fake_task, "waiter word 3 lands at in_set word 1 (shift -2)");
    std::uint64_t seen_lock = 0U;
    std::memcpy(&seen_lock,
                reinterpret_cast<const std::uint8_t *>(&in_set) + 2U * sizeof(std::uint64_t),
                sizeof(seen_lock));
    expect(seen_lock == shaped.fake_lock, "waiter word 4 lands at in_set word 2 (shift -2)");
    /* 越界：词位远超集合容量 ⇒ 具名拒绝 ✓。 */
    SelectStackConfig too_far = shaped;
    too_far.word_task = 100000;
    too_far.word_lock = 100001;
    too_far.word_pi_pc = 100002;
    too_far.word_pi_right = 100003;
    too_far.word_pi_left = 100004;
    too_far.word_wake_prio = 100005;
    too_far.word_deadline = 100006;
    too_far.word_ww_ctx = 100007;
    too_far.word_tree_pc = 100008;
    too_far.word_tree_left = 100009;
    /* 超出三组覆盖范围的词，快照的行为是**跳过** ✓（不是拒绝 ✗）⇒ 这里钉住"跳过" ✓。 */
    in_set = fd_set{};
    expect(select_stack_build_fdsets(too_far, in_set, out_set, ex_set) == nullptr,
           "words past the third set are skipped, not a hard failure");
    std::uint64_t empty_word = 1U;
    std::memcpy(&empty_word, reinterpret_cast<const std::uint8_t *>(&in_set), sizeof(empty_word));
    expect(empty_word == 0U, "nothing was written for the skipped words");

    /* 借用账本（round 174 ✓）：乱序 + 重复 + 含安装位本身 ⇒ 期望是"升序去重的低编号 fd" ✓。 */
    const std::int32_t selected[] = {5, 1, 3, 1, 7, 3};
    SelectStackFdPlan plan{};
    expect(select_stack_plan_fds(selected, 6U, 8, plan) == nullptr, "the fd plan is built");
    /* 7 == nfds - 1 是**安装位**✓ ⇒ 不属于"低编号借用"✓ ⇒ distinct 低编号只有 1/3/5 三个 ✓。 */
    expect(plan.low_count == 3U, "duplicates collapse and the install slot is excluded");
    expect(plan.low_fds[0] == 1 && plan.low_fds[1] == 3 && plan.low_fds[2] == 5,
           "the borrow list is ascending and holds only real low fds");
    expect(plan.installed_at == 7, "the read end is installed at nfds - 1");
    /* 安装位本身（fd 7 = nfds - 1 ✓）不出现在借用清单里 ✓。 */
    bool has_install_slot = false;
    for (std::size_t i = 0U; i < plan.low_count; ++i) {
        if (plan.low_fds[i] == plan.installed_at) {
            has_install_slot = true;
        }
    }
    expect(!has_install_slot, "the install slot itself is not borrowed");
    /* nfds 太小 ⇒ 具名拒绝 ✓。 */
    SelectStackFdPlan tiny{};
    expect(select_stack_plan_fds(selected, 6U, 1, tiny) != nullptr, "nfds too small is refused by name");

    if (failures == 0) {
        std::printf("select-stack route: word table and fd_set sites hold\n");
        return 0;
    }
    std::printf("select-stack route: %d failure(s)\n", failures);
    return 1;
}
