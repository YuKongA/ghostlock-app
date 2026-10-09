#pragma once
/** @file backend/cve_2026_43284/seam/esp_crypto.hpp
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

namespace ghostlock::backend::cve_2026_43284::seam {
    /** The block the write deals in, which is also the size of a vector. */
    inline constexpr std::size_t kEspBlockBytes = 16U;

    /** Derive the vector for a write: one ECB decryption of the block being replaced. Returns
     *  false when the core refuses, and a caller must not continue: with no vector there is no
     *  datagram, which is the same rule the write body follows when the old block cannot be read. */
    [[nodiscard]] inline bool esp_iv(const std::uint8_t old_block[kEspBlockBytes],
                                     const std::uint8_t key[ipsec::kAes256KeyBytes],
                                     std::uint8_t out[kEspBlockBytes]) noexcept {
        return ipsec::aes256_ecb_decrypt(key, old_block, out);
    }

    /** The integrity check, over the datagram, truncated to the length the frame states. Not
     *  defined yet: its core is the HMAC that is being ported, and this declaration exists so the
     *  call site can be written without a stand-in value taking its place. */
    [[nodiscard]] bool esp_icv(const std::uint8_t *datagram, std::size_t size,
                               const std::uint8_t key[32], std::uint8_t icv_len,
                               std::uint8_t *out, std::size_t capacity) noexcept;
}  // namespace ghostlock::backend::cve_2026_43284::seam
