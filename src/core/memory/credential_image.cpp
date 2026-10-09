/** @file memory/credential_image.cpp
 *  The credential copy: validated first, written at once.
 */
#include "memory/credential_image.hpp"

#include <cstring>

namespace ghostlock::memory {
    namespace {
        bool fits(std::size_t size, std::size_t offset, std::size_t width) noexcept {
            return offset <= size && width <= size - offset;
        }

        void put32(std::uint8_t *bytes, std::size_t offset, std::uint32_t value) noexcept {
            for (std::size_t i = 0U; i < 4U; ++i) {
                bytes[offset + i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
            }
        }

        void put64(std::uint8_t *bytes, std::size_t offset, std::uint64_t value) noexcept {
            for (std::size_t i = 0U; i < 8U; ++i) {
                bytes[offset + i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
            }
        }
    }  // namespace

    CredentialVerdict build_credential_image(std::uint8_t *buffer, std::size_t capacity,
                                            const CredentialLayout &layout,

                                            std::size_t offset) noexcept {
        if (buffer == nullptr || layout.copy_size == 0U) {
            return CredentialVerdict::LayoutInvalid;
        }
        if (!fits(capacity, offset, layout.copy_size)) {
            return CredentialVerdict::NoRoom;
        }
        if (layout.ref_count > kCredentialRefSlots) {
            return CredentialVerdict::TooManyReferences;
        }
        /* Every field has to sit inside the copy the profile declared. */
        if (!fits(layout.copy_size, layout.usage_offset, sizeof(std::uint32_t)) ||
            !fits(layout.copy_size, layout.caps_offset, sizeof(std::uint64_t)) ||
            static_cast<std::size_t>(layout.caps_count) * sizeof(std::uint64_t) >
                layout.copy_size - layout.caps_offset) {
            return CredentialVerdict::LayoutInvalid;
        }
        for (std::uint32_t r = 0U; r < layout.ref_count; ++r) {
            if (!fits(layout.copy_size, layout.ref_offset[r], sizeof(std::uint64_t))) {
                return CredentialVerdict::LayoutInvalid;
            }
        }
        std::uint8_t *copy = buffer + offset;
        std::memset(copy, 0, layout.copy_size);
        put32(copy, layout.usage_offset, layout.usage_value);
        for (std::uint32_t i = 0U; i < layout.caps_count; ++i) {
            put64(copy, layout.caps_offset + static_cast<std::size_t>(i) * sizeof(std::uint64_t),
                  layout.caps_value);
        }
        for (std::uint32_t r = 0U; r < layout.ref_count; ++r) {
            put64(copy, layout.ref_offset[r], layout.ref_image[r]);
        }
        return CredentialVerdict::Ok;
    }
}  // namespace ghostlock::memory
