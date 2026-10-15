/** @file tests/esp_icv_test.cpp
 *  The integrity check, against a MAC computed independently.
 *
 *  The frame carries a length and the MAC is thirty-two bytes, so the operation is a truncation of
 *  a known value; the fixture is a full MAC for a thirty-two byte key and the greeting that the
 *  published vectors use, computed with OpenSSL rather than copied from this code. The refusals
 *  matter as much as the value: a length of zero is not a request for no check, a length longer
 *  than the MAC is not padded, and a buffer too small is refused rather than written past.
 */
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43284/landing/esp_crypto.hpp"

namespace {
    using ghostlock::backend::cve_2026_43284::landing::esp_icv;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    const std::uint8_t kKey[32] = {0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU,
                                   0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU,
                                   0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU,
                                   0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU};
    const std::uint8_t kMessage[8] = {0x48U, 0x69U, 0x20U, 0x54U, 0x68U, 0x65U, 0x72U, 0x65U};
    /* openssl dgst -sha256 -mac HMAC -macopt hexkey:0b0b... -binary, over the greeting. */
    const std::uint8_t kFull[32] = {0x19U, 0x8aU, 0x60U, 0x7eU, 0xb4U, 0x4bU, 0xfbU, 0xc6U,
                                    0x99U, 0x03U, 0xa0U, 0xf1U, 0xcfU, 0x2bU, 0xbdU, 0xc5U,
                                    0xbaU, 0x0aU, 0xa3U, 0xf3U, 0xd9U, 0xaeU, 0x3cU, 0x1cU,
                                    0x7aU, 0x3bU, 0x16U, 0x96U, 0xa0U, 0xb6U, 0x8cU, 0xf7U};

    void the_full_mac_matches() {
        std::uint8_t out[32] = {};
        expect(esp_icv(kMessage, sizeof(kMessage), kKey, 32U, out, sizeof(out)),
               "a full length check works");
        bool same = true;
        for (std::size_t i = 0U; i < 32U; ++i) {
            if (out[i] != kFull[i]) {
                same = false;
            }
        }
        std::printf("esp icv : ");
        for (std::size_t i = 0U; i < 32U; ++i) {
            std::printf("%02x", out[i]);
        }
        std::printf("\n");
        expect(same, "and equals the MAC computed with OpenSSL");
    }

    void a_truncated_check_is_a_prefix() {
        std::uint8_t out[32] = {};
        expect(esp_icv(kMessage, sizeof(kMessage), kKey, 12U, out, sizeof(out)),
               "a shorter check is allowed");
        bool prefix = true;
        for (std::size_t i = 0U; i < 12U; ++i) {
            if (out[i] != kFull[i]) {
                prefix = false;
            }
        }
        expect(prefix, "and is the first bytes of the full MAC, not a separate computation");
    }

    void the_refusals_are_named() {
        std::uint8_t out[32] = {};
        expect(!esp_icv(kMessage, sizeof(kMessage), kKey, 0U, out, sizeof(out)),
               "a length of zero is refused, not read as asking for no check");
        expect(!esp_icv(kMessage, sizeof(kMessage), kKey, 33U, out, sizeof(out)),
               "a length longer than the MAC is refused, not padded");
        std::uint8_t small[8] = {};
        expect(!esp_icv(kMessage, sizeof(kMessage), kKey, 32U, small, sizeof(small)),
               "a buffer too small is refused rather than written past");
        expect(!esp_icv(nullptr, sizeof(kMessage), kKey, 32U, out, sizeof(out)),
               "and a null datagram is refused");
    }
}  // namespace

int main() {
    std::printf("esp icv: the full check\n");
    the_full_mac_matches();
    std::printf("esp icv: truncation\n");
    a_truncated_check_is_a_prefix();
    std::printf("esp icv: refusals\n");
    the_refusals_are_named();
    if (g_failures != 0U) {
        std::printf("esp icv: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("esp icv: PASS (full MAC, truncation is a prefix, refusals named)\n");
    return 0;
}
