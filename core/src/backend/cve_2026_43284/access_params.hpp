#pragma once
/** @file backend/cve_2026_43284/access_params.hpp
 *  What this backend's kernel access needs besides an address and a value: credentials, and where
 *  to write.
 *
 *  Same idea as the other backend's parameter set and a different content, which is the point of
 *  having one: the base cannot know what a particular backend needs, so each says it. This one
 *  needs the key material, because its write is an ESP datagram rather than a word through a race
 *  window, and the key material is a runtime credential -- the header of the primitive says it
 *  arrives through the runtime channel and not from the profile, so the access receives it here
 *  instead of reading it. It also needs an offset, since the write lands at a position in the file
 *  rather than at an address.
 *
 *  No route field: this backend's route axis is empty. What names it is the backend id, and what
 *  varies per call is the credential material and the position.
 */
#include <cstddef>
#include <cstdint>

#include "contract/kernel_access.hpp"

namespace ghostlock::backend::cve_2026_43284 {
    /** The session's secrets, as the access receives them. A plain struct rather than a pointer,
     *  so the access owns what it was given and can zero it on the way out; the fields mirror the
     *  old tree's parameter type, whose header names these as runtime credentials. */
    struct SaMaterial final {
        std::uint32_t spi = 0U;
        /** The encapsulated port the socket connects from. The old tree's runtime frame carries it,
         *  and the socket cannot be made without it, so it belongs to the credentials rather than
         *  being invented at the door. */
        std::uint16_t encap_port = 0U;
        /** The sender port, the frame's other half of the same fact. */
        std::uint16_t sender_port = 0U;
        /** The integrity length in bytes: the write truncates the ICV to it, so a value of zero is
         *  not a default to guess but something the caller must state. */
        std::uint8_t icv_len = 0U;
        /** The AES key; the write derives its IV from it and the old block's content. */
        std::uint8_t aes_key[32] = {};
        /** The integrity key for the ESP trailer. */
        std::uint8_t hmac_key[32] = {};
        /** Whether a caller has supplied anything at all. All-zero is not a credential, it is an
         *  unset one, and the access refuses to write with it rather than using it. */
        bool set = false;
    };

    class AccessParams final : public contract::KernelAccess::AccessMiscParameters {
    public:
        /** The credentials this session was given. */
        SaMaterial sa{};
        /** Where in the carrier to write. Zero is a legitimate position, so it is not a sentinel. */
        std::uint64_t offset = 0U;
        /** Whether a position was given at all, since zero cannot double as one. */
        bool offset_set = false;

        [[nodiscard]] std::string_view name() const noexcept override { return "cve_2026_43284"; }

        /** Whether this set is complete enough to bring the route up: it needs credentials. */
        [[nodiscard]] constexpr bool usable_for_initialize() const noexcept { return sa.set; }
        /** Whether it is complete enough to write: it needs a position as well. */
        [[nodiscard]] constexpr bool usable_for_access() const noexcept {
            return usable_for_initialize() && offset_set;
        }
    };
}  // namespace ghostlock::backend::cve_2026_43284
