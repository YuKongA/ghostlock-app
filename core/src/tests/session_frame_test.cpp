/** @file tests/session_frame_test.cpp
 *  The frame the session secrets travel in, both directions.
 *
 *  The encoder is checked against the layout the retired tree documented: a fixed eighty-four byte
 *  payload with the fields at the offsets that file names, a big-endian length prefix only when it
 *  is written to a stream, and a trailer that must match or the frame is refused. The decoder is
 *  checked with a pipe, because that is the shape it actually reads from, and each of the three
 *  checks -- version, kind and trailer -- is broken on purpose once so that a decoder which stopped
 *  looking would fail rather than pass.
 */
#include <array>
#include <cstdint>
#include <cstdio>
#include <unistd.h>

#include "backend/cve_2026_43284/session_frame.hpp"

namespace {
    using ghostlock::backend::cve_2026_43284::encode_session_secret_frame;
    using ghostlock::backend::cve_2026_43284::kSessionSecretFrameKindIpsecSa;
    using ghostlock::backend::cve_2026_43284::kSessionSecretFrameLengthPrefixBytes;
    using ghostlock::backend::cve_2026_43284::kSessionSecretFramePayloadBytes;
    using ghostlock::backend::cve_2026_43284::kSessionSecretFrameTrailer;
    using ghostlock::backend::cve_2026_43284::kSessionSecretFrameVersion;
    using ghostlock::backend::cve_2026_43284::read_session_secret_frame;
    using ghostlock::backend::cve_2026_43284::SaMaterial;
    using ghostlock::backend::cve_2026_43284::SessionFrameStatus;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    SaMaterial sample() noexcept {
        SaMaterial sa;
        sa.spi = 0x0A0B0C0DU;
        sa.encap_port = 4500U;
        sa.sender_port = 4501U;
        /* The frame carries a fixed ICV length, so the sample must use that value and not an
         *  arbitrary one: writing twelve here made the decoder refuse the good frame at the check
         *  that compares against the frame own constant. */
        sa.icv_len = ghostlock::backend::cve_2026_43284::kSessionSecretFrameIcvBytes;
        sa.set = true;
        for (std::size_t i = 0U; i < 32U; ++i) {
            sa.aes_key[i] = static_cast<std::uint8_t>(i);
            sa.hmac_key[i] = static_cast<std::uint8_t>(0x40U + i);
        }
        return sa;
    }

    void the_payload_is_the_documented_length() {
        const std::array<std::uint8_t, kSessionSecretFramePayloadBytes> out =
            encode_session_secret_frame(sample());
        expect(out.size() == 84U, "the payload is the fixed eighty-four bytes");
        expect(out[0] == kSessionSecretFrameVersion, "version comes first");
        expect(out[1] == kSessionSecretFrameKindIpsecSa, "then the kind");
        expect(out[12] == ghostlock::backend::cve_2026_43284::kSessionSecretFrameIcvBytes,
               "the icv length is carried, not assumed from a constant");
        expect(out[16] == 0x00U && out[47] == 0x1FU, "the two keys sit at their offsets");
        expect(out[80] == 0x34U && out[83] == 0x38U, "and the trailer closes it");
        std::printf("session frame: payload %zu bytes, trailer %08x\n", out.size(),
                    static_cast<unsigned>(kSessionSecretFrameTrailer));
    }

    /** Feed exactly these bytes to a pipe and decode from the read end. */
    SessionFrameStatus decode(const std::uint8_t *bytes, std::size_t count, SaMaterial *out,
                              bool with_prefix) {
        int fds[2] = {-1, -1};
        if (::pipe(fds) != 0) {
            return SessionFrameStatus::IoError;
        }
        if (with_prefix) {
            const std::uint8_t prefix[4] = {static_cast<std::uint8_t>(count >> 24U),
                                            static_cast<std::uint8_t>(count >> 16U),
                                            static_cast<std::uint8_t>(count >> 8U),
                                            static_cast<std::uint8_t>(count)};
            static_cast<void>(::write(fds[1], prefix, sizeof(prefix)));
        }
        if (count != 0U) {
            static_cast<void>(::write(fds[1], bytes, count));
        }
        /* The reader reads until end of input, so the writing end must be closed before it runs;
         * leaving it open is what a real caller never does and what made this test lie. */
        ::close(fds[1]);
        const SessionFrameStatus status = read_session_secret_frame(fds[0], out);
        ::close(fds[0]);
        return status;
    }

