/* Host tests for B5-3: the CVE-2026-43284 page-cache block write.
 *
 * The device syscalls are replaced by a fake SpliceIoOps that models a file,
 * a pipe and the recorded UDP datagrams; the fake also applies the ESP CBC
 * decryption in place over the fake page, so the tests observe the actual
 * page-cache-write semantics (desired block appears at the file offset) rather
 * than just call shapes. Covered: surface availability, read/write boundaries
 * (empty span, non-multiple length, overflow, EOF), short read/write, EINTR
 * retry on every syscall, pipe/io failures, multi-block offset advance,
 * failure rollback, ICV truncation, the FileCacheWriteOps positive/negative
 * contract and the secret wipe.
 *
 * The two injected fallbacks are covered too: the OldPageSource read fallback
 * and the HelperWriteSource used when file_fd < 0 (the App cannot open the
 * vendor file at all). Both fail the block closed when unbound or failing.
 *
 * No device, kernel or real syscall dependency: the test links the same
 * translation units the device build uses. */

#include "backend/cve_2026_43284/pagecache/pagecache.hpp"

#include "backend/cve_2026_43284/ipsec/ipsec.hpp"

#include <array>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace {
    using ghostlock::backend::cve_2026_43284::IpsecSaParams;
    using ghostlock::backend::cve_2026_43284::PageCacheWriteContext;
    using ghostlock::backend::cve_2026_43284::WriteResult;
    using ghostlock::backend::cve_2026_43284::esp_decrypt_block;
    using ghostlock::backend::cve_2026_43284::esp_verify_datagram;
    using ghostlock::backend::cve_2026_43284::kEspCiphertextOffset;
    using ghostlock::backend::cve_2026_43284::kEspDatagramBytes;
    using ghostlock::backend::cve_2026_43284::kEspIvOffset;
    using ghostlock::backend::cve_2026_43284::kEspSeqOffset;
    using ghostlock::backend::cve_2026_43284::make_file_cache_write_ops;
    using ghostlock::backend::cve_2026_43284::read_block;
    using ghostlock::backend::cve_2026_43284::write16;
    using ghostlock::backend::cve_2026_43284::write_block;
    using ghostlock::backend::cve_2026_43284::zeroize;
    using ghostlock::contract::FileCacheWriteOps;

    constexpr std::uint64_t kNoOffset = std::numeric_limits<std::uint64_t>::max();
    constexpr int kFileFd = 10;
    constexpr int kSocketFd = 11;
    constexpr int kPipeReadFd = 12;
    constexpr int kPipeWriteFd = 13;

    struct FakeState final {
        std::vector<std::uint8_t> file;   /* fake page cache */
        std::vector<std::uint8_t> pipe;   /* bytes currently in the pipe */
        std::vector<std::vector<std::uint8_t>> datagrams;
        std::vector<std::uint64_t> datagram_page_offsets;
        IpsecSaParams sa{};
        std::uint64_t pending_page_offset = kNoOffset;
        bool pipe_open = false;
        bool simulate_inplace = true;

        /* Fault injection: a non-zero errno makes the next matching call fail
         * with -errno; a *_limit >= 0 caps the transferred count; a positive
         * *_eintr returns -EINTR that many times first; fail_at fails the Nth
         * pipe->socket splice. */
        int pipe2_errno = 0;
        int read_errno = 0;
        int read_limit = -1;
        int read_eintr = 0;
        int vmsplice_errno = 0;
        int vmsplice_limit = -1;
        int vmsplice_eintr = 0;
        int splice_file_errno = 0;
        int splice_file_limit = -1;
        int splice_file_eintr = 0;
        int splice_send_errno = 0;
        int splice_send_limit = -1;
        int splice_send_eintr = 0;
        int splice_send_fail_at = -1;

        int read_calls = 0;
        int vmsplice_calls = 0;
        int splice_file_calls = 0;
        int splice_send_calls = 0;
    };

    FakeState g_fake;

    int fake_pipe2(int fds[2], int flags) noexcept {
        (void)flags;
        if (g_fake.pipe2_errno != 0) return -g_fake.pipe2_errno;
        fds[0] = kPipeReadFd;
        fds[1] = kPipeWriteFd;
        g_fake.pipe.clear();
        g_fake.pipe_open = true;
        g_fake.pending_page_offset = kNoOffset;
        return 0;
    }

    int fake_close_fd(int fd) noexcept {
        if (fd == kPipeReadFd || fd == kPipeWriteFd) {
            g_fake.pipe_open = false;
        }
        return 0;
    }

    long fake_read_at(int fd, std::uint8_t *out, std::size_t len,
                      std::uint64_t offset) noexcept {
        ++g_fake.read_calls;
        if (fd != kFileFd) return -EBADF;
        if (g_fake.read_errno != 0) return -g_fake.read_errno;
        if (g_fake.read_eintr > 0) {
            --g_fake.read_eintr;
            return -EINTR;
        }
        const std::uint64_t size = static_cast<std::uint64_t>(g_fake.file.size());
        const std::size_t avail =
                offset < size ? static_cast<std::size_t>(size - offset) : 0U;
        std::size_t n = len < avail ? len : avail;
        if (g_fake.read_limit >= 0 &&
            static_cast<std::size_t>(g_fake.read_limit) < n) {
            n = static_cast<std::size_t>(g_fake.read_limit);
        }
        for (std::size_t i = 0; i < n; ++i) {
            out[i] = g_fake.file[static_cast<std::size_t>(offset) + i];
        }
        return static_cast<long>(n);
    }

    long fake_vmsplice(int fd, const std::uint8_t *data, std::size_t len,
                       unsigned flags) noexcept {
        (void)flags;
        ++g_fake.vmsplice_calls;
        if (fd != kPipeWriteFd) return -EBADF;
        if (g_fake.vmsplice_errno != 0) return -g_fake.vmsplice_errno;
        if (g_fake.vmsplice_eintr > 0) {
            --g_fake.vmsplice_eintr;
            return -EINTR;
        }
        std::size_t n = len;
        if (g_fake.vmsplice_limit >= 0 &&
            static_cast<std::size_t>(g_fake.vmsplice_limit) < n) {
            n = static_cast<std::size_t>(g_fake.vmsplice_limit);
        }
        g_fake.pipe.insert(g_fake.pipe.end(), data, data + n);
        return static_cast<long>(n);
    }

    long fake_send_datagram(int fd, const std::uint8_t *buf,
                            std::size_t len) noexcept {
        if (fd != kSocketFd) return -EBADF;
        (void)buf;
        return static_cast<long>(len);
    }

    long fake_splice(int fd_in, const std::uint64_t *off_in, int fd_out,
                     const std::uint64_t *off_out, std::size_t len,
                     unsigned flags) noexcept {
        (void)off_out;
        (void)flags;
        if (fd_in == kFileFd && fd_out == kPipeWriteFd) {
            ++g_fake.splice_file_calls;
            if (g_fake.splice_file_errno != 0) return -g_fake.splice_file_errno;
            if (g_fake.splice_file_eintr > 0) {
                --g_fake.splice_file_eintr;
                return -EINTR;
            }
            const std::uint64_t offset = off_in != nullptr ? *off_in : 0U;
            const std::uint64_t size = static_cast<std::uint64_t>(g_fake.file.size());
            const std::size_t avail =
                    offset < size ? static_cast<std::size_t>(size - offset) : 0U;
            std::size_t n = len < avail ? len : avail;
            if (g_fake.splice_file_limit >= 0 &&
                static_cast<std::size_t>(g_fake.splice_file_limit) < n) {
                n = static_cast<std::size_t>(g_fake.splice_file_limit);
            }
            for (std::size_t i = 0; i < n; ++i) {
                g_fake.pipe.push_back(
                        g_fake.file[static_cast<std::size_t>(offset) + i]);
            }
            g_fake.pending_page_offset = offset;
            return static_cast<long>(n);
        }
        if (fd_in == kPipeReadFd && fd_out == kSocketFd) {
            ++g_fake.splice_send_calls;
            if (g_fake.splice_send_errno != 0) return -g_fake.splice_send_errno;
            if (g_fake.splice_send_eintr > 0) {
                --g_fake.splice_send_eintr;
                return -EINTR;
            }
            if (g_fake.splice_send_fail_at > 0 &&
                g_fake.splice_send_calls == g_fake.splice_send_fail_at) {
                return -EIO;
            }
            std::size_t n = len;
            if (g_fake.splice_send_limit >= 0 &&
                static_cast<std::size_t>(g_fake.splice_send_limit) < n) {
                n = static_cast<std::size_t>(g_fake.splice_send_limit);
            }
            if (n > g_fake.pipe.size()) n = g_fake.pipe.size();
            std::vector<std::uint8_t> datagram(
                    g_fake.pipe.begin(),
                    g_fake.pipe.begin() + static_cast<std::ptrdiff_t>(n));
            g_fake.datagrams.push_back(datagram);
            g_fake.datagram_page_offsets.push_back(g_fake.pending_page_offset);

            /* Model the vulnerable kernel: an authenticated full datagram is
             * CBC-decrypted in place over the spliced page. */
            if (g_fake.simulate_inplace && n == len &&
                g_fake.pending_page_offset != kNoOffset &&
                esp_verify_datagram(g_fake.sa, datagram.data(), datagram.size())) {
                std::array<std::uint8_t, 16> plain{};
                if (esp_decrypt_block(g_fake.sa.aes_key.data(),
                                      datagram.data() + kEspIvOffset,
                                      datagram.data() + kEspCiphertextOffset,
                                      plain.data())) {
                    const std::size_t off =
                            static_cast<std::size_t>(g_fake.pending_page_offset);
                    for (std::size_t i = 0; i < plain.size(); ++i) {
                        if (off + i < g_fake.file.size()) g_fake.file[off + i] = plain[i];
                    }
                }
            }
            g_fake.pipe.erase(g_fake.pipe.begin(),
                              g_fake.pipe.begin() + static_cast<std::ptrdiff_t>(n));
            g_fake.pending_page_offset = kNoOffset;
            return static_cast<long>(n);
        }
        return -EBADF;
    }

    ghostlock::backend::cve_2026_43284::pagecache::SpliceIoOps make_fake_io() noexcept {
        return ghostlock::backend::cve_2026_43284::pagecache::SpliceIoOps{
                &fake_pipe2, &fake_splice, &fake_vmsplice, &fake_send_datagram,
                &fake_read_at, &fake_close_fd};
    }

    /* Vendor old-page source: mirrors the fake page cache (so the ciphertext
     * the write splices is the same block), or returns an injected result. */
    struct FakeOldPage final {
        long result = 16;
        int calls = 0;
    };

    long fake_old_read16(void *raw, std::uint64_t offset,
                         std::uint8_t out[16]) noexcept {
        auto *fake = static_cast<FakeOldPage *>(raw);
        if (fake == nullptr || out == nullptr) return -EINVAL;
        ++fake->calls;
        if (fake->result != 16) return fake->result;
        if (offset + 16U > g_fake.file.size()) return -EIO;
        for (std::size_t i = 0; i < 16U; ++i) {
            out[i] = g_fake.file[static_cast<std::size_t>(offset) + i];
        }
        return 16;
    }

    /* Helper write source: splice(2)s the fake page into the write pipe exactly
     * as the patched crash_dump64 would, or returns an injected result. */
    struct FakeHelper final {
        long result = 16;
        int calls = 0;
    };

    long fake_helper_splice16(void *raw, int pipe_write_fd,
                              std::uint64_t offset) noexcept {
        auto *fake = static_cast<FakeHelper *>(raw);
        if (fake == nullptr) return -EINVAL;
        ++fake->calls;
        if (pipe_write_fd != kPipeWriteFd) return -EBADF;
        if (fake->result != 16) return fake->result;
        if (offset + 16U > g_fake.file.size()) return -EIO;
        for (std::size_t i = 0; i < 16U; ++i) {
            g_fake.pipe.push_back(
                    g_fake.file[static_cast<std::size_t>(offset) + i]);
        }
        g_fake.pending_page_offset = offset;
        return 16;
    }

    void reset_fake(std::size_t file_bytes, std::uint8_t fill) {
        g_fake = FakeState{};
        g_fake.file.assign(file_bytes, fill);
    }

    IpsecSaParams sample_sa() {
        IpsecSaParams sa{};
        sa.spi = 0x01020304U;
        sa.encap_port = 0x0506U;
        sa.sender_port = 0x0708U;
        sa.icv_len = 16U;
        for (std::size_t i = 0; i < sa.aes_key.size(); ++i) {
            sa.aes_key[i] = static_cast<std::uint8_t>(i + 1U);
            sa.hmac_key[i] = static_cast<std::uint8_t>(0x80U + i);
        }
        return sa;
    }

    PageCacheWriteContext make_ctx() {
        PageCacheWriteContext ctx{};
        ctx.io = make_fake_io();
        ctx.file_fd = kFileFd;
        ctx.socket_fd = kSocketFd;
        ctx.sa = sample_sa();
        g_fake.sa = ctx.sa;
        return ctx;
    }

    bool datagram_all(const std::vector<std::uint8_t> &dgram,
                      std::size_t offset, std::size_t len, std::uint8_t value) {
        for (std::size_t i = 0; i < len; ++i) {
            if (dgram[offset + i] != value) return false;
        }
        return true;
    }
} // namespace

