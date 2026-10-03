#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_SESSION_FRAME_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_SESSION_FRAME_HPP

/* B5-1: runtime session-secret frame (refactor-plan section 6, scheme B).
 *
 * --ghostlock-app-call with --enable-status-record reads one length-prefixed
 * GLKv3 document (profile/entry.cpp read_glk1_frame_stdin). When -- and only
 * when -- the decoded selection is the cve_2026_43284 backend, the same stdin
 * may carry one additional length-prefixed frame holding the IpSecManager
 * session secrets. The profile layer never understands it; 43499 never reads
 * it, so that path's stdin/status-ACK behavior is unchanged.
 *
 * Frame layout (refactor-plan section 6.4, frozen):
 *   u32 be payload_len = 84            (not part of the 84 payload bytes)
 *   payload:
 *     +0  u8    version        = 1
 *     +1  u8    kind           = 1      (IPSEC_SA)
 *     +2  u16 be reserved      = 0
 *     +4  u32 be spi
 *     +8  u16 be encap_port
 *     +10 u16 be sender_port
 *     +12 u8    icv_len        = 16
 *     +13 u8    reserved2[3]   = 0
 *     +16 u8    aes_key[32]
 *     +48 u8    hmac_key[32]
 *     +80 u32 be trailer       = 0x34383238
 *
 * Payload integers are big-endian (network byte order), matching the length
 * prefix and ESP-in-UDP. Reserved fields are the 5 bytes the payload layout in
 * the task text omitted: 1 + 1 + 2 + 4 + 2 + 2 + 1 + 3 + 32 + 32 + 4 = 84.
 *
 * The secrets are session secrets: they never enter profile::Document, a
 * serialized profile, the process argv or a log. They live only in the
 * process-local holder below and are zeroized on every rejection and at scope
 * exit. Any mismatch (version/kind/length/reserved/icv_len/trailer, short read,
 * absent frame) is fail-closed. */

#include "backend/cve_2026_43284/ipsec/ipsec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43284 {

    inline constexpr std::uint8_t kSessionSecretFrameVersion = 1U;
    inline constexpr std::uint8_t kSessionSecretFrameKindIpsecSa = 1U;
    inline constexpr std::uint32_t kSessionSecretFrameTrailer = 0x34383238U;
    inline constexpr std::size_t kSessionSecretFramePayloadBytes = 84U;
    inline constexpr std::size_t kSessionSecretFrameLengthPrefixBytes = 4U;
    /* Defensive cap for the declared length, so a malformed oversized prefix is
     * rejected before any allocation (plan section 6.4). */
    inline constexpr std::size_t kSessionSecretFrameMaxPayloadBytes = 4096U;

    enum class SessionFrameStatus : std::uint8_t {
        Ok = 0,
        Missing,   /* optional frame absent: no bytes on stdin */
        Truncated, /* length prefix or payload ended early */
        BadVersion,
        BadKind,
        BadLength,
        BadReserved,
        BadIcvLen,
        BadTrailer,
        IoError,
    };

    /* Encodes the fixed 84-byte payload (no length prefix). Deterministic; the
     * Kotlin SessionSecretFrame encoder must produce the identical bytes. */
    [[nodiscard]] std::array<std::uint8_t, kSessionSecretFramePayloadBytes>
    encode_session_secret_frame(const IpsecSaParams &sa) noexcept;

    /* Reads and validates one optional session frame from fd. On Ok, *out
     * receives the decoded parameters; on every other status *out is left
     * zeroed. A zero-timeout poll first: when nothing is buffered it returns
     * Missing instead of blocking (stdin stays open for the status ACK). At most
     * one frame is consumed. */
    [[nodiscard]] SessionFrameStatus read_session_secret_frame(
            int fd, IpsecSaParams *out) noexcept;

    /* Process-local holder: keeps the decoded parameters in memory only and
     * wipes them through zeroize() at scope exit. Non-copyable. */
    class ScopedIpsecSaParams final {
    public:
        ScopedIpsecSaParams() noexcept = default;
        ~ScopedIpsecSaParams() noexcept { zeroize(value); }
        ScopedIpsecSaParams(const ScopedIpsecSaParams &) = delete;
        ScopedIpsecSaParams &operator=(const ScopedIpsecSaParams &) = delete;

        IpsecSaParams value{};
    };

} // namespace ghostlock::backend::cve_2026_43284

#endif
