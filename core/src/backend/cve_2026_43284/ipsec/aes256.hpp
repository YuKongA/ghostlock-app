#pragma once
/** @file backend/cve_2026_43284/ipsec/aes256.hpp
 *  AES-256 (FIPS 197) single-block ECB for the CVE-2026-43284 ESP primitive.
 *
 *  Ported/rewritten for GhostLock from
 *  DirtyFrag-Android-Root-Jailbreak@de2ab7b usermode/ankit/aes256.h
 *  (ankitrawatgit/DirtyFrag-Android-Root-Jailbreak, commit de2ab7b), whose AES
 *  core derives from Odzhan's BSD-3-Clause implementation. The upstream
 *  repository ships no top-level LICENSE, so this is an independent rewrite
 *  with attribution retained.
 *
 *  Scope: the single block only. The whole-block CBC mode that shared the old
 *  file is a separate port and is deliberately not here; single-block ECB
 *  decryption is what feeds the CBC IV identity (see the ipsec component).
 *
 *  The primitive is pure and host-testable: no syscalls, no kernel state, no
 *  mutable global state. The expanded key schedule is wiped before return.
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
} // namespace ghostlock::backend::cve_2026_43284::ipsec
