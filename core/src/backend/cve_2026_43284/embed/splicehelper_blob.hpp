#pragma once
/** @file backend/cve_2026_43284/embed/splicehelper_blob.hpp
 *  Patch #1's payload: the upstream DirtyFrag `splicehelper`, as bytes.
 *
 *  Provenance, copied from the tree that used it in the device run this project's 43284 chain PASSed
 *  with (commit 6f7ded87, src/core/backend/cve_2026_43284/embed/splicehelper_blob.hpp):
 *
 *    source    upstream ankitrawatgit/DirtyFrag-Android-Root-Jailbreak,
 *              app/src/main/jni/splicehelper.c, HEAD de2ab7be69dc159af508d584523fd4d5c0b7cc7a
 *              (the argv[1]=offset / argv[2]=path / argv[3]="r" helper that matches
 *              usermode/ankit/exp.c; the lspromise variant hard-codes a vendor path and is
 *              deliberately not used)
 *    build     aarch64-linux-android30-clang splicehelper.c -o splicehelper -nodefaultlibs \
 *                -nostartfiles -ffreestanding -static && llvm-strip splicehelper
 *              (NDK 30.0.16248370; the repository does not vendor splicehelper.c, so the bytes are
 *              carried here as a C array)
 *    size      1432 bytes, zero-padded to 1440 -- the sixteen-byte page-cache block this project
 *              writes in, so the patch is a whole number of blocks
 *    sha256    3c0a108e637954c114df21f03bda95eb691e66f60333b9d78439d9550482545c (of the 1432)
 *
 *  Regeneration of the .cpp (the array is generated, never edited by hand):
 *    git show 6f7ded87:src/core/backend/cve_2026_43284/embed/splicehelper_blob.cpp \
 *      | sed -n '/kSpliceHelper\[kSpliceHelperPaddedSize\]/,/^        };/p' \
 *      | grep -oE '0x[0-9a-fA-F]{2}U' | sed 's/0x//; s/U//' | tr -d '\n' | xxd -r -p \
 *      > /tmp/splicehelper_padded.bin
 *    # then re-emit the array from that file and paste it between the markers below.
 *
 *  Nothing here parses the helper: patch #1 needs the bytes and their length.
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43284::embed {
    /** The raw helper. */
    inline constexpr std::size_t kSpliceHelperSize = 1432U;
    /** The helper padded to the page-cache block, which is what patch #1 writes. */
    inline constexpr std::size_t kSpliceHelperPaddedSize = 1440U;
    /** The digest the bytes were verified against. */
    inline constexpr char kSpliceHelperSha256[] =
        "3c0a108e637954c114df21f03bda95eb691e66f60333b9d78439d9550482545c";

    /** The padded helper, ready to be written at offset zero of the patch target. */
    [[nodiscard]] const std::uint8_t *splicehelper_bytes() noexcept;
    /** The unpadded length, for a caller that wants to compare against the digest. */
    [[nodiscard]] std::size_t splicehelper_size() noexcept;
    /** The padded length, which is what patch #1 writes. */
    [[nodiscard]] std::size_t splicehelper_padded_size() noexcept;
}  // namespace ghostlock::backend::cve_2026_43284::embed
