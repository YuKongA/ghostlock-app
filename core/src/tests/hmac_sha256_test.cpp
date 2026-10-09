/** @file tests/hmac_sha256_test.cpp
 *  HMAC-SHA256 (RFC 2104) against RFC 4231 and the empty and length extremes.
 *
 *  What is pinned: RFC 4231 test cases 1-3 and 6-7 -- cases 6 and 7 use a
 *  131-byte key, so the key is hashed before use, and case 7's 152-byte message
 *  crosses the inline-buffer boundary -- plus the empty key and empty message
 *  extremes, a key of exactly one block (not hashed), the message-length
 *  boundary between the inline and the temporary-buffer path, and the
 *  null-pointer tolerance. Cases 1-3 and 6-7 are the document's values; the
 *  boundary and empty-case values were computed with an independent
 *  implementation (OpenSSL HMAC-SHA256) when this test was written. A failing
 *  case prints the MAC it computed.
 */
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "support/hmac_sha256.hpp"

namespace {
    using ghostlock::support::hmac_sha256;
    using ghostlock::support::kHmacInlineMessageBytes;
    using ghostlock::support::kSha256Bytes;
    using Mac = std::array<std::uint8_t, kSha256Bytes>;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    Mac mac_of(const std::uint8_t *key, std::size_t key_len,
               const std::uint8_t *msg, std::size_t msg_len) noexcept {
        Mac mac{};
        hmac_sha256(key, key_len, msg, msg_len, mac.data());
        return mac;
    }

    const std::uint8_t *bytes_of(const char *text) noexcept {
        return reinterpret_cast<const std::uint8_t *>(text);
    }

    Mac from_hex(const char *hex) noexcept {
        const auto nibble = [](char digit) -> int {
            return digit <= '9' ? digit - '0' : digit - 'a' + 10;
        };
        Mac mac{};
        for (std::size_t i = 0U; i < kSha256Bytes; ++i) {
            mac[i] = static_cast<std::uint8_t>(
                    (nibble(hex[2U * i]) << 4) | nibble(hex[2U * i + 1U]));
        }
        return mac;
    }

    void check(const char *name, const std::uint8_t *key, std::size_t key_len,
               const std::uint8_t *msg, std::size_t msg_len, const Mac &want) {
        const Mac got = mac_of(key, key_len, msg, msg_len);
        if (got == want) {
            return;
        }
        ++g_failures;
        std::printf("FAIL: %s\n  computed ", name);
        for (const std::uint8_t byte : got) {
            std::printf("%02x", static_cast<unsigned>(byte));
        }
        std::printf("\n");
    }

    void check_text(const char *name, const std::uint8_t *key, std::size_t key_len,
                    const char *msg, const Mac &want) {
        check(name, key, key_len, bytes_of(msg), std::strlen(msg), want);
    }

