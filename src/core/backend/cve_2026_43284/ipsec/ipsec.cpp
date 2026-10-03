/* CVE-2026-43284 IpSec primitive helpers (B5-2): ESP framing, truncation ICV,
 * CBC IV derivation and the explicit wipe the session-secret lifecycle
 * requires (plan section 6.5). The AES/HMAC cores live in aes256.cpp and
 * hmac_sha256.cpp. */

#include "backend/cve_2026_43284/ipsec/ipsec.hpp"

#include "backend/cve_2026_43284/ipsec/aes256.hpp"
#include "backend/cve_2026_43284/ipsec/hmac_sha256.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43284 {
    namespace {
        /* Bytes covered by the ESP ICV: SPI || Seq || IV || ciphertext. */
        constexpr std::size_t kEspAuthenticatedBytes =
                kEspHeaderBytes + kEspIvBytes + kEspBlockBytes;
    } // namespace

    void zeroize_bytes(void *data, std::size_t len) noexcept {
        if (data == nullptr) return;
        auto *bytes = static_cast<volatile std::uint8_t *>(data);
        for (std::size_t i = 0; i < len; ++i) {
            bytes[i] = 0U;
        }
    }

    void zeroize(IpsecSaParams &sa) noexcept {
        zeroize_bytes(&sa, sizeof(IpsecSaParams));
    }

    void compute_cbc_iv(const std::uint8_t old_content[16],
                        const std::uint8_t desired[16],
                        const std::uint8_t aes_key[32],
                        std::uint8_t iv_out[16]) noexcept {
        std::array<std::uint8_t, kEspBlockBytes> decrypted{};
        if (!aes256_ecb_decrypt(aes_key, old_content, decrypted.data())) {
            zeroize_bytes(iv_out, kEspBlockBytes);
            return;
        }
        for (std::size_t i = 0; i < kEspBlockBytes; ++i) {
            iv_out[i] = static_cast<std::uint8_t>(decrypted[i] ^ desired[i]);
        }
        zeroize_bytes(decrypted.data(), decrypted.size());
    }

    std::size_t esp_datagram_bytes(std::size_t icv_len) noexcept {
        if (icv_len == 0U || icv_len > kEspIcvMaxBytes) return 0U;
        return kEspHeaderBytes + kEspIvBytes + kEspBlockBytes + icv_len;
    }

    std::size_t esp_compute_icv(const IpsecSaParams &sa,
                                const std::uint8_t header[8],
                                const std::uint8_t iv[16],
                                const std::uint8_t ciphertext[16],
                                std::uint8_t *icv_out,
                                std::size_t icv_capacity) noexcept {
        if (header == nullptr || iv == nullptr || ciphertext == nullptr ||
            icv_out == nullptr) {
            return 0U;
        }
        const std::size_t icv_len = static_cast<std::size_t>(sa.icv_len);
        if (icv_len == 0U || icv_len > kEspIcvMaxBytes || icv_capacity < icv_len) {
            return 0U;
        }

        std::array<std::uint8_t, kEspAuthenticatedBytes> mac_input{};
        for (std::size_t i = 0; i < kEspHeaderBytes; ++i) mac_input[i] = header[i];
        for (std::size_t i = 0; i < kEspIvBytes; ++i) mac_input[kEspHeaderBytes + i] = iv[i];
        for (std::size_t i = 0; i < kEspBlockBytes; ++i) {
            mac_input[kEspHeaderBytes + kEspIvBytes + i] = ciphertext[i];
        }

        std::array<std::uint8_t, kEspIcvMaxBytes> full{};
        hmac_sha256(sa.hmac_key.data(), sa.hmac_key.size(), mac_input.data(),
                    mac_input.size(), full.data());
        for (std::size_t i = 0; i < icv_len; ++i) icv_out[i] = full[i];

        zeroize_bytes(full.data(), full.size());
        zeroize_bytes(mac_input.data(), mac_input.size());
        return icv_len;
    }

    std::size_t esp_build_datagram(const IpsecSaParams &sa,
                                   std::uint32_t seq,
                                   const std::uint8_t iv[16],
                                   const std::uint8_t ciphertext[16],
                                   std::uint8_t *out,
                                   std::size_t out_capacity) noexcept {
        if (iv == nullptr || ciphertext == nullptr || out == nullptr) return 0U;
        const std::size_t icv_len = static_cast<std::size_t>(sa.icv_len);
        const std::size_t total = esp_datagram_bytes(icv_len);
        if (total == 0U || out_capacity < total) return 0U;

        out[kEspSpiOffset] = static_cast<std::uint8_t>((sa.spi >> 24U) & 0xffU);
        out[kEspSpiOffset + 1U] = static_cast<std::uint8_t>((sa.spi >> 16U) & 0xffU);
        out[kEspSpiOffset + 2U] = static_cast<std::uint8_t>((sa.spi >> 8U) & 0xffU);
        out[kEspSpiOffset + 3U] = static_cast<std::uint8_t>(sa.spi & 0xffU);
        out[kEspSeqOffset] = static_cast<std::uint8_t>((seq >> 24U) & 0xffU);
        out[kEspSeqOffset + 1U] = static_cast<std::uint8_t>((seq >> 16U) & 0xffU);
        out[kEspSeqOffset + 2U] = static_cast<std::uint8_t>((seq >> 8U) & 0xffU);
        out[kEspSeqOffset + 3U] = static_cast<std::uint8_t>(seq & 0xffU);
        for (std::size_t i = 0; i < kEspIvBytes; ++i) out[kEspIvOffset + i] = iv[i];
        for (std::size_t i = 0; i < kEspBlockBytes; ++i) {
            out[kEspCiphertextOffset + i] = ciphertext[i];
        }

        const std::size_t written =
                esp_compute_icv(sa, out, out + kEspIvOffset,
                                out + kEspCiphertextOffset, out + kEspIcvOffset,
                                out_capacity - kEspIcvOffset);
        if (written != icv_len) return 0U;
        return total;
    }

    bool esp_verify_datagram(const IpsecSaParams &sa,
                             const std::uint8_t *datagram,
                             std::size_t datagram_len) noexcept {
        if (datagram == nullptr) return false;
        const std::size_t icv_len = static_cast<std::size_t>(sa.icv_len);
        const std::size_t total = esp_datagram_bytes(icv_len);
        if (total == 0U || datagram_len != total) return false;

        std::array<std::uint8_t, kEspIcvMaxBytes> expected{};
        const std::size_t written =
                esp_compute_icv(sa, datagram, datagram + kEspIvOffset,
                                datagram + kEspCiphertextOffset, expected.data(),
                                expected.size());
        if (written != icv_len) return false;

        std::uint8_t diff = 0U;
        for (std::size_t i = 0; i < icv_len; ++i) {
            diff = static_cast<std::uint8_t>(
                    diff | (expected[i] ^ datagram[kEspIcvOffset + i]));
        }
        zeroize_bytes(expected.data(), expected.size());
        return diff == 0U;
    }

    bool esp_decrypt_block(const std::uint8_t aes_key[32],
                           const std::uint8_t iv[16],
                           const std::uint8_t ciphertext[16],
                           std::uint8_t plaintext[16]) noexcept {
        return aes256_cbc_decrypt(aes_key, iv, ciphertext, kEspBlockBytes, plaintext);
    }

} // namespace ghostlock::backend::cve_2026_43284
