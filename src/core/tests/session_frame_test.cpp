/* Host test for B5-1: the runtime session-secret frame (channel B).
 *
 * Covers the encode golden vector (shared with the Kotlin
 * SessionSecretFrameTest), a valid pipe round-trip, and every documented
 * rejection: missing frame, truncated prefix/payload, wrong version, wrong kind,
 * wrong length, non-zero reserved bytes, wrong icv_len and wrong trailer. It
 * also asserts that zeroize() and every rejected decode leave the destination
 * fully wiped, and that the optional frame is absent-tolerant only at the
 * channel layer (missing -> Missing), so the cve_2026_43284 path can fail
 * closed while 43499 never calls this decoder at all. */

#include "backend/cve_2026_43284/session_frame.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <unistd.h>

namespace {
    using ghostlock::backend::cve_2026_43284::IpsecSaParams;
    using ghostlock::backend::cve_2026_43284::SessionFrameStatus;
    using ghostlock::backend::cve_2026_43284::encode_session_secret_frame;
    using ghostlock::backend::cve_2026_43284::read_session_secret_frame;
    using ghostlock::backend::cve_2026_43284::zeroize;

    constexpr std::size_t kPayload = 84U;

    IpsecSaParams sample() {
        IpsecSaParams sa;
        sa.spi = 0x01020304U;
        sa.encap_port = 0x0506U;
        sa.sender_port = 0x0708U;
        sa.icv_len = 16U;
        for (std::size_t i = 0; i < sa.aes_key.size(); ++i) {
            sa.aes_key[i] = static_cast<std::uint8_t>(i);
            sa.hmac_key[i] = static_cast<std::uint8_t>(0x20U + i);
        }
        return sa;
    }

    std::vector<std::uint8_t> payload_bytes() {
        const auto encoded = encode_session_secret_frame(sample());
        return std::vector<std::uint8_t>(encoded.begin(), encoded.end());
    }

    /* [u32 be declared_len][payload ...] */
    std::vector<std::uint8_t> framed(const std::vector<std::uint8_t> &payload,
                                     std::uint32_t declared_len) {
        std::vector<std::uint8_t> out;
        out.push_back(static_cast<std::uint8_t>((declared_len >> 24U) & 0xffU));
        out.push_back(static_cast<std::uint8_t>((declared_len >> 16U) & 0xffU));
        out.push_back(static_cast<std::uint8_t>((declared_len >> 8U) & 0xffU));
        out.push_back(static_cast<std::uint8_t>(declared_len & 0xffU));
        out.insert(out.end(), payload.begin(), payload.end());
        return out;
    }

    int pipe_with(const std::vector<std::uint8_t> &bytes) {
        int fds[2] = {-1, -1};
        assert(::pipe(fds) == 0);
        if (!bytes.empty()) {
            const ssize_t written =
                    ::write(fds[1], bytes.data(), bytes.size());
            assert(written == static_cast<ssize_t>(bytes.size()));
        }
        ::close(fds[1]);
        return fds[0];
    }

    SessionFrameStatus decode(const std::vector<std::uint8_t> &bytes,
                              IpsecSaParams *out) {
        const int fd = pipe_with(bytes);
        const SessionFrameStatus status = read_session_secret_frame(fd, out);
        ::close(fd);
        return status;
    }

    bool all_zero(const IpsecSaParams &sa) {
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(&sa);
        for (std::size_t i = 0; i < sizeof(IpsecSaParams); ++i) {
            if (bytes[i] != 0U) return false;
        }
        return true;
    }
} // namespace

