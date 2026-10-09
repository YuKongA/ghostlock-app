/** @file memory/multicast_arm.cpp
 *  The geometry checks and the two stores they guard.
 */
#include "memory/multicast_arm.hpp"

namespace ghostlock::memory {
    namespace {
        /** A field fits when its first byte and its last byte are inside. */
        bool fits(std::size_t size, std::size_t offset, std::size_t width) noexcept {
            return offset <= size && width <= size - offset;
        }

        void put_word(std::uint8_t *buffer, std::size_t offset, std::uint64_t value,
                      std::size_t width) noexcept {
            for (std::size_t i = 0U; i < width; ++i) {
                buffer[offset + i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
            }
        }
    }  // namespace

    std::size_t required_bytes(const MulticastGeometry &geometry,

                               std::size_t word_bytes) noexcept {
        const std::size_t task = geometry.waiter_offset + geometry.task_offset;
        const std::size_t lock = geometry.waiter_offset + geometry.lock_offset;
        const std::size_t last = task > lock ? task : lock;
        return last + word_bytes;
    }

    ArmVerdict check_geometry(const MulticastGeometry &geometry, std::size_t buffer_size,

                              std::size_t word_bytes) noexcept {
        if (word_bytes == 0U || word_bytes > sizeof(std::uint64_t)) {
            return ArmVerdict::FieldsOverlap; /* a width that cannot be a field */
        }
        const std::size_t task = geometry.waiter_offset + geometry.task_offset;
        const std::size_t lock = geometry.waiter_offset + geometry.lock_offset;
        if (!fits(buffer_size, task, word_bytes) || !fits(buffer_size, lock, word_bytes)) {
            return ArmVerdict::OutOfBuffer;
        }
        /* Two distinct fields of one structure cannot share a word. */
        const std::size_t gap = task > lock ? task - lock : lock - task;
        if (gap < word_bytes) {
            return ArmVerdict::FieldsOverlap;
        }
        return ArmVerdict::Ok;
    }

    ArmVerdict encode_multicast_arm(std::uint8_t *buffer, std::size_t buffer_size,

                                    const MulticastGeometry &geometry, std::uint64_t fake_task,
                                    std::uint64_t fake_lock, std::size_t word_bytes) noexcept {
        if (buffer == nullptr || buffer_size == 0U) {
            return ArmVerdict::NoBuffer;
        }
        const ArmVerdict verdict = check_geometry(geometry, buffer_size, word_bytes);
        if (verdict != ArmVerdict::Ok) {
            return verdict;
        }
        put_word(buffer, geometry.waiter_offset + geometry.task_offset, fake_task, word_bytes);
        put_word(buffer, geometry.waiter_offset + geometry.lock_offset, fake_lock, word_bytes);
        return ArmVerdict::Ok;
    }
}  // namespace ghostlock::memory
