/** @file tests/terminal_payload_test.cpp
 *  A6-4 evidence: the digest and the payload check that guards segment three.
 *
 *  Sections: 1. SHA-256 against the published vectors (empty, abc, and the
 *  million-a string, which crosses the block boundary); 2. the hex form and its
 *  well-formedness; 3. a declared payload is accepted only when its bytes match
 *  what was declared.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "support/sha256.hpp"
#include "terminal/payload.hpp"

namespace {
    using ghostlock::support::kSha256HexBytes;
    using ghostlock::support::sha256;
    using ghostlock::support::sha256_hex;
    using ghostlock::support::sha256_matches_text;
    using ghostlock::support::sha256_text_is_well_formed;
    using ghostlock::support::Sha256Digest;
    using ghostlock::terminal::check_payload;
    using ghostlock::terminal::Payload;
    using ghostlock::terminal::PayloadVerdict;
    using ghostlock::terminal::payload_verdict_name;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void expect_hex(const std::uint8_t *data, std::size_t size, const char *wanted,
                    const char *what) {
        char hex[kSha256HexBytes];
        sha256_hex(sha256(data, size), hex);
        if (std::strcmp(hex, wanted) != 0) {
            std::printf("FAIL: %s\n  got %s\n  want %s\n", what, hex, wanted);
            ++g_failures;
        }
    }

    /* ---- 1: the published vectors ---- */
    void vectors_hold() {
        expect_hex(nullptr, 0U,
                   "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                   "the digest of nothing is the published one");
        const std::uint8_t abc[] = {'a', 'b', 'c'};
        expect_hex(abc, 3U,
                   "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                   "the digest of abc is the published one");
        /* The million-a case: 15625 blocks plus a tail, so it exercises both the
         * padding boundary and the length field. */
        static std::uint8_t million[1000000U];
        for (std::size_t i = 0U; i < sizeof(million); ++i) {
            million[i] = static_cast<std::uint8_t>('a');
        }
        expect_hex(million, sizeof(million),
                   "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
                   "the digest of a million a is the published one");
        /* One byte different must change the digest. */
        std::uint8_t other[3] = {'a', 'b', 'd'};
        expect(sha256(abc, 3U).bytes[0] != sha256(other, 3U).bytes[0],
               "a one-byte change moves the digest");
    }

    /* ---- 2: the text form ---- */
    void text_form_is_checked() {
        char hex[kSha256HexBytes];
        const std::uint8_t abc[] = {'a', 'b', 'c'};
        const Sha256Digest digest = sha256(abc, 3U);
        sha256_hex(digest, hex);
        expect(std::strlen(hex) == 64U, "hex is sixty-four characters");
        expect(sha256_text_is_well_formed(hex), "the produced text is well formed");
        expect(sha256_matches_text(digest, hex), "the digest matches its own text");
        char upper[kSha256HexBytes];
        for (std::size_t i = 0U; i < kSha256HexBytes; ++i) {
            upper[i] = hex[i];
        }
        upper[0] = 'B';
        expect(!sha256_text_is_well_formed(upper), "uppercase hex is not accepted");
        char short_text[kSha256HexBytes];
        for (std::size_t i = 0U; i < kSha256HexBytes; ++i) {
            short_text[i] = hex[i];
        }
        short_text[10] = '\0';
        expect(!sha256_text_is_well_formed(short_text), "a short text is not accepted");
        expect(!sha256_text_is_well_formed(nullptr), "no text is not accepted");
    }

    /* ---- 3: the payload is what it says it is ---- */
    void payloads_are_checked() {
        const std::uint8_t bytes[] = {1U, 2U, 3U, 4U};
        char declared[kSha256HexBytes];
        sha256_hex(sha256(bytes, sizeof(bytes)), declared);

        Payload good{};
        good.bytes = bytes;
        good.size = sizeof(bytes);
        good.declared_sha256 = declared;
        good.declared_size = static_cast<std::int64_t>(sizeof(bytes));
        expect(check_payload(good) == PayloadVerdict::Ok, "a matching payload is accepted");

        Payload wrong_size = good;
        wrong_size.declared_size = 99;
        expect(check_payload(wrong_size) == PayloadVerdict::SizeMismatch,
               "a size that disagrees with the bytes is refused");
        expect(std::string_view(payload_verdict_name(PayloadVerdict::SizeMismatch)) ==
                   std::string_view("payload-missing"),
               "the refusal is named payload-missing");

        /* A digest that differs in its last character is still a different payload. */
        char tampered[kSha256HexBytes];
        for (std::size_t i = 0U; i < kSha256HexBytes; ++i) {
            tampered[i] = declared[i];
        }
        tampered[63] = declared[63] == '0' ? '1' : '0';
        Payload wrong_digest = good;
        wrong_digest.declared_sha256 = tampered;
        expect(check_payload(wrong_digest) == PayloadVerdict::DigestMismatch,
               "one changed hex character is refused");

        Payload no_bytes = good;
        no_bytes.bytes = nullptr;
        expect(check_payload(no_bytes) == PayloadVerdict::Missing,
               "a declared payload with no bytes is refused");
        Payload no_digest = good;
        no_digest.declared_sha256 = nullptr;
        expect(check_payload(no_digest) == PayloadVerdict::Missing,
               "a payload with no declared digest is refused");
        Payload undeclared_size = good;
        undeclared_size.declared_size = -1;
        expect(check_payload(undeclared_size) == PayloadVerdict::Ok,
               "an absent size is allowed: the digest still binds the bytes");
    }
}  // namespace

int main() {
    std::printf("payload: digest vectors\n");
    vectors_hold();
    std::printf("payload: digest text\n");
    text_form_is_checked();
    std::printf("payload: declared payloads\n");
    payloads_are_checked();
    if (g_failures != 0U) {
        std::printf("payload: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("payload: PASS (vectors + text form + declared payload checks)\n");
    return 0;
}
