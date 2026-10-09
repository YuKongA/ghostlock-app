/** @file tests/real_document_test.cpp
 *  Read a document this side did not write.
 *
 *  Every other test on the profile path builds its own input, which verifies the
 *  assumption the reader was written with rather than the contract. This one
 *  reads bytes produced by the other end and pinned there as a fixture: the file
 *  is compared against, never rewritten, so the reader is measured against real
 *  output. A drift in the fixture fails here before it can reach a device.
 *
 *  Path: the other end owns the file; this side only reads it. Neither side
 *  copies it, so the two cannot drift apart while both look correct.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "profile/glkv3_document.hpp"
#include "support/sha256.hpp"

#ifndef REAL_DOCUMENT_HEX
#define REAL_DOCUMENT_HEX "../profile-core/src/test/resources/payload-document-fixture.hex"
#endif

namespace {
    using ghostlock::profile::Document;
    using ghostlock::profile::DocumentVerdict;
    using ghostlock::profile::PayloadEntry;
    using ghostlock::support::sha256;
    using ghostlock::support::sha256_hex;
    using ghostlock::support::sha256_matches_text;

    /* The pinned identity of the fixture: the document itself, and the digest
     * the other end declared for the payload inside it. */
    constexpr const char *kDocumentDigest =
        "d8d7fb574530bb3d9a84b0ab642107f7cb900afc02b7f7c5f69a7bd34f31f09a";
    constexpr const char *kPayloadDigest =
        "527ed4ff9a874faa7d4f00be7b01cbc7d63659fafcad97dddffa2edcb84d0f94";
    constexpr const char *kBackend = "cve_2026_43499";
    constexpr const char *kName = "root_child_script";

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    int hex_digit(char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        return -1;
    }

    /** Read the fixture and decode it; returns the length, or 0 on any trouble. */
    std::size_t load(std::uint8_t *out, std::size_t capacity) {
        std::FILE *file = std::fopen(REAL_DOCUMENT_HEX, "rb");
        if (file == nullptr) {
            std::printf("FAIL: cannot read the fixture at %s\n", REAL_DOCUMENT_HEX);
            ++g_failures;
            return 0U;
        }
        char text[8192];
        const std::size_t got = std::fread(text, 1U, sizeof(text), file);
        std::fclose(file);
        std::size_t digits = 0U;
        while (digits < got && hex_digit(text[digits]) >= 0) {
            ++digits;
        }
        if (digits == 0U || digits % 2U != 0U || digits / 2U > capacity) {
            std::printf("FAIL: the fixture is not an even run of hex digits\n");
            ++g_failures;
            return 0U;
        }
        for (std::size_t i = 0U; i < digits / 2U; ++i) {
            const int high = hex_digit(text[i * 2U]);
            const int low = hex_digit(text[i * 2U + 1U]);
            out[i] = static_cast<std::uint8_t>((high << 4) | low);
        }
        return digits / 2U;
    }
}  // namespace

int main() {
    static std::uint8_t bytes[4096U];
    const std::size_t size = load(bytes, sizeof(bytes));
    if (size == 0U) {
        std::printf("real document: FAIL (%zu)\n", g_failures);
        return 1;
    }
    char hex[65];
    sha256_hex(sha256(bytes, size), hex);
    expect(std::strcmp(hex, kDocumentDigest) == 0,
           "the fixture is the document that was pinned, byte for byte");

    Document document;
    expect(document.parse(bytes, size) == DocumentVerdict::Ok,
           "a document written by the other end parses here");
    expect(document.schema() == 3U, "and declares the one schema this build reads");

    PayloadEntry entry;
    const DocumentVerdict found =
        document.payload(std::string_view(kBackend), std::string_view(kName), entry);
    expect(found == DocumentVerdict::Ok, "its payload is found at the dotted segment");
    expect(entry.size > 0U && entry.bytes != nullptr, "and carries bytes");
    expect(std::strcmp(entry.kind, "script") == 0, "of the kind it declared");
    if (entry.bytes != nullptr && entry.size > 0U) {
        char payload_hex[65];
        sha256_hex(sha256(entry.bytes, entry.size), payload_hex);
        expect(std::strcmp(payload_hex, kPayloadDigest) == 0,
               "the bytes are the payload that was pinned");
        expect(sha256_matches_text(sha256(entry.bytes, entry.size), entry.sha256),
               "and the digest the document declared is the digest of those bytes");
    }
    /* A name the rule admits but the document does not carry is simply absent. */
    PayloadEntry other;
    expect(document.payload(std::string_view(kBackend), std::string_view("root_child_module"), other) ==
                   DocumentVerdict::Missing,
           "a payload the document does not carry is refused, not invented");
    document.reset();
    if (g_failures != 0U) {
        std::printf("real document: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("real document: PASS (%zu bytes from the other end, payload %s)\n", size,
                kName);
    return 0;
}
