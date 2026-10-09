#pragma once
/** @file support/hmac_sha256.hpp
 *  HMAC-SHA256 (RFC 2104) over the tree's single SHA-256.
 *
 *  The 43284 ESP write needs an ICV over one datagram; this is that MAC. The
 *  full kSha256Bytes bytes are written and the ESP layer truncates to its
 *  frame's icv_len. Neutral by construction: no I/O, no globals, no state
 *  outside the arguments, and no allocation on any path -- a message longer
 *  than kHmacInlineMessageBytes is refused rather than buffered, because this
 *  is called from the write path and the window's rules forbid allocating
 *  inside the window body. The device path (an ESP-in-UDP datagram is at most
 *  72 bytes) is well inside the bound; a streaming hash would lift it and is a
 *  later option.
 *
 *  Null key or message pointers are treated as empty inputs. Key pads, a hashed
 *  key and both intermediate digests are wiped before return.
 */
#include <cstddef>
#include <cstdint>

#include "support/sha256.hpp"

namespace ghostlock::support {
    /** Messages up to this many bytes hash without a temporary buffer. */
    inline constexpr std::size_t kHmacInlineMessageBytes = 128U;

    /** Writes the full kSha256Bytes-byte HMAC-SHA256 of @p msg under @p key.
     *  @param key     key bytes; a null pointer means an empty key.
     *  @param key_len key length; a key longer than the SHA-256 block is hashed first.
     *  @param msg     message bytes; a null pointer means an empty message.
     *  @param msg_len message length.
     *  @param out     receives the MAC; a null pointer makes the call a no-op.
     *  @note a message longer than kHmacInlineMessageBytes is copied through a
     *        temporary buffer before the inner hash; nothing else allocates. */
    void hmac_sha256(const std::uint8_t *key, std::size_t key_len,
                     const std::uint8_t *msg, std::size_t msg_len,
                     std::uint8_t out[kSha256Bytes]) noexcept;
}  // namespace ghostlock::support
