#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_IPSEC_HMAC_SHA256_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_IPSEC_HMAC_SHA256_HPP

/*
 * HMAC-SHA256 (RFC 2104) over SHA-256 (FIPS 180-4).
 *
 * Ported/rewritten for GhostLock from
 * DirtyFrag-Android-Root-Jailbreak@de2ab7b usermode/ankit/hmac_sha256.h
 * (ankitrawatgit/DirtyFrag-Android-Root-Jailbreak, commit de2ab7b), whose
 * SHA-256/HMAC core derives from Odzhan's BSD-3-Clause implementation. The
 * upstream repository ships no top-level LICENSE, so this is an independent
 * rewrite with attribution retained.
 *
 * The full 32-byte MAC is written; the ESP layer truncates it to the frame's
 * icv_len (see ipsec.hpp). Pure and host-testable: no syscalls, no globals.
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

    /* Writes the full 32-byte MAC; the caller truncates to the ESP icv_len.
     * Null key/msg pointers are treated as empty inputs. */
    void hmac_sha256(const std::uint8_t *key, std::size_t key_len,
                     const std::uint8_t *msg, std::size_t msg_len,
                     std::uint8_t out[32]) noexcept;

} // namespace ghostlock::backend::cve_2026_43284

#endif
