/** @file tests/aes256_test.cpp
 *  AES-256 single-block ECB, pinned to the FIPS-197 Appendix C.3 vector.
 *
 *  What is pinned: the C.3 vector in both directions, and the one refusal the ECB interface can
 *  express -- a null pointer (ECB has no length parameter, so a wrong length cannot be tested
 *  here; whole-block lengths belong to the CBC primitive). The computed ciphertext is printed
 *  whether or not it matched, so a failure leaves the actual value in the log rather than only a
 *  verdict.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43284/ipsec/aes256.hpp"

namespace {
    using ghostlock::backend::cve_2026_43284::ipsec::aes256_ecb_decrypt;
    using ghostlock::backend::cve_2026_43284::ipsec::aes256_ecb_encrypt;
    using ghostlock::backend::cve_2026_43284::ipsec::kAes256BlockBytes;
    using ghostlock::backend::cve_2026_43284::ipsec::kAes256KeyBytes;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void print_hex(const char *label, const std::uint8_t *bytes, std::size_t size) {
        std::printf("%s", label);
        for (std::size_t i = 0U; i < size; ++i) {
            std::printf("%02x", static_cast<unsigned>(bytes[i]));
        }
        std::printf("\n");
    }

    bool bytes_equal(const std::uint8_t *left, const std::uint8_t *right, std::size_t size) {
        for (std::size_t i = 0U; i < size; ++i) {
            if (left[i] != right[i]) {
                return false;
            }
        }
        return true;
    }

    /** FIPS-197 Appendix C.3: key 00..1f over block 0011..eeff gives 8ea2b7ca516745bfeafc49904b496089. */
    void the_c3_vector_round_trips() {
        const std::uint8_t key[kAes256KeyBytes] = {
            0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
            0x08U, 0x09U, 0x0aU, 0x0bU, 0x0cU, 0x0dU, 0x0eU, 0x0fU,
            0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U,
            0x18U, 0x19U, 0x1aU, 0x1bU, 0x1cU, 0x1dU, 0x1eU, 0x1fU};
        const std::uint8_t block[kAes256BlockBytes] = {
            0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U,
            0x88U, 0x99U, 0xaaU, 0xbbU, 0xccU, 0xddU, 0xeeU, 0xffU};
        const std::uint8_t expected[kAes256BlockBytes] = {
            0x8eU, 0xa2U, 0xb7U, 0xcaU, 0x51U, 0x67U, 0x45U, 0xbfU,
            0xeaU, 0xfcU, 0x49U, 0x90U, 0x4bU, 0x49U, 0x60U, 0x89U};

        std::uint8_t ciphertext[kAes256BlockBytes] = {};
        const bool encrypted = aes256_ecb_encrypt(key, block, ciphertext);
        /* Printed whether or not the vector matched. */
        print_hex("aes-256: ciphertext ", ciphertext, sizeof(ciphertext));
        expect(encrypted, "the single block encrypts");
        expect(bytes_equal(ciphertext, expected, kAes256BlockBytes),
               "FIPS-197 C.3: the ciphertext is 8ea2b7ca516745bfeafc49904b496089");

        std::uint8_t decrypted[kAes256BlockBytes] = {};
        const bool decrypted_ok = aes256_ecb_decrypt(key, ciphertext, decrypted);
        print_hex("aes-256: decrypted  ", decrypted, sizeof(decrypted));
        expect(decrypted_ok, "the ciphertext decrypts");
        expect(bytes_equal(decrypted, block, kAes256BlockBytes),
               "FIPS-197 C.3 backwards: the block comes back");

        /* In place, as the header declares: the same sixteen bytes, written over themselves. */
        std::uint8_t in_place[kAes256BlockBytes] = {};
        for (std::size_t i = 0U; i < kAes256BlockBytes; ++i) {
            in_place[i] = block[i];
        }
        expect(aes256_ecb_encrypt(key, in_place, in_place), "in-place encryption runs");
        expect(bytes_equal(in_place, expected, kAes256BlockBytes),
               "in-place encryption gives the same ciphertext");
    }

    /** The one refusal this interface has: a null pointer, on any of its three names. */
    void a_null_pointer_is_refused() {
        const std::uint8_t key[kAes256KeyBytes] = {};
        const std::uint8_t block[kAes256BlockBytes] = {};
        std::uint8_t out[kAes256BlockBytes] = {};
        expect(!aes256_ecb_encrypt(nullptr, block, out), "encrypt refuses a null key");
        expect(!aes256_ecb_encrypt(key, nullptr, out), "encrypt refuses a null input");
        expect(!aes256_ecb_encrypt(key, block, nullptr), "encrypt refuses a null output");
        expect(!aes256_ecb_decrypt(nullptr, block, out), "decrypt refuses a null key");
        expect(!aes256_ecb_decrypt(key, nullptr, out), "decrypt refuses a null input");
        expect(!aes256_ecb_decrypt(key, block, nullptr), "decrypt refuses a null output");
    }
}  // namespace

int main() {
    std::printf("aes-256: FIPS-197 C.3 (single-block ECB)\n");
    the_c3_vector_round_trips();
    std::printf("aes-256: refusals\n");
    a_null_pointer_is_refused();
    if (g_failures != 0U) {
        std::printf("aes-256: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("aes-256: PASS (FIPS-197 C.3 vector, round trip, null pointers refused)\n");
    return 0;
}
