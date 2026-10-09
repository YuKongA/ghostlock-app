#pragma once
/** @file script/handle_table.hpp
 *  A6-3: the typed handle table (ADR-0010, A6 baseline item 6).
 *
 *  A handle is {kind, slot, generation}: plain data, no destructor, no finaliser,
 *  and it is only ever produced here, by the host -- the script receives one when
 *  it acquires a backend and can do nothing but hand it back. What the table
 *  enforces is exactly what a script can get wrong:
 *    - acquiring twice while one handle is live: handle-conflict;
 *    - using a handle after release: handle-stale, because release bumps the
 *      generation, so the old value can never match again;
 *    - a slot outside the table: handle-stale as well (it cannot be a live one).
 *
 *  Bounded at kMaxHandles: a plan cannot grow the table, and no allocation
 *  happens on any path here.
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::script {
    /** One handle, as the script sees it. Trivially copyable, no finaliser. */
    struct Handle final {
        std::uint32_t kind = 0U;
        std::uint32_t slot = 0U;
        std::uint32_t generation = 0U;
    };

    /** One plan holds at most this many live handles (baseline item 6). */
    inline constexpr std::size_t kMaxHandles = 64U;

    /** Why a handle was refused. Names match the single rejection table. */
    enum class HandleRefusal : unsigned char {
        None = 0,
        Conflict = 1,
        Stale = 2,
        /** The table is full: the bound is the same named refusal a container
         *  bound uses, so no new vocabulary is invented for it. */
        Full = 3,
    };

    [[nodiscard]] constexpr const char *handle_refusal_name(HandleRefusal refusal) noexcept {
        switch (refusal) {
            case HandleRefusal::None: return "ok";
            case HandleRefusal::Conflict: return "handle-conflict";
            case HandleRefusal::Stale: return "handle-stale";
            case HandleRefusal::Full: return "budget-exceeded";
        }
        return "unknown";
    }

    class HandleTable final {
      public:
        /** Take the next handle of this kind. Refuses while one is live: the
         *  same backend may not hold two, and the table serves one backend. */
        [[nodiscard]] HandleRefusal acquire(std::uint32_t kind, Handle &out) noexcept {
            for (std::size_t i = 0U; i < kMaxHandles; ++i) {
                if (slots_[i].live && slots_[i].kind == kind) {
                    return HandleRefusal::Conflict;
                }
            }
            for (std::size_t i = 0U; i < kMaxHandles; ++i) {
                if (!slots_[i].live) {
                    slots_[i].live = true;
                    slots_[i].kind = kind;
                    if (slots_[i].generation == 0U) {
                        slots_[i].generation = 1U;
                    }
                    out.kind = kind;
                    out.slot = static_cast<std::uint32_t>(i);
                    out.generation = slots_[i].generation;
                    ++live_;
                    return HandleRefusal::None;
                }
            }
            return HandleRefusal::Full; /* the table is full: no new handle */
        }

        /** Give a handle back. The generation moves on, so the value the script
         *  still holds can never be accepted again. */
        [[nodiscard]] HandleRefusal release(const Handle &handle) noexcept {
            const HandleRefusal verdict = check(handle);
            if (verdict != HandleRefusal::None) {
                return verdict;
            }
            Slot &slot = slots_[handle.slot];
            slot.live = false;
            ++slot.generation;
            if (slot.generation == 0U) {
                slot.generation = 1U; /* keep zero as "never issued" */
            }
            --live_;
            return HandleRefusal::None;
        }

        [[nodiscard]] HandleRefusal check(const Handle &handle) const noexcept {
            if (handle.slot >= kMaxHandles) {
                return HandleRefusal::Stale;
            }
            const Slot &slot = slots_[handle.slot];
            if (!slot.live || slot.kind != handle.kind ||
                slot.generation != handle.generation) {
                return HandleRefusal::Stale;
            }
            return HandleRefusal::None;
        }

        [[nodiscard]] std::size_t live_count() const noexcept { return live_; }

        [[nodiscard]] std::uint32_t generation_of(std::uint32_t slot) const noexcept {
            return slot < kMaxHandles ? slots_[slot].generation : 0U;
        }

      private:
        struct Slot final {
            std::uint32_t kind = 0U;
            std::uint32_t generation = 0U;
            bool live = false;
        };
        Slot slots_[kMaxHandles];
        std::size_t live_ = 0U;
    };
}  // namespace ghostlock::script
