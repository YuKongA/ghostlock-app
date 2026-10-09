/** @file tests/aes256_cbc_test.cpp
 *  AES-256-CBC whole-block, pinned to the NIST SP 800-38A F.2.5 vector.
 *
 *  What is pinned: F.2.5's first block and its two-block extension, so the chaining itself is
 *  checked and not only the inverse; the decryption of both blocks back to the plaintext; the
 *  refusals this mode alone can state -- a length that is not a whole number of blocks, and each
 *  null pointer -- with the output untouched when a call is refused; zero blocks, which are a whole
 *  number of blocks and therefore accepted as a no-op; and in-place operation, as the header
 *  declares. The computed ciphertext is printed on every run, so a mismatch shows the bytes rather
 *  than only a verdict.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43284/ipsec/aes256.hpp"

namespace {
    using ghostlock::backend::cve_2026_43284::ipsec::aes256_cbc_decrypt;
    using ghostlock::backend::cve_2026_43284::ipsec::aes256_cbc_encrypt;
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

    /* NIST SP 800-38A F.2.5 (CBC-AES256.Encrypt): the key, the vector, and the first two blocks. */
    const std::uint8_t kKey[kAes256KeyBytes] = {
        0x60U, 0x3dU, 0xebU, 0x10U, 0x15U, 0xcaU, 0x71U, 0xbeU,
        0x2bU, 0x73U, 0xaeU, 0xf0U, 0x85U, 0x7dU, 0x77U, 0x81U,
        0x1fU, 0x35U, 0x2cU, 0x07U, 0x3bU, 0x61U, 0x08U, 0xd7U,
        0x2dU, 0x98U, 0x10U, 0xa3U, 0x09U, 0x14U, 0xdfU, 0xf4U};
    const std::uint8_t kIv[kAes256BlockBytes] = {
        0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0aU, 0x0bU, 0x0cU, 0x0dU, 0x0eU, 0x0fU};
    const std::uint8_t kPlain[2U * kAes256BlockBytes] = {
        0x6bU, 0xc1U, 0xbeU, 0xe2U, 0x2eU, 0x40U, 0x9fU, 0x96U,
        0xe9U, 0x3dU, 0x7eU, 0x11U, 0x73U, 0x93U, 0x17U, 0x2aU,
        0xaeU, 0x2dU, 0x8aU, 0x57U, 0x1eU, 0x03U, 0xacU, 0x9cU,
        0x9eU, 0xb7U, 0x6fU, 0xacU, 0x45U, 0xafU, 0x8eU, 0x51U};
    const std::uint8_t kCipher[2U * kAes256BlockBytes] = {
        0xf5U, 0x8cU, 0x4cU, 0x04U, 0xd6U, 0xe5U, 0xf1U, 0xbaU,
        0x77U, 0x9eU, 0xabU, 0xfbU, 0x5fU, 0x7bU, 0xfbU, 0xd6U,
        0x9cU, 0xfcU, 0x4eU, 0x96U, 0x7eU, 0xdbU, 0x80U, 0x8dU,
        0x67U, 0x9fU, 0x77U, 0x7bU, 0xc6U, 0x70U, 0x2cU, 0x7dU};

    /** F.2.5's two blocks: the first is what any single-block call must give, the second comes out
     *  right only when the chain is carried from the first. */
    void the_f25_vector_round_trips() {
        std::uint8_t one[kAes256BlockBytes] = {};
        const bool one_ok = aes256_cbc_encrypt(kKey, kIv, kPlain, kAes256BlockBytes, one);
        /* Printed whether or not the vector matched. */
        print_hex("aes-256-cbc: one block  ", one, sizeof(one));
        expect(one_ok, "one whole block encrypts");
        expect(bytes_equal(one, kCipher, kAes256BlockBytes),
               "F.2.5: the first block is f58c4c04d6e5f1ba779eabfb5f7bfbd6");

        std::uint8_t two[2U * kAes256BlockBytes] = {};
        const bool two_ok = aes256_cbc_encrypt(kKey, kIv, kPlain, sizeof(kPlain), two);
        print_hex("aes-256-cbc: two blocks ", two, sizeof(two));
        expect(two_ok, "two whole blocks encrypt");
        expect(bytes_equal(two, kCipher, sizeof(kCipher)),
               "F.2.5: the chained second block is 9cfc4e967edb808d679f777bc6702c7d");

        std::uint8_t back[2U * kAes256BlockBytes] = {};
        const bool back_ok = aes256_cbc_decrypt(kKey, kIv, two, sizeof(two), back);
        print_hex("aes-256-cbc: decrypted  ", back, sizeof(back));
        expect(back_ok, "the ciphertext decrypts");
        expect(bytes_equal(back, kPlain, sizeof(kPlain)), "back to F.2.5's plaintext");

        std::uint8_t back_one[kAes256BlockBytes] = {};
        expect(aes256_cbc_decrypt(kKey, kIv, kCipher, kAes256BlockBytes, back_one),
               "one whole block decrypts");
        expect(bytes_equal(back_one, kPlain, kAes256BlockBytes),
               "and one block comes back too");
    }

    /** The refusals this mode can state, and the one boundary it accepts. */
    void lengths_and_null_pointers() {
        std::uint8_t out[2U * kAes256BlockBytes] = {};
        for (std::size_t i = 0U; i < sizeof(out); ++i) {
            out[i] = 0xa5U;
        }
        expect(!aes256_cbc_encrypt(kKey, kIv, kPlain, 15U, out), "15 bytes is not a whole block");
        expect(!aes256_cbc_encrypt(kKey, kIv, kPlain, 17U, out), "17 bytes is not a whole block");
        expect(!aes256_cbc_encrypt(kKey, kIv, kPlain, 1U, out), "one byte is not a whole block");
        expect(!aes256_cbc_decrypt(kKey, kIv, kCipher, 31U, out), "31 bytes is not a whole block");
        bool sentinel = true;
        for (std::size_t i = 0U; i < sizeof(out); ++i) {
            sentinel = sentinel && out[i] == 0xa5U;
        }
        expect(sentinel, "a refused length writes no byte");

        expect(!aes256_cbc_encrypt(nullptr, kIv, kPlain, sizeof(kPlain), out), "a null key is refused");
        expect(!aes256_cbc_encrypt(kKey, nullptr, kPlain, sizeof(kPlain), out), "a null iv is refused");
        expect(!aes256_cbc_encrypt(kKey, kIv, nullptr, sizeof(kPlain), out), "a null input is refused");
        expect(!aes256_cbc_encrypt(kKey, kIv, kPlain, sizeof(kPlain), nullptr), "a null output is refused");
        expect(!aes256_cbc_decrypt(nullptr, kIv, kCipher, sizeof(kCipher), out), "decrypt refuses a null key");
        expect(!aes256_cbc_decrypt(kKey, nullptr, kCipher, sizeof(kCipher), out), "decrypt refuses a null iv");
        expect(!aes256_cbc_decrypt(kKey, kIv, nullptr, sizeof(kCipher), out), "decrypt refuses a null input");
        expect(!aes256_cbc_decrypt(kKey, kIv, kCipher, sizeof(kCipher), nullptr), "decrypt refuses a null output");

        /* Zero blocks is a whole number of blocks, so it is accepted as a no-op. */
        std::uint8_t nothing[1U] = {0xa5U};
        expect(aes256_cbc_encrypt(kKey, kIv, nullptr, 0U, nothing),
               "zero blocks is accepted (nothing to do)");
        expect(nothing[0] == 0xa5U, "and it writes nothing");
    }

    /** In place, as the header declares. */
    void in_place() {
        std::uint8_t buffer[2U * kAes256BlockBytes] = {};
        for (std::size_t i = 0U; i < sizeof(buffer); ++i) {
            buffer[i] = kPlain[i];
        }
        expect(aes256_cbc_encrypt(kKey, kIv, buffer, sizeof(buffer), buffer), "in-place encryption runs");
        expect(bytes_equal(buffer, kCipher, sizeof(kCipher)), "and lands on the same two blocks");
        expect(aes256_cbc_decrypt(kKey, kIv, buffer, sizeof(buffer), buffer), "in-place decryption runs");
        expect(bytes_equal(buffer, kPlain, sizeof(kPlain)), "and comes back");
    }
}  // namespace

int main() {
    std::printf("aes-256-cbc: NIST SP 800-38A F.2.5 (two blocks, chained)\n");
    the_f25_vector_round_trips();
    std::printf("aes-256-cbc: lengths and null pointers\n");
    lengths_and_null_pointers();
    std::printf("aes-256-cbc: in place\n");
    in_place();
    if (g_failures != 0U) {
        std::printf("aes-256-cbc: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("aes-256-cbc: PASS (F.2.5 vector, chain, round trip, refusals)\n");
    return 0;
}