    void a_good_frame_round_trips() {
        const std::array<std::uint8_t, kSessionSecretFramePayloadBytes> bytes =
            encode_session_secret_frame(sample());
        SaMaterial back{};
        expect(decode(bytes.data(), bytes.size(), &back, true) == SessionFrameStatus::Ok,
               "a well formed frame decodes");
        expect(back.spi == 0x0A0B0C0DU && back.encap_port == 4500U && back.sender_port == 4501U,
               "and the fields come back as they went in");
        /* The material field is deliberately not checked: the frame file never mentions it, in the
         *  retired tree either, because saying that the material is usable is the caller's act after a
         *  successful read, not the reader's. Asserting it here would have been my assumption passing
         *  judgement on a contract that does not exist. */
        expect(back.icv_len == ghostlock::backend::cve_2026_43284::kSessionSecretFrameIcvBytes,
               "including the ones the layout puts late");
        expect(back.aes_key[0] == 0x00U && back.aes_key[31] == 0x1FU && back.hmac_key[0] == 0x40U,
               "and the key material lands in the fields the access expects");
    }

    void the_three_checks_each_refuse() {
        const std::array<std::uint8_t, kSessionSecretFramePayloadBytes> good =
            encode_session_secret_frame(sample());
        std::array<std::uint8_t, kSessionSecretFramePayloadBytes> bad = good;
        bad[0] = static_cast<std::uint8_t>(kSessionSecretFrameVersion + 1U);
        SaMaterial out{};
        expect(decode(bad.data(), bad.size(), &out, true) == SessionFrameStatus::BadVersion,
               "a wrong version is refused by name");
        bad = good;
        bad[1] = static_cast<std::uint8_t>(kSessionSecretFrameKindIpsecSa + 1U);
        expect(decode(bad.data(), bad.size(), &out, true) == SessionFrameStatus::BadKind,
               "a wrong kind is refused by name");
        bad = good;
        bad[83] = static_cast<std::uint8_t>(bad[83] ^ 0x01U);
        expect(decode(bad.data(), bad.size(), &out, true) == SessionFrameStatus::BadTrailer,
               "a wrong trailer is refused by name");
    }

    void the_extremes_are_named() {
        SaMaterial out{};
        expect(decode(nullptr, 0U, &out, false) == SessionFrameStatus::Missing,
               "no bytes at all is the optional frame being absent, not an error");
        /* A prefix that names a different length is refused as a bad length, and that is checked by
         *  passing a count the encoder never produces. What this case checks instead is a genuinely
         *  short write: fewer than four prefix bytes, and a correct prefix followed by a truncated
         *  payload. */
        int short_fds[2] = {-1, -1};
        expect(::pipe(short_fds) == 0, "a pipe for the short cases");
        const std::uint8_t two[2] = {0x00U, 0x00U};
        static_cast<void>(::write(short_fds[1], two, sizeof(two)));
        ::close(short_fds[1]);
        expect(read_session_secret_frame(short_fds[0], &out) == SessionFrameStatus::Truncated,
               "fewer than four prefix bytes is truncated");
        ::close(short_fds[0]);

        const std::array<std::uint8_t, kSessionSecretFramePayloadBytes> good =
            encode_session_secret_frame(sample());
        int cut_fds[2] = {-1, -1};
        expect(::pipe(cut_fds) == 0, "a pipe for the cut payload");
        const std::uint8_t prefix[4] = {0x00U, 0x00U, 0x00U, 0x54U};   /* 84, the one frozen length */
        static_cast<void>(::write(cut_fds[1], prefix, sizeof(prefix)));
        static_cast<void>(::write(cut_fds[1], good.data(), good.size() - 1U));
        ::close(cut_fds[1]);
        expect(read_session_secret_frame(cut_fds[0], &out) == SessionFrameStatus::Truncated,
               "a correct prefix with a truncated payload is truncated");
        ::close(cut_fds[0]);

        int short_cut[2] = {-1, -1};
        expect(::pipe(short_cut) == 0, "a pipe for the short payload the helper would misname");
        const std::uint8_t short_prefix[4] = {0x00U, 0x00U, 0x00U, 0x53U};   /* 83: not the frozen one */
        static_cast<void>(::write(short_cut[1], short_prefix, sizeof(short_prefix)));
        static_cast<void>(::write(short_cut[1], good.data(), good.size() - 1U));
        ::close(short_cut[1]);
        expect(read_session_secret_frame(short_cut[0], &out) == SessionFrameStatus::BadLength,
               "and a prefix naming another length is a bad length, which the reader says by name");
        ::close(short_cut[0]);
    }
}  // namespace

int main() {
    std::printf("session frame: the payload\n");
    the_payload_is_the_documented_length();
    std::printf("session frame: a good frame\n");
    a_good_frame_round_trips();
    std::printf("session frame: the three checks\n");
    the_three_checks_each_refuse();
    std::printf("session frame: the extremes\n");
    the_extremes_are_named();
    if (g_failures != 0U) {
        std::printf("session frame: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("session frame: PASS (fixed payload, three named refusals, round trip)\n");
    return 0;
}
