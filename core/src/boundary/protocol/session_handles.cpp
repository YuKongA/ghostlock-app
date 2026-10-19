/** @file boundary/protocol/session_handles.cpp
 *  The handle table's three operations, and nothing else (see the header for why it exists).
 */
#include "boundary/protocol/session_handles.hpp"

namespace ghostlock::jni {
    HandleOutcome HandleTable::mint(std::uint32_t &index, std::uint32_t &generation) noexcept {
        for (std::uint32_t i = 0U; i < kMaxSessions; ++i) {
            if (!slots_[i].used) {
                slots_[i].used = true;
                if (slots_[i].generation == 0U) {
                    /* A wrapped generation would make an old handle look live again. */
                    slots_[i].generation = 1U;
                }
                index = i;
                generation = slots_[i].generation;
                return HandleOutcome::Ok;
            }
        }
        return HandleOutcome::TableFull;
    }

    HandleOutcome HandleTable::resolve(std::uint32_t index,
                                       std::uint32_t generation) const noexcept {
        if (index >= kMaxSessions) {
            return HandleOutcome::Unknown;
        }
        const Slot &slot = slots_[index];
        if (!slot.used) {
            return HandleOutcome::Unknown;
        }
        return slot.generation == generation ? HandleOutcome::Ok : HandleOutcome::Stale;
    }

    void HandleTable::retire(std::uint32_t index) noexcept {
        if (index >= kMaxSessions) {
            return;
        }
        slots_[index].used = false;
        ++slots_[index].generation;
    }

    bool HandleTable::live(std::uint32_t index) const noexcept {
        return index < kMaxSessions && slots_[index].used;
    }

    std::uint32_t HandleTable::live_count() const noexcept {
        std::uint32_t count = 0U;
        for (std::uint32_t i = 0U; i < kMaxSessions; ++i) {
            if (slots_[i].used) {
                ++count;
            }
        }
        return count;
    }
}  // namespace ghostlock::jni
