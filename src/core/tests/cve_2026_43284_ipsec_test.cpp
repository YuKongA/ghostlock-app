/* Host KAT for B5-2: the CVE-2026-43284 IpSec/ESP primitives.
 *
 * Covers AES-256 ECB (FIPS-197 C.3) and CBC (NIST SP 800-38A F.2.5),
 * HMAC-SHA256 (RFC 4231 cases 1-4/6/7 plus an empty-data vector), the CBC IV
 * identity iv = AES_ECB_DEC(K, old) XOR desired, and the ESP-in-UDP model:
 * datagram layout, ICV truncation to the frame's icv_len, constant-time tamper
 * detection and the size/argument rejection paths.
 *
 * No device, kernel or syscall dependency: the test links the same
 * translation units the device build uses. */

#include "backend/cve_2026_43284/ipsec/aes256.hpp"
#include "backend/cve_2026_43284/ipsec/hmac_sha256.hpp"
#include "backend/cve_2026_43284/ipsec/ipsec.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
    using ghostlock::backend::cve_2026_43284::IpsecSaParams;
    using ghostlock::backend::cve_2026_43284::aes256_cbc_decrypt;
    using ghostlock::backend::cve_2026_43284::aes256_cbc_encrypt;
    using ghostlock::backend::cve_2026_43284::aes256_ecb_decrypt;
    using ghostlock::backend::cve_2026_43284::aes256_ecb_encrypt;
    using ghostlock::backend::cve_2026_43284::compute_cbc_iv;
    using ghostlock::backend::cve_2026_43284::esp_build_datagram;
    using ghostlock::backend::cve_2026_43284::esp_compute_icv;
    using ghostlock::backend::cve_2026_43284::esp_datagram_bytes;
    using ghostlock::backend::cve_2026_43284::esp_decrypt_block;
    using ghostlock::backend::cve_2026_43284::esp_verify_datagram;
    using ghostlock::backend::cve_2026_43284::hmac_sha256;
    using ghostlock::backend::cve_2026_43284::kEspDatagramBytes;
    using ghostlock::backend::cve_2026_43284::zeroize_bytes;

    std::uint8_t nibble(char c) {
        if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
        return 0U;
    }

    template <std::size_t N>
    std::array<std::uint8_t, N> from_hex(const char *hex) {
        std::array<std::uint8_t, N> out{};
        for (std::size_t i = 0; i < N; ++i) {
            out[i] = static_cast<std::uint8_t>(
                    (nibble(hex[2U * i]) << 4U) | nibble(hex[2U * i + 1U]));
        }
        return out;
    }

    std::vector<std::uint8_t> bytes(const char *text) {
        return std::vector<std::uint8_t>(text, text + std::strlen(text));
    }

    std::vector<std::uint8_t> repeat(std::uint8_t value, std::size_t count) {
        return std::vector<std::uint8_t>(count, value);
    }

    std::string to_hex(const std::uint8_t *data, std::size_t len) {
        static const char *const digits = "0123456789abcdef";
        std::string out;
        out.reserve(len * 2U);
        for (std::size_t i = 0; i < len; ++i) {
            out.push_back(digits[(data[i] >> 4U) & 0x0fU]);
            out.push_back(digits[data[i] & 0x0fU]);
        }
        return out;
    }

    std::string hmac_hex(const std::vector<std::uint8_t> &key,
                         const std::vector<std::uint8_t> &msg) {
        std::array<std::uint8_t, 32> mac{};
        hmac_sha256(key.data(), key.size(), msg.data(), msg.size(), mac.data());
        return to_hex(mac.data(), mac.size());
    }

    IpsecSaParams sample_sa(std::uint8_t icv_len) {
        IpsecSaParams sa;
        sa.spi = 0x01020304U;
        sa.encap_port = 0x0506U;
        sa.sender_port = 0x0708U;
        sa.icv_len = icv_len;
        for (std::size_t i = 0; i < sa.aes_key.size(); ++i) {
            sa.aes_key[i] = static_cast<std::uint8_t>(i);
            sa.hmac_key[i] = static_cast<std::uint8_t>(0x20U + i);
        }
        return sa;
    }
} // namespace

