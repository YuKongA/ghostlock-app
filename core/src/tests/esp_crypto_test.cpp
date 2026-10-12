/** @file tests/esp_crypto_test.cpp
 *  The vector the write derives, against the published AES vector.
 *
 *  The initialisation vector is one ECB decryption of the block being replaced, so the FIPS-197 C.3
 *  pair is exactly the right fixture: decrypting the published ciphertext with the published key
 *  must give the published plaintext. That makes this test and the AES test two views of one
 *  operation, which is what a caller needs to know: feeding the wrong block gives a wrong vector,
 *  and a wrong vector is a datagram the kernel will not accept.
 */
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43284/seam/esp_crypto.hpp"

namespace {
    using ghostlock::backend::cve_2026_43284::seam::esp_cbc_iv;
    using ghostlock::backend::cve_2026_43284::seam::kEspBlockBytes;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** The published pair, as bytes. */
    void c3_plain(uint8_t *out) {
        const std::uint8_t p[kEspBlockBytes] = {0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U,
                                                0x88U, 0x99U, 0xaaU, 0xbbU, 0xccU, 0xddU, 0xeeU, 0xffU};
        for (std::size_t i = 0U; i < kEspBlockBytes; ++i) {
            out[i] = p[i];
        }
    }
    void c3_cipher(uint8_t *out) {
        const std::uint8_t c[kEspBlockBytes] = {0x8eU, 0xa2U, 0xb7U, 0xcaU, 0x51U, 0x67U, 0x45U, 0xbfU,
                                                0xeaU, 0xfcU, 0x49U, 0x90U, 0x4bU, 0x49U, 0x60U, 0x89U};
        for (std::size_t i = 0U; i < kEspBlockBytes; ++i) {
            out[i] = c[i];
        }
    }
    void c3_key(uint8_t *out) {
        for (std::size_t i = 0U; i < 32U; ++i) {
            out[i] = static_cast<std::uint8_t>(i);
        }
    }
}  // namespace

int main() {
    std::uint8_t key[32] = {};
    std::uint8_t plain[kEspBlockBytes] = {};
    std::uint8_t cipher[kEspBlockBytes] = {};
    c3_key(key);
    c3_plain(plain);
    c3_cipher(cipher);
    std::uint8_t iv[kEspBlockBytes] = {};
    /* W-what the primitive is for: the page becomes D_K(old) xor IV, so a caller that wants
     * `desired` there must get a vector of D_K(old) xor desired. Asking for the plaintext of the
     * published ciphertext therefore has to give a vector of zeros -- and asking for its complement
     * has to give the complement of the plaintext. A vector that ignored the desired block would
     * pass the first only by accident and fail the second. */
    bool ok = esp_cbc_iv(cipher, plain, key, iv);
    expect(ok, "deriving the vector works");
    bool zero_vector = true;
    for (std::size_t i = 0U; i < kEspBlockBytes; ++i) {
        if (iv[i] != 0U) {
            zero_vector = false;
        }
    }
    expect(zero_vector, "a write that wants the block's own plaintext needs no vector change");
    std::uint8_t inverted[kEspBlockBytes] = {};
    for (std::size_t i = 0U; i < kEspBlockBytes; ++i) {
        inverted[i] = static_cast<std::uint8_t>(plain[i] ^ 0xFFU);
    }
    ok = esp_cbc_iv(cipher, inverted, key, iv);
    expect(ok, "and deriving it for another block works");
    /* D_K(cipher) = plain, so the vector is plain xor inverted = plain xor (plain xor 0xFF) = 0xFF,
     * whatever the plaintext was. A derivation that ignored the desired block would return plain. */
    bool complement = true;
    for (std::size_t i = 0U; i < kEspBlockBytes; ++i) {
        if (iv[i] != 0xFFU) {
            complement = false;
        }
    }
    expect(complement, "the vector carries the desired block, not only the old one");
    std::printf("esp iv (desired = old plaintext): ");
    for (std::size_t i = 0U; i < kEspBlockBytes; ++i) {
        std::printf("%02x", iv[i]);
    }
    std::printf("\n");
    if (g_failures != 0U) {
        std::printf("esp crypto: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("esp crypto: PASS (the vector is D_K(old) xor the desired block)\n");
    return 0;
}
