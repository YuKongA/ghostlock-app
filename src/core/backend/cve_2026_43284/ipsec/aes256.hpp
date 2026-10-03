#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_IPSEC_AES256_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_IPSEC_AES256_HPP

/*
 * AES-256 (FIPS 197): single-block ECB and whole-block CBC.
 *
 * Ported/rewritten for GhostLock from
 * third_party/dirtyfrag/usermode/ankit/aes256.h
 * (ankitrawatgit/DirtyFrag-Android-Root-Jailbreak, commit de2ab7b), whose AES
 * core derives from Odzhan's BSD-3-Clause implementation. The upstream
 * repository ships no top-level LICENSE, so this is an independent rewrite
 * with attribution retained.
 *
 * The primitives are pure and host-testable: no syscalls, no kernel state.
 * ECB single-block decryption feeds the CBC IV identity (see ipsec.hpp); CBC
 * models the kernel-side ESP transform.
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

namespace ghostlock::backend::cve_2026_43284 {

    inline constexpr std::size_t kAes256KeyBytes = 32;
    inline constexpr std::size_t kAes256BlockBytes = 16;

    /* Single-block ECB. Returns false on a null pointer. In-place (in == out) is
     * supported. */
    [[nodiscard]] bool aes256_ecb_encrypt(const std::uint8_t key[kAes256KeyBytes],
                                          const std::uint8_t in[kAes256BlockBytes],
                                          std::uint8_t out[kAes256BlockBytes]) noexcept;
    [[nodiscard]] bool aes256_ecb_decrypt(const std::uint8_t key[kAes256KeyBytes],
                                          const std::uint8_t in[kAes256BlockBytes],
                                          std::uint8_t out[kAes256BlockBytes]) noexcept;

    /* Whole-block CBC. in_len must be a multiple of kAes256BlockBytes; in-place
     * (in == out) is supported. Returns false on a null pointer or a non-block
     * length. */
    [[nodiscard]] bool aes256_cbc_encrypt(const std::uint8_t key[kAes256KeyBytes],
                                          const std::uint8_t iv[kAes256BlockBytes],
                                          const std::uint8_t *in, std::size_t in_len,
                                          std::uint8_t *out) noexcept;
    [[nodiscard]] bool aes256_cbc_decrypt(const std::uint8_t key[kAes256KeyBytes],
                                          const std::uint8_t iv[kAes256BlockBytes],
                                          const std::uint8_t *in, std::size_t in_len,
                                          std::uint8_t *out) noexcept;

} // namespace ghostlock::backend::cve_2026_43284

#endif
