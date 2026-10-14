#pragma once
/** @file jni/session_handles.hpp
 *  Session handles: an index, and the generation that index was minted with.
 *
 *  Why not a bare pointer: a pointer is handed to Kotlin as a jlong, Kotlin keeps it for as long
 *  as it likes, and a session that was destroyed in the meantime would be used again -- by a stale
 *  copy, or by a second call that raced the first. A generation that is bumped on every retire
 *  turns that reuse into a named refusal ("stale-target") instead of a wild dereference.
 *
 *  Why not a heap-allocated registry: the table is 64 slots of eight bytes; a fixed table has no
 *  allocation, no failure mode of its own, and no lifetime question about who frees it.
 *
 *  Encoding: high 32 bits carry the generation, low 32 bits the index plus one, so a zero jlong is
 *  never a valid handle (Kotlin treats zero as "no session", and this makes that true by
 *  construction).
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::jni {
    /** How many sessions one process may hold at once. */
    inline constexpr std::uint32_t kMaxSessions = 64U;

    /** What minting or resolving a handle answered. */
    enum class HandleOutcome : std::uint8_t {
        Ok = 0,
        Unknown = 1,   /**< the index was never used, or was retired and never reused since. */
        Stale = 2,     /**< the index is live, but not at the generation the caller holds. */
        TableFull = 3, /**< mint: no free slot. */
    };

    [[nodiscard]] constexpr const char *handle_outcome_name(HandleOutcome outcome) noexcept {
        switch (outcome) {
            case HandleOutcome::Ok: return "ok";
            case HandleOutcome::Unknown: return "unknown-handle";
            case HandleOutcome::Stale: return "stale-handle";
            case HandleOutcome::TableFull: return "session-table-full";
        }
        return "unknown";
    }

    /** Build the jlong a caller keeps. */
    [[nodiscard]] constexpr std::uint64_t encode_handle(std::uint32_t index,
                                                        std::uint32_t generation) noexcept {
        return (static_cast<std::uint64_t>(generation) << 32U) |
               (static_cast<std::uint64_t>(index) + 1U);
    }

    /** Take a jlong apart. @return false when it cannot be one of ours (zero, or a zero index). */
    [[nodiscard]] constexpr bool decode_handle(std::uint64_t raw, std::uint32_t &index,
                                               std::uint32_t &generation) noexcept {
        if (raw == 0U) {
            return false;
        }
        const std::uint32_t low = static_cast<std::uint32_t>(raw & 0xffffffffULL);
        if (low == 0U) {
            return false;
        }
        index = low - 1U;
        generation = static_cast<std::uint32_t>(raw >> 32U);
        return index < kMaxSessions;
    }

    /** The table: mint a slot, resolve a handle, retire a slot. */
    class HandleTable final {
      public:
        /** Take the next free slot. @return Ok and fills index/generation, or TableFull. */
        [[nodiscard]] HandleOutcome mint(std::uint32_t &index, std::uint32_t &generation) noexcept;
        /** Check a handle. A retired slot answers Stale (never Ok). */
        [[nodiscard]] HandleOutcome resolve(std::uint32_t index,
                                            std::uint32_t generation) const noexcept;
        /** Give a slot back and bump its generation, so old handles stay refused. */
        void retire(std::uint32_t index) noexcept;
        [[nodiscard]] bool live(std::uint32_t index) const noexcept;
        [[nodiscard]] std::uint32_t live_count() const noexcept;

      private:
        struct Slot final {
            bool used = false;
            std::uint32_t generation = 1U;
        };
        Slot slots_[kMaxSessions]{};
    };
}  // namespace ghostlock::jni
