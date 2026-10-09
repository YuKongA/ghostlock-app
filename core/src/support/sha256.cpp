/** @file support/sha256.cpp
 *  SHA-256 (FIPS 180-4), written out rather than pulled in: the tree vendors
 *  nothing it does not need, and this is small enough to read in one sitting.
 */
#include "support/sha256.hpp"

namespace ghostlock::support {
    namespace {
        /* The algorithm's round constants: the first 32 bits of the fractional
         * parts of the cube roots of the first 64 primes (FIPS 180-4 4.2.2). */
        constexpr std::uint32_t kRound[64] = {
            0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
            0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
            0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
            0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
            0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
            0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
            0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
            0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
            0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
            0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
            0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
        };

        constexpr std::uint32_t kInitial[8] = {
            0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
            0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
        };

        [[nodiscard]] constexpr std::uint32_t rotr(std::uint32_t value,
                                                   unsigned shift) noexcept {
            return (value >> shift) | (value << (32U - shift));
        }

        void compress(std::uint32_t *state, const std::uint8_t *block) noexcept {
            std::uint32_t w[64];
            for (std::size_t i = 0; i < 16U; ++i) {
                const std::size_t at = i * 4U;
                w[i] = (static_cast<std::uint32_t>(block[at]) << 24U) |
                       (static_cast<std::uint32_t>(block[at + 1U]) << 16U) |
                       (static_cast<std::uint32_t>(block[at + 2U]) << 8U) |
                       static_cast<std::uint32_t>(block[at + 3U]);
            }
            for (std::size_t i = 16U; i < 64U; ++i) {
                const std::uint32_t s0 =
                    rotr(w[i - 15U], 7U) ^ rotr(w[i - 15U], 18U) ^ (w[i - 15U] >> 3U);
                const std::uint32_t s1 =
                    rotr(w[i - 2U], 17U) ^ rotr(w[i - 2U], 19U) ^ (w[i - 2U] >> 10U);
                w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
            }
            std::uint32_t a = state[0];
            std::uint32_t b = state[1];
            std::uint32_t c = state[2];
            std::uint32_t d = state[3];
            std::uint32_t e = state[4];
            std::uint32_t f = state[5];
            std::uint32_t g = state[6];
            std::uint32_t h = state[7];
            for (std::size_t i = 0; i < 64U; ++i) {
                const std::uint32_t s1 = rotr(e, 6U) ^ rotr(e, 11U) ^ rotr(e, 25U);
                const std::uint32_t choice = (e & f) ^ (~e & g);
                const std::uint32_t temp1 = h + s1 + choice + kRound[i] + w[i];
                const std::uint32_t s0 = rotr(a, 2U) ^ rotr(a, 13U) ^ rotr(a, 22U);
                const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
                const std::uint32_t temp2 = s0 + majority;
                h = g;
                g = f;
                f = e;
                e = d + temp1;
                d = c;
                c = b;
                b = a;
                a = temp1 + temp2;
            }
            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
            state[5] += f;
            state[6] += g;
            state[7] += h;
        }
    }  // namespace

    Sha256Digest sha256(const std::uint8_t *data, std::size_t size) noexcept {
        std::uint32_t state[8];
        for (std::size_t i = 0; i < 8U; ++i) {
            state[i] = kInitial[i];
        }
        std::size_t offset = 0U;
        while (size - offset >= 64U) {
            compress(state, data + offset);
            offset += 64U;
        }
        std::uint8_t tail[128];
        for (std::size_t i = 0; i < 128U; ++i) {
            tail[i] = 0U;
        }
        const std::size_t rest = size - offset;
        for (std::size_t i = 0; i < rest; ++i) {
            tail[i] = data[offset + i];
        }
        tail[rest] = 0x80U;
        const std::size_t tail_size = rest < 56U ? 64U : 128U;
        const std::uint64_t bits = static_cast<std::uint64_t>(size) * 8U;
        for (std::size_t i = 0; i < 8U; ++i) {
            tail[tail_size - 1U - i] = static_cast<std::uint8_t>((bits >> (i * 8U)) & 0xFFU);
        }
        compress(state, tail);
        if (tail_size == 128U) {
            compress(state, tail + 64U);
        }
        Sha256Digest digest{};
        for (std::size_t i = 0; i < 8U; ++i) {
            digest.bytes[i * 4U] = static_cast<std::uint8_t>((state[i] >> 24U) & 0xFFU);
            digest.bytes[i * 4U + 1U] = static_cast<std::uint8_t>((state[i] >> 16U) & 0xFFU);
            digest.bytes[i * 4U + 2U] = static_cast<std::uint8_t>((state[i] >> 8U) & 0xFFU);
            digest.bytes[i * 4U + 3U] = static_cast<std::uint8_t>(state[i] & 0xFFU);
        }
        return digest;
    }

    void sha256_hex(const Sha256Digest &digest, char *out) noexcept {
        constexpr char kDigits[] = "0123456789abcdef";
        for (std::size_t i = 0; i < kSha256Bytes; ++i) {
            out[i * 2U] = kDigits[(digest.bytes[i] >> 4U) & 0x0FU];
            out[i * 2U + 1U] = kDigits[digest.bytes[i] & 0x0FU];
        }
        out[kSha256HexBytes - 1U] = '\0';
    }

    bool sha256_text_is_well_formed(const char *text) noexcept {
        if (text == nullptr) {
            return false;
        }
        for (std::size_t i = 0; i < kSha256HexBytes - 1U; ++i) {
            const char c = text[i];
            const bool digit = c >= '0' && c <= '9';
            const bool lower = c >= 'a' && c <= 'f';
            if (!digit && !lower) {
                return false;
            }
        }
        return text[kSha256HexBytes - 1U] == '\0';
    }

    bool sha256_matches_text(const Sha256Digest &digest, const char *text) noexcept {
        if (!sha256_text_is_well_formed(text)) {
            return false;
        }
        char hex[kSha256HexBytes];
        sha256_hex(digest, hex);
        for (std::size_t i = 0; i < kSha256HexBytes; ++i) {
            if (hex[i] != text[i]) {
                return false;
            }
        }
        return true;
    }
}  // namespace ghostlock::support
