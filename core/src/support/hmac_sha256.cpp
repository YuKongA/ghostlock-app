/** @file support/hmac_sha256.cpp
 *  HMAC-SHA256 (RFC 2104) computed over the one SHA-256 in support/sha256.cpp.
 *
 *  RFC 2104 defines the MAC as H((K0 ^ opad) || H((K0 ^ ipad) || msg)), with K0
 *  the key zero-padded to the 64-byte block (its own digest when it is longer).
 *  The inner hash receives its two parts as one contiguous buffer, so the
 *  message is assembled after the inner pad: on the stack up to
 *  kHmacInlineMessageBytes, through a temporary buffer beyond that. The outer
 *  hash is always 96 stack bytes. Key pads, the hashed key and both digests are
 *  wiped before return; no globals, no I/O.
 */
#include "support/hmac_sha256.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ghostlock::support {
    namespace {
        /* SHA-256's block size (FIPS 180-4 section 5.2.1). */
        constexpr std::size_t kBlockBytes = 64U;
        /* RFC 2104 section 2: the inner and outer key pads. */
        constexpr std::uint8_t kInnerPadByte = 0x36U;
        constexpr std::uint8_t kOuterPadByte = 0x5cU;

        /* Compiler-resistant wipe: volatile stores the optimiser may not drop. */
        void wipe(void *data, std::size_t len) noexcept {
            auto *bytes = static_cast<volatile std::uint8_t *>(data);
            for (std::size_t i = 0U; i < len; ++i) {
                bytes[i] = 0U;
            }
        }

        /* Fills padded[64] with K0: the key itself when it fits the block, else
         * its digest, zero-padded either way. */
        void normalize_key(const std::uint8_t *key, std::size_t key_len,
                           std::uint8_t padded[kBlockBytes]) noexcept {
            for (std::size_t i = 0U; i < kBlockBytes; ++i) {
                padded[i] = 0U;
            }
            if (key_len > kBlockBytes) {
                Sha256Digest hashed = sha256(key, key_len);
                for (std::size_t i = 0U; i < kSha256Bytes; ++i) {
                    padded[i] = hashed.bytes[i];
                }
                wipe(&hashed, sizeof(hashed));
            } else {
                for (std::size_t i = 0U; i < key_len; ++i) {
                    padded[i] = key[i];
                }
            }
        }
    }  // namespace

    void hmac_sha256(const std::uint8_t *key, std::size_t key_len,
                     const std::uint8_t *msg, std::size_t msg_len,
                     std::uint8_t out[kSha256Bytes]) noexcept {
        if (out == nullptr) {
            return;
        }
        if (key == nullptr) {
            key_len = 0U;
        }
        if (msg == nullptr) {
            msg_len = 0U;
        }
        /* A message past the inline bound is refused rather than given a temporary buffer. This runs
         *  from the write path, and the window's rules forbid allocating inside the window body, so
         *  the bound is a property of where this is called from rather than a performance choice. A
         *  streaming hash would lift it and is a later option, not this piece. */
        if (msg_len > kHmacInlineMessageBytes) {
            return;
        }

        std::array<std::uint8_t, kBlockBytes> key_block{};
        normalize_key(key, key_len, key_block.data());

        std::array<std::uint8_t, kBlockBytes> inner_pad{};
        std::array<std::uint8_t, kBlockBytes> outer_pad{};
        for (std::size_t i = 0U; i < kBlockBytes; ++i) {
            inner_pad[i] = static_cast<std::uint8_t>(kInnerPadByte ^ key_block[i]);
            outer_pad[i] = static_cast<std::uint8_t>(kOuterPadByte ^ key_block[i]);
        }

        /* The inner hash: H(ipad || msg), assembled on the stack. The bound was checked above, so
         *  this is the only path and it needs no buffer of its own. */
        const std::size_t inner_len = kBlockBytes + msg_len;
        std::array<std::uint8_t, kBlockBytes + kHmacInlineMessageBytes> inline_input{};
        Sha256Digest inner{};
        for (std::size_t i = 0U; i < kBlockBytes; ++i) {
            inline_input[i] = inner_pad[i];
        }
        for (std::size_t i = 0U; i < msg_len; ++i) {
            inline_input[kBlockBytes + i] = msg[i];
        }
        inner = sha256(inline_input.data(), inner_len);

        /* The outer hash: H(opad || inner). */
        std::array<std::uint8_t, kBlockBytes + kSha256Bytes> outer_input{};
        for (std::size_t i = 0U; i < kBlockBytes; ++i) {
            outer_input[i] = outer_pad[i];
        }
        for (std::size_t i = 0U; i < kSha256Bytes; ++i) {
            outer_input[kBlockBytes + i] = inner.bytes[i];
        }
        Sha256Digest mac = sha256(outer_input.data(), outer_input.size());
        for (std::size_t i = 0U; i < kSha256Bytes; ++i) {
            out[i] = mac.bytes[i];
        }

        wipe(key_block.data(), key_block.size());
        wipe(inner_pad.data(), inner_pad.size());
        wipe(outer_pad.data(), outer_pad.size());
        wipe(inline_input.data(), inline_input.size());
        wipe(&inner, sizeof(inner));
        wipe(outer_input.data(), outer_input.size());
        wipe(&mac, sizeof(mac));
    }
}  // namespace ghostlock::support
