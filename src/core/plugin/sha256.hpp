#ifndef GHOSTLOCK_PLUGIN_SHA256_HPP
#define GHOSTLOCK_PLUGIN_SHA256_HPP

/* CM-2: self-contained SHA-256 (FIPS 180-4) for the countermeasure loader.
 *
 * The tree already ships a SHA-256 inside the CVE-2026-43284 IpSec backend, but
 * backend/ is forbidden to platform/ by the R1 include firewall. Rather than
 * whitelist a cross-layer edge (or add a third-party dependency), this is an
 * independent implementation of the public FIPS 180-4 algorithm, owned by the
 * loader's own layer. It backs LoaderOps::sha256_file's production default.
 *
 * No global state, no syscalls beyond file reads, no allocation. */

#include <cstddef>
#include <cstdint>

namespace ghostlock::plugin {

    /* Hex digest length (64 lowercase digits, no NUL). */
    inline constexpr std::size_t kSha256HexLength = 64u;

    /* Hashes len bytes of data into out[32]. A NULL data pointer means an empty
     * input. */
    void sha256(const std::uint8_t *data, std::size_t len,
                std::uint8_t out[32]) noexcept;

    /* Reads the whole file at path and writes 64 lowercase hex digits plus a
     * NUL terminator into out_hex. Returns 0 on success, -1 on any I/O error or
     * when cap is smaller than 65. */
    std::int32_t sha256_file(const char *path, char *out_hex,
                             std::size_t cap) noexcept;

} // namespace ghostlock::plugin

#endif