int main() {
    using ghostlock::backend::cve_2026_43284::pagecache::SpliceIoOps;

    /* ---- The injectable surface is available only when fully bound. ---- */
    {
        SpliceIoOps empty{};
        assert(!empty.available());
        SpliceIoOps io = make_fake_io();
        assert(io.available());
        io.close_fd = nullptr;
        assert(!io.available());
        io = make_fake_io();
        io.send_datagram = nullptr;
        assert(!io.available());
        io = make_fake_io();
        io.read_at = nullptr;
        assert(!io.available());
        io = make_fake_io();
        io.splice = nullptr;
        assert(!io.available());
    }

    /* ---- read_block: full, EOF, short, EINTR, io error, unavailable. ---- */
    {
        reset_fake(32U, 0x5AU);
        PageCacheWriteContext ctx = make_ctx();
        std::array<std::uint8_t, 16> out{};
        assert(read_block(ctx, 16U, out.data()) == WriteResult::Ok);
        for (const auto b : out) assert(b == 0x5AU);
        assert(read_block(ctx, 32U, out.data()) == WriteResult::ShortRead);
        assert(read_block(ctx, 0U, nullptr) == WriteResult::InvalidArgument);

        reset_fake(32U, 0x11U);
        g_fake.read_limit = 8;
        PageCacheWriteContext short_ctx = make_ctx();
        assert(read_block(short_ctx, 0U, out.data()) == WriteResult::Ok);
        for (const auto b : out) assert(b == 0x11U);
        assert(read_block(short_ctx, 28U, out.data()) == WriteResult::ShortRead);
        assert(g_fake.read_calls >= 3);

        reset_fake(32U, 0x22U);
        g_fake.read_eintr = 2;
        PageCacheWriteContext eintr_ctx = make_ctx();
        assert(read_block(eintr_ctx, 0U, out.data()) == WriteResult::Ok);
        assert(g_fake.read_calls == 3);
        for (const auto b : out) assert(b == 0x22U);

        reset_fake(32U, 0x00U);
        g_fake.read_errno = EIO;
        PageCacheWriteContext err_ctx = make_ctx();
        assert(read_block(err_ctx, 0U, out.data()) == WriteResult::IoError);

        reset_fake(16U, 0x00U);
        PageCacheWriteContext gone_ctx = make_ctx();
        gone_ctx.file_fd = -1;
        assert(read_block(gone_ctx, 0U, out.data()) == WriteResult::NotAvailable);

        reset_fake(16U, 0x00U);
        PageCacheWriteContext huge_ctx = make_ctx();
        assert(read_block(huge_ctx, std::numeric_limits<std::uint64_t>::max(),
                          out.data()) == WriteResult::InvalidArgument);
    }

    /* ---- write16: one block, datagram layout, in-place page update. ---- */
    {
        reset_fake(64U, 0xA5U);
        PageCacheWriteContext ctx = make_ctx();
        std::array<std::uint8_t, 16> desired{};
        for (std::size_t i = 0; i < desired.size(); ++i) {
            desired[i] = static_cast<std::uint8_t>(0x10U + i);
        }
        assert(write16(ctx, 16U, desired.data()) == WriteResult::Ok);
        assert(ctx.next_seq == 2U);
        assert(ctx.stats.blocks_written == 1U);
        assert(ctx.stats.blocks_failed == 0U);
        assert(ctx.stats.datagrams_sent == 1U);
        assert(g_fake.datagrams.size() == 1U);
        assert(g_fake.datagram_page_offsets.size() == 1U);
        assert(g_fake.datagram_page_offsets[0] == 16U);
        assert(!g_fake.pipe_open);

        const std::vector<std::uint8_t> &dgram = g_fake.datagrams[0];
        assert(dgram.size() == kEspDatagramBytes);
        assert(esp_verify_datagram(ctx.sa, dgram.data(), dgram.size()));
        /* The ciphertext spliced from the page is the old content. */
        assert(datagram_all(dgram, kEspCiphertextOffset, 16U, 0xA5U));
        /* The kernel decrypts desired into the page. */
        for (std::size_t i = 0; i < desired.size(); ++i) {
            assert(g_fake.file[16U + i] == desired[i]);
        }
        assert(g_fake.file[0] == 0xA5U);
        assert(g_fake.file[32U] == 0xA5U);
    }

    /* ---- write16: argument, availability and overflow rejections. ---- */
    {
        reset_fake(16U, 0x00U);
        PageCacheWriteContext ctx = make_ctx();
        std::array<std::uint8_t, 16> block{};
        assert(write16(ctx, 0U, nullptr) == WriteResult::InvalidArgument);
        assert(write16(ctx, std::numeric_limits<std::uint64_t>::max(), block.data()) ==
               WriteResult::InvalidArgument);

        PageCacheWriteContext no_socket = make_ctx();
        no_socket.socket_fd = -1;
        assert(write16(no_socket, 0U, block.data()) == WriteResult::NotAvailable);

        PageCacheWriteContext bad_icv = make_ctx();
        bad_icv.sa.icv_len = 0U;
        assert(write16(bad_icv, 0U, block.data()) == WriteResult::NotAvailable);
        PageCacheWriteContext big_icv = make_ctx();
        big_icv.sa.icv_len = 33U;
        assert(write16(big_icv, 0U, block.data()) == WriteResult::NotAvailable);
    }

    /* ---- write16: pipe/vmsplice/splice/read failure paths. ---- */
    {
        reset_fake(16U, 0x33U);
        g_fake.pipe2_errno = EIO;
        PageCacheWriteContext pipe_ctx = make_ctx();
        std::array<std::uint8_t, 16> block{};
        block.fill(0x44U);
        assert(write16(pipe_ctx, 0U, block.data()) == WriteResult::IoError);
        assert(g_fake.datagrams.empty());
        assert(pipe_ctx.stats.blocks_written == 0U);
        assert(pipe_ctx.stats.blocks_failed == 1U);
        assert(pipe_ctx.next_seq == 1U);
        assert(g_fake.file[0] == 0x33U);

        reset_fake(16U, 0x33U);
        g_fake.vmsplice_limit = 0; /* a zero transfer is a short write */
        PageCacheWriteContext vm_ctx = make_ctx();
        assert(write16(vm_ctx, 0U, block.data()) == WriteResult::ShortWrite);
        assert(!g_fake.pipe_open);
        assert(g_fake.datagrams.empty());
        assert(vm_ctx.stats.blocks_failed == 1U);
        assert(g_fake.file[0] == 0x33U);

        reset_fake(16U, 0x33U);
        g_fake.read_limit = 0; /* EOF before one block */
        PageCacheWriteContext rd_ctx = make_ctx();
        assert(write16(rd_ctx, 0U, block.data()) == WriteResult::ShortRead);
        assert(g_fake.datagrams.empty());

        reset_fake(16U, 0x33U);
        g_fake.splice_file_limit = 0; /* page splice came up short */
        PageCacheWriteContext sp_ctx = make_ctx();
        assert(write16(sp_ctx, 0U, block.data()) == WriteResult::ShortRead);
        assert(g_fake.datagrams.empty());
        assert(!g_fake.pipe_open);

        reset_fake(16U, 0x33U);
        g_fake.splice_send_limit = static_cast<int>(kEspDatagramBytes - 1U);
        PageCacheWriteContext ss_ctx = make_ctx();
        assert(write16(ss_ctx, 0U, block.data()) == WriteResult::ShortWrite);
        assert(ss_ctx.stats.blocks_failed == 1U);
        assert(g_fake.file[0] == 0x33U); /* truncated datagram wrote nothing */
    }

    /* ---- write16: EINTR on every syscall is retried. ---- */
    {
        reset_fake(16U, 0x55U);
        g_fake.read_eintr = 1;
        g_fake.vmsplice_eintr = 1;
        g_fake.splice_file_eintr = 1;
        g_fake.splice_send_eintr = 1;
        PageCacheWriteContext ctx = make_ctx();
        std::array<std::uint8_t, 16> block{};
        block.fill(0x66U);
        assert(write16(ctx, 0U, block.data()) == WriteResult::Ok);
        assert(g_fake.read_calls == 2);
        assert(g_fake.vmsplice_calls == 3);
        assert(g_fake.splice_file_calls == 2);
        assert(g_fake.splice_send_calls == 2);
        assert(g_fake.file[0] == 0x66U);
    }

    /* ---- ICV truncation: total length and verification follow icv_len. ---- */
    {
        reset_fake(16U, 0x00U);
        PageCacheWriteContext ctx = make_ctx();
        ctx.sa.icv_len = 8U;
        g_fake.sa = ctx.sa;
        std::array<std::uint8_t, 16> desired{};
        desired.fill(0x5CU);
        assert(write16(ctx, 0U, desired.data()) == WriteResult::Ok);
        assert(g_fake.datagrams.size() == 1U);
        assert(g_fake.datagrams[0].size() == 8U + 16U + 16U + 8U);
        assert(esp_verify_datagram(ctx.sa, g_fake.datagrams[0].data(),
                                   g_fake.datagrams[0].size()));
        assert(g_fake.file[0] == 0x5CU);
    }

    /* ---- write_block: empty span is a no-op, arguments fail closed. ---- */
    {
        reset_fake(32U, 0x77U);
        PageCacheWriteContext ctx = make_ctx();
        assert(write_block(ctx, 0U, nullptr, 0U) == WriteResult::Ok);
        std::array<std::uint8_t, 16> block{};
        block.fill(0x01U);
        assert(write_block(ctx, 0U, block.data(), 0U) == WriteResult::Ok);
        assert(g_fake.datagrams.empty());
        assert(ctx.stats.blocks_written == 0U);
        assert(g_fake.file[0] == 0x77U);

        std::vector<std::uint8_t> odd(17U, 0x01U);
        assert(write_block(ctx, 0U, odd.data(), odd.size()) ==
               WriteResult::InvalidArgument);
        assert(write_block(ctx, 0U, nullptr, 16U) == WriteResult::InvalidArgument);
        assert(g_fake.datagrams.empty());

        PageCacheWriteContext gone = make_ctx();
        gone.file_fd = -1;
        std::vector<std::uint8_t> one(16U, 0x02U);
        assert(write_block(gone, 0U, one.data(), one.size()) ==
               WriteResult::NotAvailable);

        PageCacheWriteContext overflow = make_ctx();
        std::vector<std::uint8_t> two(32U, 0x03U);
        assert(write_block(overflow,
                           std::numeric_limits<std::uint64_t>::max() - 16U,
                           two.data(), two.size()) == WriteResult::InvalidArgument);
    }

    /* ---- write_block: multi-block span advances the offset by 16. ---- */
    {
        reset_fake(80U, 0x00U);
        PageCacheWriteContext ctx = make_ctx();
        std::array<std::uint8_t, 48> payload{};
        for (std::size_t i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<std::uint8_t>(i + 1U);
        }
        assert(write_block(ctx, 16U, payload.data(), payload.size()) ==
               WriteResult::Ok);
        assert(g_fake.datagrams.size() == 3U);
        assert(g_fake.datagram_page_offsets.size() == 3U);
        assert(g_fake.datagram_page_offsets[0] == 16U);
        assert(g_fake.datagram_page_offsets[1] == 32U);
        assert(g_fake.datagram_page_offsets[2] == 48U);
        assert(ctx.stats.blocks_written == 3U);
        assert(ctx.stats.datagrams_sent == 3U);
        assert(ctx.next_seq == 4U);
        for (std::size_t i = 0; i < payload.size(); ++i) {
            assert(g_fake.file[16U + i] == payload[i]);
        }
        assert(g_fake.file[0] == 0x00U);
        assert(g_fake.file[64U] == 0x00U);
        /* SPI is constant, sequence advances per datagram. */
        for (std::size_t i = 0; i < g_fake.datagrams.size(); ++i) {
            assert(esp_verify_datagram(ctx.sa, g_fake.datagrams[i].data(),
                                       g_fake.datagrams[i].size()));
            assert(datagram_all(g_fake.datagrams[i], kEspCiphertextOffset, 16U, 0x00U));
        }
        assert(g_fake.datagrams[0][kEspSeqOffset + 3U] == 1U);
        assert(g_fake.datagrams[1][kEspSeqOffset + 3U] == 2U);
        assert(g_fake.datagrams[2][kEspSeqOffset + 3U] == 3U);
    }

    /* ---- write_block: EOF on the first block is not a partial write. ---- */
    {
        reset_fake(16U, 0x11U);
        PageCacheWriteContext ctx = make_ctx();
        std::array<std::uint8_t, 16> payload{};
        payload.fill(0x22U);
        assert(write_block(ctx, 32U, payload.data(), payload.size()) ==
               WriteResult::ShortRead);
        assert(g_fake.datagrams.empty());
        assert(ctx.stats.blocks_written == 0U);
        assert(ctx.stats.blocks_failed == 1U);
        assert(ctx.next_seq == 1U);
        assert(g_fake.file[0] == 0x11U);
    }

    /* ---- write_block: failure after block 0 rolls back to PartialWrite and
     * leaves the later blocks untouched. ---- */
    {
        reset_fake(80U, 0x00U);
        g_fake.splice_send_fail_at = 2;
        PageCacheWriteContext ctx = make_ctx();
        std::array<std::uint8_t, 48> payload{};
        for (std::size_t i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<std::uint8_t>(i + 1U);
        }
        assert(write_block(ctx, 16U, payload.data(), payload.size()) ==
               WriteResult::PartialWrite);
        assert(g_fake.datagrams.size() == 1U);
        assert(ctx.stats.blocks_written == 1U);
        assert(ctx.stats.blocks_failed == 1U);
        assert(ctx.next_seq == 2U);
        for (std::size_t i = 0; i < 16U; ++i) {
            assert(g_fake.file[16U + i] == payload[i]);
        }
        for (std::size_t i = 32U; i < 80U; ++i) {
            assert(g_fake.file[i] == 0x00U);
        }
    }

    /* ---- write_block: failure on block 0 reports the real reason. ---- */
    {
        reset_fake(32U, 0x00U);
        g_fake.splice_send_fail_at = 1;
        PageCacheWriteContext ctx = make_ctx();
        std::array<std::uint8_t, 16> payload{};
        payload.fill(0x99U);
        assert(write_block(ctx, 0U, payload.data(), payload.size()) ==
               WriteResult::IoError);
        assert(g_fake.datagrams.empty());
        assert(ctx.stats.blocks_written == 0U);
        assert(ctx.stats.blocks_failed == 1U);
        assert(ctx.next_seq == 1U);
        assert(g_fake.file[0] == 0x00U);
    }

    /* ---- FileCacheWriteOps contract: positive and negative handles. ---- */
    {
        reset_fake(32U, 0x00U);
        PageCacheWriteContext ctx = make_ctx();
        FileCacheWriteOps ops = make_file_cache_write_ops(ctx);
        assert(ops.available());
        assert(ops.ctx == &ctx);
        assert(ops.write16 != nullptr);

        std::array<std::uint8_t, 16> desired{};
        desired.fill(0x7EU);
        assert(ops.write16(ops.ctx, 0U, desired.data()) == 0);
        for (std::size_t i = 0; i < desired.size(); ++i) {
            assert(g_fake.file[i] == 0x7EU);
        }
        assert(ops.write16(nullptr, 0U, desired.data()) ==
               static_cast<std::int32_t>(WriteResult::InvalidArgument));
        assert(ops.write16(ops.ctx, 0U, nullptr) ==
               static_cast<std::int32_t>(WriteResult::InvalidArgument));

        PageCacheWriteContext no_socket = make_ctx();
        no_socket.socket_fd = -1;
        assert(!make_file_cache_write_ops(no_socket).available());

        PageCacheWriteContext no_io = make_ctx();
        no_io.io.close_fd = nullptr;
        assert(!make_file_cache_write_ops(no_io).available());

        PageCacheWriteContext no_icv = make_ctx();
        no_icv.sa.icv_len = 0U;
        assert(!make_file_cache_write_ops(no_icv).available());

        FileCacheWriteOps none{};
        assert(!none.available());
        assert(none.write16 == nullptr);
    }

    /* ---- zeroize wipes the session secrets in the context. ---- */
    {
        reset_fake(16U, 0x00U);
        PageCacheWriteContext ctx = make_ctx();
        bool any = false;
        for (const auto b : ctx.sa.aes_key) any = any || (b != 0U);
        for (const auto b : ctx.sa.hmac_key) any = any || (b != 0U);
        assert(any);
        zeroize(ctx);
        for (const auto b : ctx.sa.aes_key) assert(b == 0U);
        for (const auto b : ctx.sa.hmac_key) assert(b == 0U);
        assert(ctx.sa.spi == 0U);
    }

    /* ---- old_page fallback: direct read fails, the bridge supplies the
     * block and the write still succeeds. ---- */
    {
        reset_fake(32U, 0xB7U);
        g_fake.read_errno = EIO;
        PageCacheWriteContext ctx = make_ctx();
        FakeOldPage old{};
        ctx.old_page.ctx = &old;
        ctx.old_page.read16 = &fake_old_read16;

        std::array<std::uint8_t, 16> out{};
        assert(read_block(ctx, 0U, out.data()) == WriteResult::Ok);
        for (const auto b : out) assert(b == 0xB7U);
        assert(old.calls == 1);

        std::array<std::uint8_t, 16> desired{};
        desired.fill(0x42U);
        assert(write16(ctx, 16U, desired.data()) == WriteResult::Ok);
        assert(old.calls == 2);
        assert(g_fake.datagrams.size() == 1U);
        assert(esp_verify_datagram(ctx.sa, g_fake.datagrams[0].data(),
                                   g_fake.datagrams[0].size()));
        /* The ciphertext spliced from the page is the bridged old block. */
        assert(datagram_all(g_fake.datagrams[0], kEspCiphertextOffset, 16U, 0xB7U));
        for (std::size_t i = 0; i < 16U; ++i) {
            assert(g_fake.file[16U + i] == 0x42U);
        }
        assert(ctx.stats.blocks_written == 1U);
        assert(ctx.stats.blocks_failed == 0U);
    }

    /* ---- the direct read wins: the bridge is not consulted. ---- */
    {
        reset_fake(16U, 0x24U);
        PageCacheWriteContext ctx = make_ctx();
        FakeOldPage old{};
        ctx.old_page.ctx = &old;
        ctx.old_page.read16 = &fake_old_read16;
        std::array<std::uint8_t, 16> out{};
        assert(read_block(ctx, 0U, out.data()) == WriteResult::Ok);
        for (const auto b : out) assert(b == 0x24U);
        assert(old.calls == 0);
    }

    /* ---- bridge failure is fail-closed: no datagram, page unchanged. ---- */
    {
        reset_fake(16U, 0x33U);
        g_fake.read_errno = EACCES;
        PageCacheWriteContext ctx = make_ctx();
        FakeOldPage old{};
        old.result = -EIO;
        ctx.old_page.ctx = &old;
        ctx.old_page.read16 = &fake_old_read16;
        std::array<std::uint8_t, 16> desired{};
        desired.fill(0x77U);
        assert(write16(ctx, 0U, desired.data()) == WriteResult::IoError);
        assert(old.calls == 1);
        assert(g_fake.datagrams.empty());
        assert(ctx.stats.blocks_written == 0U);
        assert(ctx.stats.blocks_failed == 1U);
        assert(ctx.next_seq == 1U);
        assert(g_fake.file[0] == 0x33U);
    }

    /* ---- a bridge short read is also fail-closed. ---- */
    {
        reset_fake(16U, 0x33U);
        g_fake.read_errno = EIO;
        PageCacheWriteContext ctx = make_ctx();
        FakeOldPage old{};
        old.result = 0;
        ctx.old_page.ctx = &old;
        ctx.old_page.read16 = &fake_old_read16;
        std::array<std::uint8_t, 16> out{};
        assert(read_block(ctx, 0U, out.data()) == WriteResult::ShortRead);
        assert(old.calls == 1);
        assert(g_fake.datagrams.empty());
    }

    /* ---- an unbound old_page keeps the exact direct failure code. ---- */
    {
        reset_fake(16U, 0x00U);
        g_fake.read_errno = EIO;
        PageCacheWriteContext ctx = make_ctx();
        std::array<std::uint8_t, 16> out{};
        assert(read_block(ctx, 0U, out.data()) == WriteResult::IoError);
    }


    /* ---- helper write source: file_fd < 0 + bridge -> helper completes the
     * 16-byte write through the same pipe/datagram path. ---- */
    {
        reset_fake(32U, 0xC3U);
        PageCacheWriteContext ctx = make_ctx();
        ctx.file_fd = -1;
        FakeOldPage old{};
        ctx.old_page.ctx = &old;
        ctx.old_page.read16 = &fake_old_read16;
        FakeHelper helper{};
        ctx.helper_write.ctx = &helper;
        ctx.helper_write.splice16 = &fake_helper_splice16;

        /* The capability advertises itself without a file fd. */
        FileCacheWriteOps ops = make_file_cache_write_ops(ctx);
        assert(ops.available());

        std::array<std::uint8_t, 16> desired{};
        desired.fill(0x5EU);
        assert(write16(ctx, 16U, desired.data()) == WriteResult::Ok);
        assert(helper.calls == 1);
        assert(old.calls == 1);
        assert(g_fake.splice_file_calls == 0);
        assert(g_fake.datagrams.size() == 1U);
        assert(g_fake.datagram_page_offsets[0] == 16U);
        assert(esp_verify_datagram(ctx.sa, g_fake.datagrams[0].data(),
                                   g_fake.datagrams[0].size()));
        /* The ciphertext comes from the helper-spliced page, not the direct
         * file splice. */
        assert(datagram_all(g_fake.datagrams[0], kEspCiphertextOffset, 16U, 0xC3U));
        for (std::size_t i = 0; i < 16U; ++i) {
            assert(g_fake.file[16U + i] == 0x5EU);
        }
        assert(ctx.stats.blocks_written == 1U);
        assert(ctx.stats.blocks_failed == 0U);
        assert(ctx.stats.datagrams_sent == 1U);
        assert(ctx.next_seq == 2U);
    }

    /* ---- helper unavailable with file_fd < 0 is NotAvailable: no datagram,
     * no page change, the read bridge is not consulted. ---- */
    {
        reset_fake(16U, 0x00U);
        PageCacheWriteContext ctx = make_ctx();
        ctx.file_fd = -1;
        FakeOldPage old{};
        ctx.old_page.ctx = &old;
        ctx.old_page.read16 = &fake_old_read16;
        assert(!make_file_cache_write_ops(ctx).available());
        std::array<std::uint8_t, 16> desired{};
        desired.fill(0x11U);
        assert(write16(ctx, 0U, desired.data()) == WriteResult::NotAvailable);
        assert(old.calls == 0);
        assert(g_fake.datagrams.empty());
        assert(g_fake.file[0] == 0x00U);
    }

    /* ---- helper failure is fail-closed: no datagram, blocks_failed, page
     * unchanged. ---- */
    {
        reset_fake(16U, 0x44U);
        PageCacheWriteContext ctx = make_ctx();
        ctx.file_fd = -1;
        FakeOldPage old{};
        ctx.old_page.ctx = &old;
        ctx.old_page.read16 = &fake_old_read16;
        FakeHelper helper{};
        helper.result = -EIO;
        ctx.helper_write.ctx = &helper;
        ctx.helper_write.splice16 = &fake_helper_splice16;
        std::array<std::uint8_t, 16> desired{};
        desired.fill(0x66U);
        assert(write16(ctx, 0U, desired.data()) == WriteResult::IoError);
        assert(helper.calls == 1);
        assert(g_fake.datagrams.empty());
        assert(ctx.stats.blocks_written == 0U);
        assert(ctx.stats.blocks_failed == 1U);
        assert(ctx.next_seq == 1U);
        assert(g_fake.file[0] == 0x44U);
    }

    /* ---- a short helper transfer is ShortWrite, fail-closed. ---- */
    {
        reset_fake(16U, 0x44U);
        PageCacheWriteContext ctx = make_ctx();
        ctx.file_fd = -1;
        FakeOldPage old{};
        ctx.old_page.ctx = &old;
        ctx.old_page.read16 = &fake_old_read16;
        FakeHelper helper{};
        helper.result = 8;
        ctx.helper_write.ctx = &helper;
        ctx.helper_write.splice16 = &fake_helper_splice16;
        std::array<std::uint8_t, 16> desired{};
        desired.fill(0x66U);
        assert(write16(ctx, 0U, desired.data()) == WriteResult::ShortWrite);
        assert(helper.calls == 1);
        assert(g_fake.datagrams.empty());
        assert(ctx.stats.blocks_failed == 1U);
        assert(g_fake.file[0] == 0x44U);
    }

    /* ---- the direct fd wins: a bound helper is never consulted. ---- */
    {
        reset_fake(16U, 0x24U);
        PageCacheWriteContext ctx = make_ctx();
        FakeHelper helper{};
        ctx.helper_write.ctx = &helper;
        ctx.helper_write.splice16 = &fake_helper_splice16;
        std::array<std::uint8_t, 16> desired{};
        desired.fill(0x42U);
        assert(write16(ctx, 0U, desired.data()) == WriteResult::Ok);
        assert(helper.calls == 0);
        assert(g_fake.splice_file_calls == 1);
        assert(g_fake.file[0] == 0x42U);
    }

    /* ---- file_fd < 0 + helper but a failing read (no old_page) fails closed
     * before the helper splice. ---- */
    {
        reset_fake(16U, 0x44U);
        PageCacheWriteContext ctx = make_ctx();
        ctx.file_fd = -1;
        FakeHelper helper{};
        ctx.helper_write.ctx = &helper;
        ctx.helper_write.splice16 = &fake_helper_splice16;
        std::array<std::uint8_t, 16> desired{};
        desired.fill(0x66U);
        assert(write16(ctx, 0U, desired.data()) == WriteResult::IoError);
        assert(helper.calls == 0);
        assert(g_fake.datagrams.empty());
        assert(ctx.stats.blocks_failed == 1U);
        assert(g_fake.file[0] == 0x44U);
    }

    /* ---- file_fd < 0: a failing read bridge fails closed before the helper
     * is ever invoked. ---- */
    {
        reset_fake(16U, 0x44U);
        PageCacheWriteContext ctx = make_ctx();
        ctx.file_fd = -1;
        FakeOldPage old{};
        old.result = -EIO;
        ctx.old_page.ctx = &old;
        ctx.old_page.read16 = &fake_old_read16;
        FakeHelper helper{};
        ctx.helper_write.ctx = &helper;
        ctx.helper_write.splice16 = &fake_helper_splice16;
        std::array<std::uint8_t, 16> desired{};
        desired.fill(0x66U);
        assert(write16(ctx, 0U, desired.data()) == WriteResult::IoError);
        assert(old.calls == 1);
        assert(helper.calls == 0);
        assert(g_fake.datagrams.empty());
        assert(ctx.stats.blocks_failed == 1U);
        assert(g_fake.file[0] == 0x44U);
    }

    std::puts("cve_2026_43284_pagecache_test: OK");
    return 0;
}
