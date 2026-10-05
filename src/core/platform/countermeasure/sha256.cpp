/* CM-2 SHA-256 (FIPS 180-4) and whole-file digest helper.
 *
 * Independent implementation of the public algorithm; see sha256.hpp for why it
 * lives here instead of reusing the CVE-2026-43284 IpSec copy (R1 firewall).
 * The working context, chunk buffer and digest are wiped before return. */

#include "platform/countermeasure/sha256.hpp"

#include <array>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

namespace ghostlock::platform::countermeasure {
    namespace {

        constexpr std::size_t kBlockBytes = 64u;
        constexpr std::size_t kDigestBytes = 32u;

        constexpr std::array<std::uint32_t, 64> kRoundConstants = {
            0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
            0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
            0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
            0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
            0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
            0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
            0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
            0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
            0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
            0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
            0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
            0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
            0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
        };

        constexpr std::array<std::uint32_t, 8> kInitialState = {
            0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
            0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
        };

        struct Sha256State final {
            std::array<std::uint32_t, 8> state{};
            std::array<std::uint8_t, kBlockBytes> buffer{};
            std::uint64_t total_bytes = 0u;
            std::size_t buffer_len = 0u;
        };

        [[nodiscard]] std::uint32_t rotr(std::uint32_t value,
                                         std::uint32_t amount) noexcept {
            return (value >> amount) | (value << (32u - amount));
        }

        void transform(Sha256State &ctx,
                       const std::uint8_t block[kBlockBytes]) noexcept {
            std::array<std::uint32_t, 64> w{};
            for (std::size_t i = 0u; i < 16u; ++i) {
                w[i] = (static_cast<std::uint32_t>(block[4u * i]) << 24u) |
                       (static_cast<std::uint32_t>(block[4u * i + 1u]) << 16u) |
                       (static_cast<std::uint32_t>(block[4u * i + 2u]) << 8u) |
                       static_cast<std::uint32_t>(block[4u * i + 3u]);
            }
            for (std::size_t i = 16u; i < 64u; ++i) {
                const std::uint32_t s0 = rotr(w[i - 15u], 7u) ^
                                         rotr(w[i - 15u], 18u) ^ (w[i - 15u] >> 3u);
                const std::uint32_t s1 = rotr(w[i - 2u], 17u) ^
                                         rotr(w[i - 2u], 19u) ^ (w[i - 2u] >> 10u);
                w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
            }

            std::uint32_t a = ctx.state[0];
            std::uint32_t b = ctx.state[1];
            std::uint32_t c = ctx.state[2];
            std::uint32_t d = ctx.state[3];
            std::uint32_t e = ctx.state[4];
            std::uint32_t f = ctx.state[5];
            std::uint32_t g = ctx.state[6];
            std::uint32_t h = ctx.state[7];
            for (std::size_t i = 0u; i < 64u; ++i) {
                const std::uint32_t s1 = rotr(e, 6u) ^ rotr(e, 11u) ^ rotr(e, 25u);
                const std::uint32_t ch = (e & f) ^ ((~e) & g);
                const std::uint32_t t1 = h + s1 + ch + kRoundConstants[i] + w[i];
                const std::uint32_t s0 = rotr(a, 2u) ^ rotr(a, 13u) ^ rotr(a, 22u);
                const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                const std::uint32_t t2 = s0 + maj;
                h = g;
                g = f;
                f = e;
                e = d + t1;
                d = c;
                c = b;
                b = a;
                a = t1 + t2;
            }

            ctx.state[0] += a;
            ctx.state[1] += b;
            ctx.state[2] += c;
            ctx.state[3] += d;
            ctx.state[4] += e;
            ctx.state[5] += f;
            ctx.state[6] += g;
            ctx.state[7] += h;
            std::memset(w.data(), 0, sizeof(w));
        }

        void init(Sha256State &ctx) noexcept {
            ctx.state = kInitialState;
            ctx.buffer.fill(0u);
            ctx.total_bytes = 0u;
            ctx.buffer_len = 0u;
        }

