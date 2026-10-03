/* AES-256 (FIPS 197) ECB and CBC for the CVE-2026-43284 ESP primitive.
 *
 * Independent rewrite with attribution; the upstream AES core is Odzhan's
 * BSD-3-Clause implementation (full notice in aes256.hpp). Key schedules and
 * block scratch are key-derived and wiped before return via
 * ipsec::zeroize_bytes. No syscalls, no global state. */

#include "backend/cve_2026_43284/ipsec/aes256.hpp"
#include "backend/cve_2026_43284/ipsec/ipsec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43284 {
    namespace {

        constexpr std::array<std::uint8_t, 256> kSbox = {
        0x63U,0x7cU,0x77U,0x7bU,0xf2U,0x6bU,0x6fU,0xc5U,0x30U,0x01U,0x67U,0x2bU,0xfeU,0xd7U,0xabU,0x76U,
        0xcaU,0x82U,0xc9U,0x7dU,0xfaU,0x59U,0x47U,0xf0U,0xadU,0xd4U,0xa2U,0xafU,0x9cU,0xa4U,0x72U,0xc0U,
        0xb7U,0xfdU,0x93U,0x26U,0x36U,0x3fU,0xf7U,0xccU,0x34U,0xa5U,0xe5U,0xf1U,0x71U,0xd8U,0x31U,0x15U,
        0x04U,0xc7U,0x23U,0xc3U,0x18U,0x96U,0x05U,0x9aU,0x07U,0x12U,0x80U,0xe2U,0xebU,0x27U,0xb2U,0x75U,
        0x09U,0x83U,0x2cU,0x1aU,0x1bU,0x6eU,0x5aU,0xa0U,0x52U,0x3bU,0xd6U,0xb3U,0x29U,0xe3U,0x2fU,0x84U,
        0x53U,0xd1U,0x00U,0xedU,0x20U,0xfcU,0xb1U,0x5bU,0x6aU,0xcbU,0xbeU,0x39U,0x4aU,0x4cU,0x58U,0xcfU,
        0xd0U,0xefU,0xaaU,0xfbU,0x43U,0x4dU,0x33U,0x85U,0x45U,0xf9U,0x02U,0x7fU,0x50U,0x3cU,0x9fU,0xa8U,
        0x51U,0xa3U,0x40U,0x8fU,0x92U,0x9dU,0x38U,0xf5U,0xbcU,0xb6U,0xdaU,0x21U,0x10U,0xffU,0xf3U,0xd2U,
        0xcdU,0x0cU,0x13U,0xecU,0x5fU,0x97U,0x44U,0x17U,0xc4U,0xa7U,0x7eU,0x3dU,0x64U,0x5dU,0x19U,0x73U,
        0x60U,0x81U,0x4fU,0xdcU,0x22U,0x2aU,0x90U,0x88U,0x46U,0xeeU,0xb8U,0x14U,0xdeU,0x5eU,0x0bU,0xdbU,
        0xe0U,0x32U,0x3aU,0x0aU,0x49U,0x06U,0x24U,0x5cU,0xc2U,0xd3U,0xacU,0x62U,0x91U,0x95U,0xe4U,0x79U,
        0xe7U,0xc8U,0x37U,0x6dU,0x8dU,0xd5U,0x4eU,0xa9U,0x6cU,0x56U,0xf4U,0xeaU,0x65U,0x7aU,0xaeU,0x08U,
        0xbaU,0x78U,0x25U,0x2eU,0x1cU,0xa6U,0xb4U,0xc6U,0xe8U,0xddU,0x74U,0x1fU,0x4bU,0xbdU,0x8bU,0x8aU,
        0x70U,0x3eU,0xb5U,0x66U,0x48U,0x03U,0xf6U,0x0eU,0x61U,0x35U,0x57U,0xb9U,0x86U,0xc1U,0x1dU,0x9eU,
        0xe1U,0xf8U,0x98U,0x11U,0x69U,0xd9U,0x8eU,0x94U,0x9bU,0x1eU,0x87U,0xe9U,0xceU,0x55U,0x28U,0xdfU,
        0x8cU,0xa1U,0x89U,0x0dU,0xbfU,0xe6U,0x42U,0x68U,0x41U,0x99U,0x2dU,0x0fU,0xb0U,0x54U,0xbbU,0x16U
        };

        constexpr std::array<std::uint8_t, 256> kInvSbox = {
        0x52U,0x09U,0x6aU,0xd5U,0x30U,0x36U,0xa5U,0x38U,0xbfU,0x40U,0xa3U,0x9eU,0x81U,0xf3U,0xd7U,0xfbU,
        0x7cU,0xe3U,0x39U,0x82U,0x9bU,0x2fU,0xffU,0x87U,0x34U,0x8eU,0x43U,0x44U,0xc4U,0xdeU,0xe9U,0xcbU,
        0x54U,0x7bU,0x94U,0x32U,0xa6U,0xc2U,0x23U,0x3dU,0xeeU,0x4cU,0x95U,0x0bU,0x42U,0xfaU,0xc3U,0x4eU,
        0x08U,0x2eU,0xa1U,0x66U,0x28U,0xd9U,0x24U,0xb2U,0x76U,0x5bU,0xa2U,0x49U,0x6dU,0x8bU,0xd1U,0x25U,
        0x72U,0xf8U,0xf6U,0x64U,0x86U,0x68U,0x98U,0x16U,0xd4U,0xa4U,0x5cU,0xccU,0x5dU,0x65U,0xb6U,0x92U,
        0x6cU,0x70U,0x48U,0x50U,0xfdU,0xedU,0xb9U,0xdaU,0x5eU,0x15U,0x46U,0x57U,0xa7U,0x8dU,0x9dU,0x84U,
        0x90U,0xd8U,0xabU,0x00U,0x8cU,0xbcU,0xd3U,0x0aU,0xf7U,0xe4U,0x58U,0x05U,0xb8U,0xb3U,0x45U,0x06U,
        0xd0U,0x2cU,0x1eU,0x8fU,0xcaU,0x3fU,0x0fU,0x02U,0xc1U,0xafU,0xbdU,0x03U,0x01U,0x13U,0x8aU,0x6bU,
        0x3aU,0x91U,0x11U,0x41U,0x4fU,0x67U,0xdcU,0xeaU,0x97U,0xf2U,0xcfU,0xceU,0xf0U,0xb4U,0xe6U,0x73U,
        0x96U,0xacU,0x74U,0x22U,0xe7U,0xadU,0x35U,0x85U,0xe2U,0xf9U,0x37U,0xe8U,0x1cU,0x75U,0xdfU,0x6eU,
        0x47U,0xf1U,0x1aU,0x71U,0x1dU,0x29U,0xc5U,0x89U,0x6fU,0xb7U,0x62U,0x0eU,0xaaU,0x18U,0xbeU,0x1bU,
        0xfcU,0x56U,0x3eU,0x4bU,0xc6U,0xd2U,0x79U,0x20U,0x9aU,0xdbU,0xc0U,0xfeU,0x78U,0xcdU,0x5aU,0xf4U,
        0x1fU,0xddU,0xa8U,0x33U,0x88U,0x07U,0xc7U,0x31U,0xb1U,0x12U,0x10U,0x59U,0x27U,0x80U,0xecU,0x5fU,
        0x60U,0x51U,0x7fU,0xa9U,0x19U,0xb5U,0x4aU,0x0dU,0x2dU,0xe5U,0x7aU,0x9fU,0x93U,0xc9U,0x9cU,0xefU,
        0xa0U,0xe0U,0x3bU,0x4dU,0xaeU,0x2aU,0xf5U,0xb0U,0xc8U,0xebU,0xbbU,0x3cU,0x83U,0x53U,0x99U,0x61U,
        0x17U,0x2bU,0x04U,0x7eU,0xbaU,0x77U,0xd6U,0x26U,0xe1U,0x69U,0x14U,0x63U,0x55U,0x21U,0x0cU,0x7dU
        };

        constexpr std::array<std::uint8_t, 11> kRcon = {
            0x00U, 0x01U, 0x02U, 0x04U, 0x08U, 0x10U, 0x20U,
            0x40U, 0x80U, 0x1bU, 0x36U};

        using Block = std::array<std::uint8_t, kAes256BlockBytes>;
        using RoundKeys = std::array<std::uint32_t, 60>;

        std::uint8_t xtime(std::uint8_t a) noexcept {
            const std::uint8_t doubled = static_cast<std::uint8_t>(a << 1U);
            return static_cast<std::uint8_t>(
                    ((a & 0x80U) != 0U) ? (doubled ^ 0x1bU) : doubled);
        }

        std::uint8_t gmul(std::uint8_t a, std::uint8_t b) noexcept {
            std::uint8_t result = 0U;
            std::uint8_t x = a;
            for (unsigned i = 0U; i < 8U; ++i) {
                if ((b & 0x01U) != 0U) result = static_cast<std::uint8_t>(result ^ x);
                x = xtime(x);
                b = static_cast<std::uint8_t>(b >> 1U);
            }
            return result;
        }

        RoundKeys expand_key(const std::uint8_t key[kAes256KeyBytes]) noexcept {
            RoundKeys rk{};
            for (std::size_t i = 0U; i < 8U; ++i) {
                rk[i] = (static_cast<std::uint32_t>(key[4U * i]) << 24U) |
                        (static_cast<std::uint32_t>(key[4U * i + 1U]) << 16U) |
                        (static_cast<std::uint32_t>(key[4U * i + 2U]) << 8U) |
                        static_cast<std::uint32_t>(key[4U * i + 3U]);
            }
            for (std::size_t i = 8U; i < rk.size(); ++i) {
                std::uint32_t t = rk[i - 1U];
                if (i % 8U == 0U) {
                    t = (static_cast<std::uint32_t>(kSbox[(t >> 16U) & 0xffU]) << 24U) |
                        (static_cast<std::uint32_t>(kSbox[(t >> 8U) & 0xffU]) << 16U) |
                        (static_cast<std::uint32_t>(kSbox[t & 0xffU]) << 8U) |
                        static_cast<std::uint32_t>(kSbox[(t >> 24U) & 0xffU]);
                    t ^= static_cast<std::uint32_t>(kRcon[i / 8U]) << 24U;
                } else if (i % 8U == 4U) {
                    t = (static_cast<std::uint32_t>(kSbox[(t >> 24U) & 0xffU]) << 24U) |
                        (static_cast<std::uint32_t>(kSbox[(t >> 16U) & 0xffU]) << 16U) |
                        (static_cast<std::uint32_t>(kSbox[(t >> 8U) & 0xffU]) << 8U) |
                        static_cast<std::uint32_t>(kSbox[t & 0xffU]);
                }
                rk[i] = rk[i - 8U] ^ t;
            }
            return rk;
        }

        void add_round_key(Block &s, const RoundKeys &rk, std::size_t round) noexcept {
            for (std::size_t c = 0U; c < 4U; ++c) {
                const std::uint32_t w = rk[round * 4U + c];
                const std::size_t base = 4U * c;
                s[base] = static_cast<std::uint8_t>(
                        static_cast<std::uint32_t>(s[base]) ^ ((w >> 24U) & 0xffU));
                s[base + 1U] = static_cast<std::uint8_t>(
                        static_cast<std::uint32_t>(s[base + 1U]) ^ ((w >> 16U) & 0xffU));
                s[base + 2U] = static_cast<std::uint8_t>(
                        static_cast<std::uint32_t>(s[base + 2U]) ^ ((w >> 8U) & 0xffU));
                s[base + 3U] = static_cast<std::uint8_t>(
                        static_cast<std::uint32_t>(s[base + 3U]) ^ (w & 0xffU));
            }
        }

        void sub_bytes(Block &s, const std::array<std::uint8_t, 256> &box) noexcept {
            for (std::size_t i = 0U; i < s.size(); ++i) s[i] = box[s[i]];
        }

        void shift_rows(Block &s) noexcept {
            const Block t = s;
            for (std::size_t c = 0U; c < 4U; ++c) {
                for (std::size_t r = 0U; r < 4U; ++r) {
                    s[r + 4U * c] = t[r + 4U * ((c + r) % 4U)];
                }
            }
        }

        void inv_shift_rows(Block &s) noexcept {
            const Block t = s;
            for (std::size_t c = 0U; c < 4U; ++c) {
                for (std::size_t r = 0U; r < 4U; ++r) {
                    s[r + 4U * c] = t[r + 4U * ((c + 4U - r) % 4U)];
                }
            }
        }

        void mix_columns(Block &s) noexcept {
            for (std::size_t c = 0U; c < 4U; ++c) {
                const std::size_t b = 4U * c;
                const std::uint8_t a0 = s[b];
                const std::uint8_t a1 = s[b + 1U];
                const std::uint8_t a2 = s[b + 2U];
                const std::uint8_t a3 = s[b + 3U];
                s[b] = static_cast<std::uint8_t>(
                        gmul(a0, 0x02U) ^ gmul(a1, 0x03U) ^ a2 ^ a3);
                s[b + 1U] = static_cast<std::uint8_t>(
                        a0 ^ gmul(a1, 0x02U) ^ gmul(a2, 0x03U) ^ a3);
                s[b + 2U] = static_cast<std::uint8_t>(
                        a0 ^ a1 ^ gmul(a2, 0x02U) ^ gmul(a3, 0x03U));
                s[b + 3U] = static_cast<std::uint8_t>(
                        gmul(a0, 0x03U) ^ a1 ^ a2 ^ gmul(a3, 0x02U));
            }
        }

        void inv_mix_columns(Block &s) noexcept {
            for (std::size_t c = 0U; c < 4U; ++c) {
                const std::size_t b = 4U * c;
                const std::uint8_t a0 = s[b];
                const std::uint8_t a1 = s[b + 1U];
                const std::uint8_t a2 = s[b + 2U];
                const std::uint8_t a3 = s[b + 3U];
                s[b] = static_cast<std::uint8_t>(
                        gmul(a0, 0x0eU) ^ gmul(a1, 0x0bU) ^ gmul(a2, 0x0dU) ^ gmul(a3, 0x09U));
                s[b + 1U] = static_cast<std::uint8_t>(
                        gmul(a0, 0x09U) ^ gmul(a1, 0x0eU) ^ gmul(a2, 0x0bU) ^ gmul(a3, 0x0dU));
                s[b + 2U] = static_cast<std::uint8_t>(
                        gmul(a0, 0x0dU) ^ gmul(a1, 0x09U) ^ gmul(a2, 0x0eU) ^ gmul(a3, 0x0bU));
                s[b + 3U] = static_cast<std::uint8_t>(
                        gmul(a0, 0x0bU) ^ gmul(a1, 0x0dU) ^ gmul(a2, 0x09U) ^ gmul(a3, 0x0eU));
            }
        }

        Block encrypt_block(const RoundKeys &rk, const std::uint8_t in[kAes256BlockBytes]) noexcept {
            Block s{};
            for (std::size_t i = 0U; i < s.size(); ++i) s[i] = in[i];
            add_round_key(s, rk, 0U);
            for (std::size_t round = 1U; round < 14U; ++round) {
                sub_bytes(s, kSbox);
                shift_rows(s);
                mix_columns(s);
                add_round_key(s, rk, round);
            }
            sub_bytes(s, kSbox);
            shift_rows(s);
            add_round_key(s, rk, 14U);
            return s;
        }

        Block decrypt_block(const RoundKeys &rk, const std::uint8_t in[kAes256BlockBytes]) noexcept {
            Block s{};
            for (std::size_t i = 0U; i < s.size(); ++i) s[i] = in[i];
            add_round_key(s, rk, 14U);
            for (std::size_t round = 13U; round > 0U; --round) {
                inv_shift_rows(s);
                sub_bytes(s, kInvSbox);
                add_round_key(s, rk, round);
                inv_mix_columns(s);
            }
            inv_shift_rows(s);
            sub_bytes(s, kInvSbox);
            add_round_key(s, rk, 0U);
            return s;
        }

        bool ecb_crypt(const std::uint8_t key[kAes256KeyBytes],
                       const std::uint8_t in[kAes256BlockBytes],
                       std::uint8_t out[kAes256BlockBytes], bool encrypt) noexcept {
            if (key == nullptr || in == nullptr || out == nullptr) return false;
            RoundKeys rk = expand_key(key);
            const Block block = encrypt ? encrypt_block(rk, in) : decrypt_block(rk, in);
            for (std::size_t i = 0U; i < block.size(); ++i) out[i] = block[i];
            zeroize_bytes(&rk, sizeof(rk));
            return true;
        }

        bool cbc_crypt(const std::uint8_t key[kAes256KeyBytes],
                       const std::uint8_t iv[kAes256BlockBytes],
                       const std::uint8_t *in, std::size_t in_len,
                       std::uint8_t *out, bool encrypt) noexcept {
            if (key == nullptr || iv == nullptr || out == nullptr) return false;
            if (in_len % kAes256BlockBytes != 0U) return false;
            if (in == nullptr && in_len != 0U) return false;

            RoundKeys rk = expand_key(key);
            Block chain{};
            for (std::size_t i = 0U; i < chain.size(); ++i) chain[i] = iv[i];

            for (std::size_t off = 0U; off < in_len; off += kAes256BlockBytes) {
                Block cur{};
                for (std::size_t i = 0U; i < cur.size(); ++i) {
                    cur[i] = in[off + i];
                }
                if (encrypt) {
                    for (std::size_t i = 0U; i < cur.size(); ++i) {
                        cur[i] = static_cast<std::uint8_t>(cur[i] ^ chain[i]);
                    }
                    const Block enc = encrypt_block(rk, cur.data());
                    for (std::size_t i = 0U; i < enc.size(); ++i) {
                        out[off + i] = enc[i];
                        chain[i] = enc[i];
                    }
                } else {
                    const Block dec = decrypt_block(rk, cur.data());
                    for (std::size_t i = 0U; i < dec.size(); ++i) {
                        out[off + i] = static_cast<std::uint8_t>(dec[i] ^ chain[i]);
                    }
                    chain = cur;
                }
            }
            zeroize_bytes(&rk, sizeof(rk));
            zeroize_bytes(chain.data(), chain.size());
            return true;
        }

    } // namespace

    bool aes256_ecb_encrypt(const std::uint8_t key[kAes256KeyBytes],
                            const std::uint8_t in[kAes256BlockBytes],
                            std::uint8_t out[kAes256BlockBytes]) noexcept {
        return ecb_crypt(key, in, out, true);
    }

    bool aes256_ecb_decrypt(const std::uint8_t key[kAes256KeyBytes],
                            const std::uint8_t in[kAes256BlockBytes],
                            std::uint8_t out[kAes256BlockBytes]) noexcept {
        return ecb_crypt(key, in, out, false);
    }

    bool aes256_cbc_encrypt(const std::uint8_t key[kAes256KeyBytes],
                            const std::uint8_t iv[kAes256BlockBytes],
                            const std::uint8_t *in, std::size_t in_len,
                            std::uint8_t *out) noexcept {
        return cbc_crypt(key, iv, in, in_len, out, true);
    }

    bool aes256_cbc_decrypt(const std::uint8_t key[kAes256KeyBytes],
                            const std::uint8_t iv[kAes256BlockBytes],
                            const std::uint8_t *in, std::size_t in_len,
                            std::uint8_t *out) noexcept {
        return cbc_crypt(key, iv, in, in_len, out, false);
    }

} // namespace ghostlock::backend::cve_2026_43284
