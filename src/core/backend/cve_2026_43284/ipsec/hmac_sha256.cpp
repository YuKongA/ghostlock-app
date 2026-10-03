/* SHA-256 / HMAC-SHA256 for the CVE-2026-43284 ESP ICV.
 *
 * Independent rewrite with attribution; the SHA-256/HMAC core is Odzhan's
 * BSD-3-Clause implementation (full notice in hmac_sha256.hpp). Key pads,
 * intermediate hashes and the working context are wiped before return via
 * ipsec::zeroize_bytes. No syscalls, no global state. */

#include "backend/cve_2026_43284/ipsec/hmac_sha256.hpp"
#include "backend/cve_2026_43284/ipsec/ipsec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43284 {
    namespace {

        constexpr std::size_t kBlockBytes = 64;
        constexpr std::size_t kDigestBytes = 32;

        constexpr std::array<std::uint32_t, 64> kRoundConstants = {
        0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
        0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
        0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
        0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
        0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
        0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
        0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
        0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
        };

        constexpr std::array<std::uint32_t, 8> kInitialState = {
            0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
            0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

        struct Sha256 {
            std::array<std::uint32_t, 8> state{};
            std::array<std::uint8_t, kBlockBytes> buffer{};
            std::uint64_t total_bytes = 0U;
            std::size_t buffer_len = 0U;
        };

        std::uint32_t rotr(std::uint32_t x, unsigned n) noexcept {
            return (x >> n) | (x << (32U - n));
        }

        void transform(Sha256 &ctx, const std::uint8_t block[kBlockBytes]) noexcept {
            std::array<std::uint32_t, 64> w{};
            for (std::size_t i = 0U; i < 16U; ++i) {
                w[i] = (static_cast<std::uint32_t>(block[4U * i]) << 24U) |
                       (static_cast<std::uint32_t>(block[4U * i + 1U]) << 16U) |
                       (static_cast<std::uint32_t>(block[4U * i + 2U]) << 8U) |
                       static_cast<std::uint32_t>(block[4U * i + 3U]);
            }
            for (std::size_t i = 16U; i < 64U; ++i) {
                const std::uint32_t s0 =
                        rotr(w[i - 15U], 7U) ^ rotr(w[i - 15U], 18U) ^ (w[i - 15U] >> 3U);
                const std::uint32_t s1 =
                        rotr(w[i - 2U], 17U) ^ rotr(w[i - 2U], 19U) ^ (w[i - 2U] >> 10U);
                w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
            }

            std::uint32_t a = ctx.state[0];
            std::uint32_t b = ctx.state[1];
            std::uint32_t c = ctx.state[2];
            std::uint32_t d = ctx.state[3];
            std::uint32_t e = ctx.state[4];
            std::uint32_t f = ctx.state[5];
            std::uint32_t g = ctx.state[6];
            std::uint32_t h = ctx.state[7];
            for (std::size_t i = 0U; i < 64U; ++i) {
                const std::uint32_t s1 = rotr(e, 6U) ^ rotr(e, 11U) ^ rotr(e, 25U);
                const std::uint32_t ch = (e & f) ^ ((~e) & g);
                const std::uint32_t t1 = h + s1 + ch + kRoundConstants[i] + w[i];
                const std::uint32_t s0 = rotr(a, 2U) ^ rotr(a, 13U) ^ rotr(a, 22U);
                const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                const std::uint32_t t2 = s0 + maj;
                h = g;
                g = f;
                f = e;
                e = d + t1;
                d = c;
                c = b;
                b = a;
                a = t1 + t2;
            }

            ctx.state[0] += a;
            ctx.state[1] += b;
            ctx.state[2] += c;
            ctx.state[3] += d;
            ctx.state[4] += e;
            ctx.state[5] += f;
            ctx.state[6] += g;
            ctx.state[7] += h;
            zeroize_bytes(w.data(), sizeof(w));
        }

        void sha256_init(Sha256 &ctx) noexcept {
            ctx.state = kInitialState;
            ctx.buffer.fill(0U);
            ctx.total_bytes = 0U;
            ctx.buffer_len = 0U;
        }

        void sha256_update(Sha256 &ctx, const std::uint8_t *data, std::size_t len) noexcept {
            if (data == nullptr) return;
            ctx.total_bytes += static_cast<std::uint64_t>(len);
            for (std::size_t i = 0U; i < len; ++i) {
                ctx.buffer[ctx.buffer_len] = data[i];
                ++ctx.buffer_len;
                if (ctx.buffer_len == kBlockBytes) {
                    transform(ctx, ctx.buffer.data());
                    ctx.buffer_len = 0U;
                }
            }
        }

        void sha256_final(Sha256 &ctx, std::uint8_t out[kDigestBytes]) noexcept {
            const std::uint64_t bits = ctx.total_bytes * 8U;
            std::size_t i = ctx.buffer_len;
            ctx.buffer[i] = 0x80U;
            ++i;
            if (i > 56U) {
                while (i < kBlockBytes) {
                    ctx.buffer[i] = 0U;
                    ++i;
                }
                transform(ctx, ctx.buffer.data());
                i = 0U;
            }
            while (i < 56U) {
                ctx.buffer[i] = 0U;
                ++i;
            }
            for (std::size_t j = 0U; j < 8U; ++j) {
                ctx.buffer[56U + j] =
                        static_cast<std::uint8_t>((bits >> (56U - 8U * j)) & 0xffU);
            }
            transform(ctx, ctx.buffer.data());
            for (std::size_t j = 0U; j < 8U; ++j) {
                out[4U * j] = static_cast<std::uint8_t>((ctx.state[j] >> 24U) & 0xffU);
                out[4U * j + 1U] = static_cast<std::uint8_t>((ctx.state[j] >> 16U) & 0xffU);
                out[4U * j + 2U] = static_cast<std::uint8_t>((ctx.state[j] >> 8U) & 0xffU);
                out[4U * j + 3U] = static_cast<std::uint8_t>(ctx.state[j] & 0xffU);
            }
        }

    } // namespace

    void hmac_sha256(const std::uint8_t *key, std::size_t key_len,
                     const std::uint8_t *msg, std::size_t msg_len,
                     std::uint8_t out[32]) noexcept {
        if (out == nullptr) return;
        if (key == nullptr) key_len = 0U;
        if (msg == nullptr) msg_len = 0U;

        std::array<std::uint8_t, kBlockBytes> ipad{};
        std::array<std::uint8_t, kBlockBytes> opad{};
        std::array<std::uint8_t, kDigestBytes> key_hash{};
        const std::uint8_t *k = key;
        std::size_t k_len = key_len;
        if (k_len > kBlockBytes) {
            Sha256 hash{};
            sha256_init(hash);
            sha256_update(hash, key, key_len);
            sha256_final(hash, key_hash.data());
            zeroize_bytes(&hash, sizeof(hash));
            k = key_hash.data();
            k_len = kDigestBytes;
        }

        ipad.fill(0x36U);
        opad.fill(0x5cU);
        for (std::size_t i = 0U; i < k_len; ++i) {
            ipad[i] = static_cast<std::uint8_t>(ipad[i] ^ k[i]);
            opad[i] = static_cast<std::uint8_t>(opad[i] ^ k[i]);
        }

        std::array<std::uint8_t, kDigestBytes> inner{};
        Sha256 ctx{};
        sha256_init(ctx);
        sha256_update(ctx, ipad.data(), ipad.size());
        sha256_update(ctx, msg, msg_len);
        sha256_final(ctx, inner.data());
        sha256_init(ctx);
        sha256_update(ctx, opad.data(), opad.size());
        sha256_update(ctx, inner.data(), inner.size());
        sha256_final(ctx, out);

        zeroize_bytes(ipad.data(), sizeof(ipad));
        zeroize_bytes(opad.data(), sizeof(opad));
        zeroize_bytes(key_hash.data(), sizeof(key_hash));
        zeroize_bytes(inner.data(), sizeof(inner));
        zeroize_bytes(&ctx, sizeof(ctx));
    }

} // namespace ghostlock::backend::cve_2026_43284