        void update(Sha256State &ctx, const std::uint8_t *data,
                    std::size_t len) noexcept {
            if (data == nullptr) {
                return;
            }
            ctx.total_bytes += static_cast<std::uint64_t>(len);
            for (std::size_t i = 0u; i < len; ++i) {
                ctx.buffer[ctx.buffer_len] = data[i];
                ++ctx.buffer_len;
                if (ctx.buffer_len == kBlockBytes) {
                    transform(ctx, ctx.buffer.data());
                    ctx.buffer_len = 0u;
                }
            }
        }

        void finalize(Sha256State &ctx, std::uint8_t out[kDigestBytes]) noexcept {
            const std::uint64_t bits = ctx.total_bytes * 8u;
            std::size_t i = ctx.buffer_len;
            ctx.buffer[i] = 0x80u;
            ++i;
            if (i > 56u) {
                while (i < kBlockBytes) {
                    ctx.buffer[i] = 0u;
                    ++i;
                }
                transform(ctx, ctx.buffer.data());
                i = 0u;
            }
            while (i < 56u) {
                ctx.buffer[i] = 0u;
                ++i;
            }
            for (std::size_t j = 0u; j < 8u; ++j) {
                ctx.buffer[56u + j] = static_cast<std::uint8_t>(
                        (bits >> (56u - 8u * j)) & 0xffu);
            }
            transform(ctx, ctx.buffer.data());
            for (std::size_t j = 0u; j < 8u; ++j) {
                out[4u * j] = static_cast<std::uint8_t>((ctx.state[j] >> 24u) & 0xffu);
                out[4u * j + 1u] =
                        static_cast<std::uint8_t>((ctx.state[j] >> 16u) & 0xffu);
                out[4u * j + 2u] =
                        static_cast<std::uint8_t>((ctx.state[j] >> 8u) & 0xffu);
                out[4u * j + 3u] = static_cast<std::uint8_t>(ctx.state[j] & 0xffu);
            }
        }

    } // namespace

    void sha256(const std::uint8_t *data, std::size_t len,
                std::uint8_t out[32]) noexcept {
        if (out == nullptr) {
            return;
        }
        if (data == nullptr) {
            len = 0u;
        }
        Sha256State ctx{};
        init(ctx);
        update(ctx, data, len);
        finalize(ctx, out);
        std::memset(&ctx, 0, sizeof(ctx));
    }

    std::int32_t sha256_file(const char *path, char *out_hex,
                             std::size_t cap) noexcept {
        if (path == nullptr || out_hex == nullptr || cap < kSha256HexLength + 1u) {
            return -1;
        }
        /* POSIX read rather than stdio: the clang-analyzer unix.Stream check
         * cannot model a pread-style loop around fread and false-positives on
         * it, and open/read/close is the same surface the platform layer
         * already uses. EINTR is retried; any other error fails closed. */
        const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            return -1;
        }
        Sha256State ctx{};
        init(ctx);
        std::array<std::uint8_t, 8192u> chunk{};
        std::int32_t rc = 0;
        while (true) {
            const ssize_t got = ::read(fd, chunk.data(), chunk.size());
            if (got < 0) {
                if (errno == EINTR) {
                    continue;
                }
                rc = -1;
                break;
            }
            if (got == 0) {
                break;
            }
            update(ctx, chunk.data(), static_cast<std::size_t>(got));
        }
        (void)::close(fd);

        std::uint8_t digest[kDigestBytes] = {};
        finalize(ctx, digest);
        static constexpr char kHex[] = "0123456789abcdef";
        for (std::size_t i = 0u; i < kDigestBytes; ++i) {
            out_hex[2u * i] = kHex[(digest[i] >> 4u) & 0x0fu];
            out_hex[2u * i + 1u] = kHex[digest[i] & 0x0fu];
        }
        out_hex[kSha256HexLength] = '\0';

        std::memset(&ctx, 0, sizeof(ctx));
        std::memset(chunk.data(), 0, chunk.size());
        std::memset(digest, 0, sizeof(digest));
        return rc;
    }

} // namespace ghostlock::platform::countermeasure
