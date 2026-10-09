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
    using ghostlock::backend::cve_2026_43284::seam::esp_iv;
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
    const bool ok = esp_iv(cipher, key, iv);
    expect(ok, "deriving the vector from a block works");
    std::printf("esp iv  : ");
    for (std::size_t i = 0U; i < kEspBlockBytes; ++i) {
        std::printf("%02x", iv[i]);
    }
    std::printf("\n");
    std::printf("expected: 00112233445566778899aabbccddeeff\n");
    bool same = true;
    for (std::size_t i = 0U; i < kEspBlockBytes; ++i) {
        if (iv[i] != plain[i]) {
            same = false;
        }
    }
    expect(same, "decrypting the published ciphertext returns the published plaintext");
    if (g_failures != 0U) {
        std::printf("esp crypto: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("esp crypto: PASS (the vector is one ECB decryption of the old block)\n");
    return 0;
}
