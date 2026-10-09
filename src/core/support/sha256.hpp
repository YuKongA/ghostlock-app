#pragma once
/** @file support/sha256.hpp
 *  SHA-256: the one digest the payload path checks a document against.
 *
 *  Neutral by construction -- no allocation, no I/O, no state outside the
 *  arguments -- so it belongs to support and may be used from any layer.
 *
 *  The round constants below are the algorithm's own; they are not tuning and
 *  have no profile key, which is why they live here rather than in the data
 *  plane.
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::support {
    inline constexpr std::size_t kSha256Bytes = 32U;
    /** Lowercase hex plus a terminator. */
    inline constexpr std::size_t kSha256HexBytes = 65U;

    struct Sha256Digest final {
        std::uint8_t bytes[kSha256Bytes];
    };

    /** One-shot digest over a buffer; an empty buffer is well defined. */
    [[nodiscard]] Sha256Digest sha256(const std::uint8_t *data, std::size_t size) noexcept;

    /** Lowercase hex of a digest into out, which must hold kSha256HexBytes. */
    void sha256_hex(const Sha256Digest &digest, char *out) noexcept;

    /** True when a [0-9a-f] string of exactly 64 characters is a digest text. */
    [[nodiscard]] bool sha256_text_is_well_formed(const char *text) noexcept;

    /** Compare a digest against 64 lowercase hex characters. */
    [[nodiscard]] bool sha256_matches_text(const Sha256Digest &digest, const char *text) noexcept;
}  // namespace ghostlock::support
