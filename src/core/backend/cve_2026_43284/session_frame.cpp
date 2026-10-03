/* B5-1: runtime session-secret frame codec (channel B). See session_frame.hpp
 * for the frozen 84-byte layout and the security/failure contract. */

#include "backend/cve_2026_43284/session_frame.hpp"

#include <cerrno>
#include <poll.h>
#include <unistd.h>

#include <array>

namespace ghostlock::backend::cve_2026_43284 {
    namespace {
        constexpr int kPollTimeoutMs = 0;

        /* Wipes a raw byte range in a way the optimizer may not elide; used on
         * the stack scratch buffer that briefly holds the secret payload. */
        void wipe_bytes(void *data, std::size_t len) noexcept {
            auto *bytes = static_cast<volatile std::uint8_t *>(data);
            for (std::size_t i = 0; i < len; ++i) bytes[i] = 0U;
        }

        std::uint32_t read_be32(const std::uint8_t *bytes) noexcept {
            return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
                   (static_cast<std::uint32_t>(bytes[1]) << 16U) |
                   (static_cast<std::uint32_t>(bytes[2]) << 8U) |
                   static_cast<std::uint32_t>(bytes[3]);
        }

        std::uint16_t read_be16(const std::uint8_t *bytes) noexcept {
            return static_cast<std::uint16_t>(
                    (static_cast<std::uint16_t>(bytes[0]) << 8U) |
                    static_cast<std::uint16_t>(bytes[1]));
        }

        void write_be32(std::uint8_t *bytes, std::uint32_t value) noexcept {
            bytes[0] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
            bytes[1] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
            bytes[2] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
            bytes[3] = static_cast<std::uint8_t>(value & 0xffU);
        }

        void write_be16(std::uint8_t *bytes, std::uint16_t value) noexcept {
            bytes[0] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
            bytes[1] = static_cast<std::uint8_t>(value & 0xffU);
        }

        /* Fill out_len bytes, retrying EINTR. Returns the number of bytes read
         * (== out_len on success, < out_len on EOF). errno is left set on a
         * read failure; the caller distinguishes EOF from error via a boolean
         * out-parameter. */
        std::size_t read_upto(int fd, std::uint8_t *out, std::size_t out_len,
                              bool *io_error) noexcept {
            std::size_t used = 0;
            while (used < out_len) {
                const ssize_t count = ::read(fd, out + used, out_len - used);
                if (count > 0) {
                    used += static_cast<std::size_t>(count);
                    continue;
                }
                if (count == 0) break;
                if (errno == EINTR) continue;
                *io_error = true;
                break;
            }
            return used;
        }
    } // namespace

    std::array<std::uint8_t, kSessionSecretFramePayloadBytes>
    encode_session_secret_frame(const IpsecSaParams &sa) noexcept {
        std::array<std::uint8_t, kSessionSecretFramePayloadBytes> out{};
        out[0] = kSessionSecretFrameVersion;
        out[1] = kSessionSecretFrameKindIpsecSa;
        write_be16(&out[2], 0U); /* reserved */
        write_be32(&out[4], sa.spi);
        write_be16(&out[8], sa.encap_port);
        write_be16(&out[10], sa.sender_port);
        out[12] = sa.icv_len;
        out[13] = 0U;
        out[14] = 0U;
        out[15] = 0U;
        for (std::size_t i = 0; i < sa.aes_key.size(); ++i) {
            out[16U + i] = sa.aes_key[i];
        }
        for (std::size_t i = 0; i < sa.hmac_key.size(); ++i) {
            out[48U + i] = sa.hmac_key[i];
        }
        write_be32(&out[80], kSessionSecretFrameTrailer);
        return out;
    }

    SessionFrameStatus read_session_secret_frame(int fd, IpsecSaParams *out) noexcept {
        if (out == nullptr || fd < 0) return SessionFrameStatus::IoError;
        zeroize(*out);

        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        const int ready = ::poll(&pfd, 1, kPollTimeoutMs);
        if (ready < 0) {
            if (errno == EINTR) return SessionFrameStatus::Missing;
            return SessionFrameStatus::IoError;
        }
        /* ready == 0: nothing buffered yet -> optional frame absent. A hangup
         * (POLLHUP/POLLNVAL) still falls through to read, which then reports
         * Missing (EOF) or IoError. */
        if (ready == 0) return SessionFrameStatus::Missing;

        bool io_error = false;
        std::uint8_t header[kSessionSecretFrameLengthPrefixBytes] = {};
        const std::size_t header_read =
                read_upto(fd, header, sizeof(header), &io_error);
        if (header_read == 0U && !io_error) return SessionFrameStatus::Missing;
        if (io_error) return SessionFrameStatus::IoError;
        if (header_read != sizeof(header)) return SessionFrameStatus::Truncated;

        /* Reject the malformed/oversized prefix before touching any payload,
         * then require the one frozen length (plan section 6.4). */
        const std::uint32_t declared = read_be32(header);
        if (declared == 0U ||
            declared > kSessionSecretFrameMaxPayloadBytes ||
            declared != kSessionSecretFramePayloadBytes) {
            return SessionFrameStatus::BadLength;
        }

        std::array<std::uint8_t, kSessionSecretFramePayloadBytes> payload{};
        const std::size_t payload_read =
                read_upto(fd, payload.data(), payload.size(), &io_error);
        if (io_error) {
            wipe_bytes(payload.data(), payload.size());
            return SessionFrameStatus::IoError;
        }
        if (payload_read != payload.size()) {
            wipe_bytes(payload.data(), payload.size());
            return SessionFrameStatus::Truncated;
        }

        SessionFrameStatus status = SessionFrameStatus::Ok;
        if (payload[0] != kSessionSecretFrameVersion) {
            status = SessionFrameStatus::BadVersion;
        } else if (payload[1] != kSessionSecretFrameKindIpsecSa) {
            status = SessionFrameStatus::BadKind;
        } else if (payload[2] != 0U || payload[3] != 0U ||
                   payload[13] != 0U || payload[14] != 0U ||
                   payload[15] != 0U) {
            status = SessionFrameStatus::BadReserved;
        } else if (payload[12] != kEspIcvBytes) {
            status = SessionFrameStatus::BadIcvLen;
        } else if (read_be32(&payload[80]) != kSessionSecretFrameTrailer) {
            status = SessionFrameStatus::BadTrailer;
        }

        if (status != SessionFrameStatus::Ok) {
            wipe_bytes(payload.data(), payload.size());
            return status;
        }

        IpsecSaParams decoded{};
        decoded.spi = read_be32(&payload[4]);
        decoded.encap_port = read_be16(&payload[8]);
        decoded.sender_port = read_be16(&payload[10]);
        decoded.icv_len = payload[12];
        for (std::size_t i = 0; i < decoded.aes_key.size(); ++i) {
            decoded.aes_key[i] = payload[16U + i];
        }
        for (std::size_t i = 0; i < decoded.hmac_key.size(); ++i) {
            decoded.hmac_key[i] = payload[48U + i];
        }
        wipe_bytes(payload.data(), payload.size());
        *out = decoded;
        return SessionFrameStatus::Ok;
    }
} // namespace ghostlock::backend::cve_2026_43284
