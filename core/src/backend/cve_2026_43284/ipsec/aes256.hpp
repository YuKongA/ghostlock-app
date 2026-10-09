#pragma once
/** @file backend/cve_2026_43284/ipsec/aes256.hpp
 *  AES-256 (FIPS 197): single-block ECB and whole-block CBC for the CVE-2026-43284
 *  ESP primitive.
 *
 *  Ported/rewritten for GhostLock from
 *  DirtyFrag-Android-Root-Jailbreak@de2ab7b usermode/ankit/aes256.h
 *  (ankitrawatgit/DirtyFrag-Android-Root-Jailbreak, commit de2ab7b), whose AES
 *  core derives from Odzhan's BSD-3-Clause implementation. The upstream
 *  repository ships no top-level LICENSE, so this is an independent rewrite
 *  with attribution retained.
 *
 *  Scope: the two modes the old file carried. Single-block ECB decryption is
 *  what feeds the CBC IV identity (see the ipsec component); whole-block CBC is
 *  the ESP transform's own mode.
 *
 *  The primitive is pure and host-testable: no syscalls, no kernel state, no
 *  mutable global state. The expanded key schedule and the CBC chain value are
 *  wiped before return.
 *
 *  ---------------------------------------------------------------------------
 *  Copyright (c) 2018, Odzhan. All Rights Reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 *  EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 *  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 *  OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *  WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 *  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *  ---------------------------------------------------------------------------
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43284::ipsec {
    /** The key width this primitive takes: AES-256, in bytes. */
    inline constexpr std::size_t kAes256KeyBytes = 32;
    /** The block width this primitive works in, in bytes. */
    inline constexpr std::size_t kAes256BlockBytes = 16;

    /** Single-block ECB encryption.
     *  @param key The 32-byte key.
     *  @param in  The 16-byte input block; in-place (in == out) is supported.
     *  @param out The 16-byte output block.
     *  @return false on a null pointer (any of the three), true otherwise.
     *  @note The expanded key schedule is wiped before returning. */
    [[nodiscard]] bool aes256_ecb_encrypt(const std::uint8_t key[kAes256KeyBytes],
                                          const std::uint8_t in[kAes256BlockBytes],
                                          std::uint8_t out[kAes256BlockBytes]) noexcept;

    /** Single-block ECB decryption.
     *  @param key The 32-byte key.
     *  @param in  The 16-byte input block; in-place (in == out) is supported.
     *  @param out The 16-byte output block.
     *  @return false on a null pointer (any of the three), true otherwise.
     *  @note The expanded key schedule is wiped before returning. */
    [[nodiscard]] bool aes256_ecb_decrypt(const std::uint8_t key[kAes256KeyBytes],
                                          const std::uint8_t in[kAes256BlockBytes],
                                          std::uint8_t out[kAes256BlockBytes]) noexcept;

    /** Whole-block CBC encryption; @p in_len must be a whole number of blocks.
     *  @param key    The 32-byte key.
     *  @param iv     The 16-byte initialisation vector.
     *  @param in     The input bytes, @p in_len of them; may alias @p out. A null
     *                pointer is allowed only when @p in_len is zero.
     *  @param in_len The input length; a non-block length is refused.
     *  @param out    The output bytes, @p in_len of them.
     *  @return false on a null key, iv or out, or on a non-block length; nothing
     *          is written then.
     *  @note in-place (in == out) is supported; the key schedule and the chain
     *        value are wiped before returning. */
    [[nodiscard]] bool aes256_cbc_encrypt(const std::uint8_t key[kAes256KeyBytes],
                                          const std::uint8_t iv[kAes256BlockBytes],
                                          const std::uint8_t *in, std::size_t in_len,
                                          std::uint8_t *out) noexcept;

    /** Whole-block CBC decryption; @p in_len must be a whole number of blocks.
     *  @param key    The 32-byte key.
     *  @param iv     The 16-byte initialisation vector.
     *  @param in     The ciphertext bytes, @p in_len of them; may alias @p out. A
     *                null pointer is allowed only when @p in_len is zero.
     *  @param in_len The input length; a non-block length is refused.
     *  @param out    The plaintext bytes, @p in_len of them.
     *  @return false on a null key, iv or out, or on a non-block length; nothing
     *          is written then.
     *  @note in-place (in == out) is supported; the key schedule and the chain
     *        value are wiped before returning. */
    [[nodiscard]] bool aes256_cbc_decrypt(const std::uint8_t key[kAes256KeyBytes],
                                          const std::uint8_t iv[kAes256BlockBytes],
                                          const std::uint8_t *in, std::size_t in_len,
                                          std::uint8_t *out) noexcept;
} // namespace ghostlock::backend::cve_2026_43284::ipsec