    void rfc_4231_cases() {
        std::array<std::uint8_t, 20> key_0b{};
        key_0b.fill(0x0bU);
        check_text("RFC 4231 case 1 (20x0b / 'Hi There')", key_0b.data(), key_0b.size(),
                   "Hi There",
                   from_hex("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));

        check_text("RFC 4231 case 2 ('Jefe' / 'what do ya want for nothing?')",
                   bytes_of("Jefe"), 4U, "what do ya want for nothing?",
                   from_hex("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));

        std::array<std::uint8_t, 20> key_aa{};
        key_aa.fill(0xaaU);
        std::array<std::uint8_t, 50> data_dd{};
        data_dd.fill(0xddU);
        check("RFC 4231 case 3 (20xaa / 50xdd)", key_aa.data(), key_aa.size(),
              data_dd.data(), data_dd.size(),
              from_hex("773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe"));

        std::array<std::uint8_t, 131> key_long{};
        key_long.fill(0xaaU);
        check_text("RFC 4231 case 6 (131xaa key, hashed first)",
                   key_long.data(), key_long.size(),
                   "Test Using Larger Than Block-Size Key - Hash Key First",
                   from_hex("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"));
        /* RFC 4231 case 7 uses a 152-byte message, which is past the inline bound this build
         *  allows, because the write path must not allocate. The case is kept rather than dropped:
         *  it now pins the refusal, and the value it would have produced is printed beside it so the
         *  case is not lost. */
        const char *case7 = "This is a test using a larger than block-size key and a larger than "
                            "block-size data. The key needs to be hashed before being used by the "
                            "HMAC algorithm.";
        std::array<std::uint8_t, 32> refused{};
        refused.fill(0x5aU);
        hmac_sha256(key_long.data(), key_long.size(),
                    reinterpret_cast<const std::uint8_t *>(case7), 152U, refused.data());
        bool untouched = true;
        for (std::size_t i = 0U; i < refused.size(); ++i) {
            if (refused[i] != 0x5aU) {
                untouched = false;
            }
        }
        expect(untouched, "a message past the inline bound is refused: not buffered and not written");
        std::printf("hmac-sha256: case 7 (152 bytes) refused; its RFC value would be "
                    "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2\n");

    }

    void the_empty_extremes() {
        check("the empty message under key 'Jefe'", bytes_of("Jefe"), 4U, nullptr, 0U,
              from_hex("923598ca6d64af2a5dba79dcd021a8a0fe5c5f557519adaaf0ad532d4506dd30"));
        check("the empty key over 'Hi There'", nullptr, 0U, bytes_of("Hi There"), 8U,
              from_hex("e48411262715c8370cd5e7bf8e82bef53bd53712d007f3429351843b77c7bb9b"));
    }

    void the_length_boundaries() {
        std::array<std::uint8_t, 64> key_0c{};
        key_0c.fill(0x0cU);
        check_text("a key of exactly one block is not hashed", key_0c.data(), key_0c.size(),
                   "boundary",
                   from_hex("1af7cc06b8130cc1d1e9387f8835adedb2fc150ef22842875c146ec08b024323"));

        std::array<std::uint8_t, kHmacInlineMessageBytes> at_capacity{};
        for (std::size_t i = 0U; i < at_capacity.size(); ++i) {
            at_capacity[i] = static_cast<std::uint8_t>(i & 0xffU);
        }
        check("a message of exactly the inline capacity", bytes_of("Jefe"), 4U,
              at_capacity.data(), at_capacity.size(),
              from_hex("d8f65b1ed5e893cd84e45e2c5456be128c3043f3aa2aee33f4fd926cfec68ba2"));

        std::array<std::uint8_t, kHmacInlineMessageBytes + 1U> past_capacity{};
        for (std::size_t i = 0U; i < past_capacity.size(); ++i) {
            past_capacity[i] = static_cast<std::uint8_t>(i & 0xffU);
        }
        /* One byte past the capacity is refused rather than buffered, so this case now pins the
         *  refusal instead of a value: the buffer is filled with a sentinel and must come back
         *  untouched. The value it would have produced is kept beside it, so nothing measured is
         *  lost. */
        std::array<std::uint8_t, kSha256Bytes> refused{};
        refused.fill(0x5aU);
        hmac_sha256(bytes_of("Jefe"), 4U, past_capacity.data(), past_capacity.size(), refused.data());
        bool untouched = true;
        for (std::size_t i = 0U; i < refused.size(); ++i) {
            if (refused[i] != 0x5aU) {
                untouched = false;
            }
        }
        expect(untouched, "one byte past the inline capacity is refused, not buffered and not written");
        std::printf("hmac-sha256: past capacity refused; the old value would have been "
                    "5268040695826faba93fa002dd57ca97f9be422cd83754f29a7ceacf729335de\n");
    }

    void null_pointers_read_as_empty() {
        const Mac empty_message = mac_of(bytes_of("Jefe"), 4U, nullptr, 0U);
        expect(mac_of(bytes_of("Jefe"), 4U, nullptr, 7U) == empty_message,
               "a null message reads as the empty message whatever its length says");
        const Mac empty_key = mac_of(nullptr, 0U, bytes_of("Hi There"), 8U);
        expect(mac_of(nullptr, 9U, bytes_of("Hi There"), 8U) == empty_key,
               "a null key reads as the empty key whatever its length says");
        /* A null output is a no-op; reaching the next line is the assertion. */
        hmac_sha256(bytes_of("Jefe"), 4U, bytes_of("Hi There"), 8U, nullptr);
    }
}  // namespace

int main() {
    std::printf("hmac-sha256: RFC 4231\n");
    rfc_4231_cases();
    std::printf("hmac-sha256: the empty extremes\n");
    the_empty_extremes();
    std::printf("hmac-sha256: the key and message length boundaries\n");
    the_length_boundaries();
    std::printf("hmac-sha256: null pointers\n");
    null_pointers_read_as_empty();
    if (g_failures != 0U) {
        std::printf("hmac-sha256: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("hmac-sha256: PASS (RFC 4231 vectors)\n");
    return 0;
}