int main() {
    /* ---- AES-256 ECB, FIPS-197 C.3. ---- */
    {
        const auto key = from_hex<32>(
                "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
        const auto plain = from_hex<16>("00112233445566778899aabbccddeeff");
        const auto expect = from_hex<16>("8ea2b7ca516745bfeafc49904b496089");
        std::array<std::uint8_t, 16> out{};

        assert(aes256_ecb_encrypt(key.data(), plain.data(), out.data()));
        assert(out == expect);
        assert(aes256_ecb_decrypt(key.data(), expect.data(), out.data()));
        assert(out == plain);

        assert(!aes256_ecb_encrypt(nullptr, plain.data(), out.data()));
        assert(!aes256_ecb_decrypt(key.data(), nullptr, out.data()));
        assert(!aes256_ecb_encrypt(key.data(), plain.data(), nullptr));
    }

    /* ---- AES-256 CBC, NIST SP 800-38A F.2.5. ---- */
    {
        const auto key = from_hex<32>(
                "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4");
        const auto iv = from_hex<16>("000102030405060708090a0b0c0d0e0f");
        const auto plain = from_hex<64>(
                "6bc1bee22e409f96e93d7e117393172a"
                "ae2d8a571e03ac9c9eb76fac45af8e51"
                "30c81c46a35ce411e5fbc1191a0a52ef"
                "f69f2445df4f9b17ad2b417be66c3710");
        const auto expect = from_hex<64>(
                "f58c4c04d6e5f1ba779eabfb5f7bfbd6"
                "9cfc4e967edb808d679f777bc6702c7d"
                "39f23369a9d9bacfa530e26304231461"
                "b2eb05e2c39be9fcda6c19078c6a9d1b");
        std::array<std::uint8_t, 64> out{};

        assert(aes256_cbc_encrypt(key.data(), iv.data(), plain.data(), plain.size(),
                                  out.data()));
        assert(out == expect);
        assert(aes256_cbc_decrypt(key.data(), iv.data(), out.data(), out.size(),
                                  out.data()));  /* in place */
        assert(out == plain);

        /* A non-block length and null pointers are rejected. */
        assert(!aes256_cbc_encrypt(key.data(), iv.data(), plain.data(), 15U, out.data()));
        assert(!aes256_cbc_decrypt(key.data(), iv.data(), plain.data(), 4U, out.data()));
        assert(!aes256_cbc_encrypt(nullptr, iv.data(), plain.data(), plain.size(),
                                   out.data()));
        assert(!aes256_cbc_decrypt(key.data(), nullptr, plain.data(), plain.size(),
                                   out.data()));
    }

    /* ---- HMAC-SHA256, RFC 4231. ---- */
    {
        /* Test Case 1: key 0x0b x20. */
        assert(hmac_hex(repeat(0x0bU, 20U), bytes("Hi There")) ==
               "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
        /* Test Case 2: short key. */
        assert(hmac_hex(bytes("Jefe"), bytes("what do ya want for nothing?")) ==
               "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
        /* Test Case 3: key 0xaa x20, data 0xdd x50. */
        assert(hmac_hex(repeat(0xaaU, 20U), repeat(0xddU, 50U)) ==
               "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe");
        /* Test Case 4: 25-byte key. */
        std::vector<std::uint8_t> key25(25U);
        for (std::size_t i = 0; i < key25.size(); ++i) {
            key25[i] = static_cast<std::uint8_t>(i + 1U);
        }
        assert(hmac_hex(key25, repeat(0xcdU, 50U)) ==
               "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b");
        /* Test Case 6: key 0xaa x131 (longer than the 64-byte block). */
        assert(hmac_hex(repeat(0xaaU, 131U),
                        bytes("Test Using Larger Than Block-Size Key - Hash Key First")) ==
               "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
        /* Test Case 7: long key and long data. */
        assert(hmac_hex(repeat(0xaaU, 131U),
                        bytes("This is a test using a larger than block-size key and a "
                              "larger than block-size data. The key needs to be hashed "
                              "before being used by the HMAC algorithm.")) ==
               "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2");
        /* Empty data (computed independently with Python hashlib/hmac). */
        assert(hmac_hex(repeat(0x0bU, 20U), std::vector<std::uint8_t>{}) ==
               "999a901219f032cd497cadb5e6051e97b6a29ab297bd6ae722bd6062a2f59542");
        std::array<std::uint8_t, 32> mac{};
        std::array<std::uint8_t, 32> mac_null{};
        const auto key = repeat(0x0bU, 20U);
        hmac_sha256(key.data(), key.size(), nullptr, 0U, mac.data());
        hmac_sha256(key.data(), key.size(), mac.data(), 0U, mac_null.data());
        assert(mac == mac_null);
    }

    /* ---- CBC IV identity: iv = AES_ECB_DEC(K, old) XOR desired. ---- */
    {
        const auto key = from_hex<32>(
                "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
        const auto old_content = from_hex<16>("00112233445566778899aabbccddeeff");
        const auto desired = from_hex<16>("deadbeefcafebabe0123456789abcdef");

        std::array<std::uint8_t, 16> iv{};
        std::array<std::uint8_t, 16> recovered{};
        compute_cbc_iv(old_content.data(), desired.data(), key.data(), iv.data());
        assert(esp_decrypt_block(key.data(), iv.data(), old_content.data(),
                                 recovered.data()));
        assert(recovered == desired);

        /* The same identity through the raw CBC primitive. */
        std::array<std::uint8_t, 16> raw{};
        assert(aes256_cbc_decrypt(key.data(), iv.data(), old_content.data(), 16U,
                                  raw.data()));
        assert(raw == desired);

        /* A different desired block yields a different IV. */
        std::array<std::uint8_t, 16> desired2 = desired;
        desired2[0] = static_cast<std::uint8_t>(desired2[0] ^ 0x01U);
        std::array<std::uint8_t, 16> iv2{};
        compute_cbc_iv(old_content.data(), desired2.data(), key.data(), iv2.data());
        assert(iv != iv2);
        assert(esp_decrypt_block(key.data(), iv2.data(), old_content.data(),
                                 recovered.data()));
        assert(recovered == desired2);

        /* old == desired is a valid no-op write. */
        std::array<std::uint8_t, 16> noop_iv{};
        compute_cbc_iv(old_content.data(), old_content.data(), key.data(),
                       noop_iv.data());
        assert(esp_decrypt_block(key.data(), noop_iv.data(), old_content.data(),
                                 recovered.data()));
        assert(recovered == old_content);
    }

    /* ---- ESP-in-UDP datagram, ICV truncation and tamper detection. ---- */
    {
        const auto iv = from_hex<16>("1a1b1c1d1e1f20212223242526272829");
        const auto ciphertext = from_hex<16>("00112233445566778899aabbccddeeff");

        IpsecSaParams sa = sample_sa(12U);
        std::array<std::uint8_t, 64> datagram{};
        std::size_t len = esp_build_datagram(sa, 0x0a0b0c0dU, iv.data(),
                                             ciphertext.data(), datagram.data(),
                                             datagram.size());
        assert(len == 52U);
        assert(len == esp_datagram_bytes(12U));

        /* SPI and seq are big-endian; IV and ciphertext are verbatim. */
        assert(datagram[0] == 0x01U && datagram[1] == 0x02U && datagram[2] == 0x03U &&
               datagram[3] == 0x04U);
        assert(datagram[4] == 0x0aU && datagram[5] == 0x0bU && datagram[6] == 0x0cU &&
               datagram[7] == 0x0dU);
        for (std::size_t i = 0; i < iv.size(); ++i) {
            assert(datagram[8U + i] == iv[i]);
        }
        for (std::size_t i = 0; i < ciphertext.size(); ++i) {
            assert(datagram[24U + i] == ciphertext[i]);
        }

        /* The 12-byte ICV is the first 12 bytes of the full HMAC. */
        std::array<std::uint8_t, 40> mac_input{};
        std::memcpy(mac_input.data(), datagram.data(), mac_input.size());
        std::array<std::uint8_t, 32> mac{};
        hmac_sha256(sa.hmac_key.data(), sa.hmac_key.size(), mac_input.data(),
                    mac_input.size(), mac.data());
        for (std::size_t i = 0; i < 12U; ++i) {
            assert(datagram[40U + i] == mac[i]);
        }
        assert(esp_verify_datagram(sa, datagram.data(), len));

        /* Flipping any authenticated byte or the ICV itself fails closed. */
        for (std::size_t pos : {0U, 4U, 8U, 24U, 40U}) {
            auto bad = datagram;
            bad[pos] = static_cast<std::uint8_t>(bad[pos] ^ 0x01U);
            assert(!esp_verify_datagram(sa, bad.data(), len));
        }
        assert(!esp_verify_datagram(sa, datagram.data(), len - 1U));
        assert(!esp_verify_datagram(sa, nullptr, len));
        {
            IpsecSaParams wrong_key = sa;
            wrong_key.hmac_key[0] =
                    static_cast<std::uint8_t>(wrong_key.hmac_key[0] ^ 0x01U);
            assert(!esp_verify_datagram(wrong_key, datagram.data(), len));
        }

        /* Full 32-byte ICV. */
        sa.icv_len = 32U;
        std::array<std::uint8_t, 80> full{};
        len = esp_build_datagram(sa, 1U, iv.data(), ciphertext.data(), full.data(),
                                 full.size());
        assert(len == kEspDatagramBytes + 16U);
        assert(len == esp_datagram_bytes(32U));
        assert(esp_verify_datagram(sa, full.data(), len));
        full[40U] = static_cast<std::uint8_t>(full[40U] ^ 0x80U);
        assert(!esp_verify_datagram(sa, full.data(), len));

        /* Default 16-byte ICV matches the frozen kEspDatagramBytes. */
        sa.icv_len = 16U;
        std::array<std::uint8_t, 64> default_icv{};
        len = esp_build_datagram(sa, 2U, iv.data(), ciphertext.data(),
                                 default_icv.data(), default_icv.size());
        assert(len == kEspDatagramBytes);
        assert(esp_verify_datagram(sa, default_icv.data(), len));

        /* Out-of-range ICV lengths are rejected everywhere. */
        sa.icv_len = 0U;
        assert(esp_datagram_bytes(0U) == 0U);
        assert(esp_build_datagram(sa, 3U, iv.data(), ciphertext.data(),
                                  default_icv.data(), default_icv.size()) == 0U);
        assert(!esp_verify_datagram(sa, default_icv.data(), default_icv.size()));
        sa.icv_len = 33U;
        assert(esp_datagram_bytes(33U) == 0U);
        assert(esp_build_datagram(sa, 3U, iv.data(), ciphertext.data(),
                                  default_icv.data(), default_icv.size()) == 0U);

        /* Too-small buffers and null arguments. */
        sa.icv_len = 16U;
        assert(esp_build_datagram(sa, 4U, iv.data(), ciphertext.data(),
                                  default_icv.data(), kEspDatagramBytes - 1U) == 0U);
        assert(esp_build_datagram(sa, 4U, nullptr, ciphertext.data(),
                                  default_icv.data(), default_icv.size()) == 0U);
        assert(esp_build_datagram(sa, 4U, iv.data(), ciphertext.data(), nullptr,
                                  default_icv.size()) == 0U);
        assert(esp_compute_icv(sa, default_icv.data(), default_icv.data() + 8U,
                               default_icv.data() + 24U, mac.data(), 15U) == 0U);
        assert(esp_compute_icv(sa, nullptr, default_icv.data() + 8U,
                               default_icv.data() + 24U, mac.data(), mac.size()) == 0U);

        /* zeroize_bytes clears a whole range. */
        std::array<std::uint8_t, 16> wipe{};
        for (std::size_t i = 0; i < wipe.size(); ++i) {
            wipe[i] = static_cast<std::uint8_t>(i + 1U);
        }
        zeroize_bytes(wipe.data(), wipe.size());
        for (std::size_t i = 0; i < wipe.size(); ++i) assert(wipe[i] == 0U);
    }

    std::puts("cve_2026_43284_ipsec_test: OK");
    return 0;
}