int main() {
    /* ---- Golden 84-byte vector; Kotlin asserts the same hex. ---- */
    {
        const std::vector<std::uint8_t> encoded = payload_bytes();
        assert(encoded.size() == kPayload);
        static const char *kGoldenHex =
                "01010000010203040506070810000000"
                "000102030405060708090a0b0c0d0e0f"
                "101112131415161718191a1b1c1d1e1f"
                "202122232425262728292a2b2c2d2e2f"
                "303132333435363738393a3b3c3d3e3f"
                "34383238";
        std::string hex;
        hex.reserve(kPayload * 2U);
        static const char *kDigits = "0123456789abcdef";
        for (const std::uint8_t byte : encoded) {
            hex.push_back(kDigits[byte >> 4U]);
            hex.push_back(kDigits[byte & 0x0fU]);
        }
        assert(hex == kGoldenHex);
    }

    /* ---- Valid round-trip. ---- */
    {
        IpsecSaParams out;
        const SessionFrameStatus status = decode(framed(payload_bytes(), kPayload), &out);
        assert(status == SessionFrameStatus::Ok);
        const IpsecSaParams expected = sample();
        assert(out.spi == expected.spi);
        assert(out.encap_port == expected.encap_port);
        assert(out.sender_port == expected.sender_port);
        assert(out.icv_len == expected.icv_len);
        assert(out.aes_key == expected.aes_key);
        assert(out.hmac_key == expected.hmac_key);
    }

    /* ---- Missing frame: the optional channel is absent. ---- */
    {
        IpsecSaParams out;
        assert(decode({}, &out) == SessionFrameStatus::Missing);
        assert(all_zero(out));
    }

    /* ---- Truncated length prefix. ---- */
    {
        IpsecSaParams out;
        assert(decode({0x00U, 0x00U}, &out) == SessionFrameStatus::Truncated);
        assert(all_zero(out));
    }

    /* ---- Truncated payload. ---- */
    {
        std::vector<std::uint8_t> bytes = framed(payload_bytes(), kPayload);
        bytes.resize(4U + 10U);
        IpsecSaParams out;
        assert(decode(bytes, &out) == SessionFrameStatus::Truncated);
        assert(all_zero(out));
    }

    /* ---- Wrong version / kind / reserved / icv_len / trailer. ---- */
    {
        for (std::size_t index : {std::size_t{0}, std::size_t{1}, std::size_t{2},
                                  std::size_t{12}, std::size_t{80}}) {
            std::vector<std::uint8_t> payload = payload_bytes();
            payload[index] = static_cast<std::uint8_t>(payload[index] ^ 0xffU);
            IpsecSaParams out;
            const SessionFrameStatus status = decode(framed(payload, kPayload), &out);
            assert(status != SessionFrameStatus::Ok);
            assert(all_zero(out));
        }
    }

    /* ---- Explicit per-field rejection tags. ---- */
    {
        std::vector<std::uint8_t> payload = payload_bytes();
        payload[0] = 2U;
        IpsecSaParams out;
        assert(decode(framed(payload, kPayload), &out) ==
               SessionFrameStatus::BadVersion);

        payload = payload_bytes();
        payload[1] = 9U;
        assert(decode(framed(payload, kPayload), &out) == SessionFrameStatus::BadKind);

        payload = payload_bytes();
        payload[2] = 1U;
        assert(decode(framed(payload, kPayload), &out) ==
               SessionFrameStatus::BadReserved);

        payload = payload_bytes();
        payload[14] = 1U;
        assert(decode(framed(payload, kPayload), &out) ==
               SessionFrameStatus::BadReserved);

        payload = payload_bytes();
        payload[12] = 8U;
        assert(decode(framed(payload, kPayload), &out) ==
               SessionFrameStatus::BadIcvLen);

        payload = payload_bytes();
        payload[83] = static_cast<std::uint8_t>(payload[83] ^ 0x01U);
        assert(decode(framed(payload, kPayload), &out) ==
               SessionFrameStatus::BadTrailer);
        assert(all_zero(out));
    }

    /* ---- Wrong declared length and an oversized prefix. ---- */
    {
        IpsecSaParams out;
        assert(decode(framed(payload_bytes(), 83U), &out) ==
               SessionFrameStatus::BadLength);
        const std::vector<std::uint8_t> oversized =
                framed({}, 4097U);
        assert(decode(oversized, &out) == SessionFrameStatus::BadLength);
        assert(all_zero(out));
    }

    /* ---- zeroize clears every byte of a populated value. ---- */
    {
        IpsecSaParams sa = sample();
        assert(!all_zero(sa));
        zeroize(sa);
        assert(all_zero(sa));
    }

    std::puts("session_frame_test: OK");
    return 0;
}
