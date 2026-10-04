#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_EMBED_SPLICEHELPER_BLOB_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_EMBED_SPLICEHELPER_BLOB_HPP

/* CVE-2026-43284 patch #1 payload: the upstream DirtyFrag `splicehelper`.
 *
 * Provenance (read-only vendored reference plan, independent of the running
 * device):
 *   source      upstream ankitrawatgit/DirtyFrag-Android-Root-Jailbreak
 *               app/src/main/jni/splicehelper.c, HEAD
 *               de2ab7be69dc159af508d584523fd4d5c0b7cc7a
 *               (the argv[1]=offset / argv[2]=path / argv[3]="r" helper that
 *               matches usermode/ankit/exp.c; the lspromise variant hard-codes
 *               a vendor path and is deliberately NOT used)
 *   build       aarch64-linux-android30-clang splicehelper.c -o splicehelper \
 *                 -nodefaultlibs -nostartfiles -ffreestanding -static
 *               llvm-strip splicehelper
 *               (NDK 30.0.16248370; the repo does not vendor splicehelper.c,
 *               so the bytes are generated from that upstream source and
 *               embedded here as a C array, the .incbin equivalent)
 *   size        1432 bytes, zero-padded to 1440 (16-byte page-cache block)
 *   sha256      3c0a108e637954c114df21f03bda95eb691e66f60333b9d78439d9550482545c
 *
 * The patch step only needs the bytes and their length; nothing here parses
 * the helper. ADR-0004 R1: no pipeline/ include. */

#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43284::embed {

    inline constexpr std::size_t kSpliceHelperSize = 1432U;
    inline constexpr std::size_t kSpliceHelperPaddedSize = 1440U;
    inline constexpr char kSpliceHelperSha256[] =
            "3c0a108e637954c114df21f03bda95eb691e66f60333b9d78439d9550482545c";

    [[nodiscard]] const std::uint8_t *splicehelper_bytes() noexcept;
    [[nodiscard]] std::size_t splicehelper_size() noexcept;
    [[nodiscard]] std::size_t splicehelper_padded_size() noexcept;

} // namespace ghostlock::backend::cve_2026_43284::embed

#endif
