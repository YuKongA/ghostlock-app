#ifndef GHOSTLOCK_SUPPORT_SHA256_HPP
#define GHOSTLOCK_SUPPORT_SHA256_HPP

/* Single SHA-256 (FIPS 180-4) and HMAC-SHA256 (RFC 2104) implementation.
 *
 * S4 R8: the tree used to carry two copies -- plugin/sha256.* for the
 * countermeasure loader and backend/cve_2026_43284/ipsec/hmac_sha256.* for the
 * ESP ICV -- because the R1 include firewall forbids backend/ from reaching
 * plugin/. support/ is the base layer every layer may include, so the one
 * implementation lives here: no duplicated algorithm and no cross-layer edge.
 *
 * The HMAC-SHA256 core is Odzhan's BSD-3-Clause implementation (attribution
 * retained below); the SHA-256 core implements the same public algorithm as the
 * two former copies and produces byte-identical digests. Pure and
 * host-testable: no globals, no allocation, and no syscalls beyond the read
 * loop in sha256_file(). Working contexts, key pads and intermediate hashes are
 * wiped before return.
 *
 * ---------------------------------------------------------------------------
 * Copyright (c) 2018, Odzhan. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ---------------------------------------------------------------------------
 */

#include <cstddef>
#include <cstdint>

namespace ghostlock::support {
    /* Hex digest length (64 lowercase digits, no NUL). */
    inline constexpr std::size_t kSha256HexLength = 64u;
    inline constexpr std::size_t kSha256DigestBytes = 32u;

    /* Hashes len bytes of data into out[32]. A NULL data pointer means an empty
     * input. */
    void sha256(const std::uint8_t *data, std::size_t len,
                std::uint8_t out[kSha256DigestBytes]) noexcept;

    /* Reads the whole file at path and writes 64 lowercase hex digits plus a NUL
     * terminator into out_hex. Returns 0 on success, -1 on any I/O error or when
     * cap is smaller than kSha256HexLength + 1. */
    std::int32_t sha256_file(const char *path, char *out_hex,
                             std::size_t cap) noexcept;

    /* HMAC-SHA256 (RFC 2104); the full 32-byte MAC is written and the ESP layer
     * truncates it to the frame's icv_len. Null key/msg pointers are treated as
     * empty inputs. */
    void hmac_sha256(const std::uint8_t *key, std::size_t key_len,
                     const std::uint8_t *msg, std::size_t msg_len,
                     std::uint8_t out[kSha256DigestBytes]) noexcept;
} // namespace ghostlock::support

#endif
