/** @file tests/real_splice_io_test.cpp
 *  The system side of the write, against a recording surface: no real call is made.
 *
 *  What is pinned: the retry rules the old implementation measured -- a short transfer is retried
 *  on the buffer, span and block-read writes, and -EINTR is retried everywhere without advancing --
 *  the one place a short transfer is not retried (the pipe-to-socket datagram splice, because it
 *  cannot be appended to the same datagram), both block sources (the direct descriptor and the
 *  injected bridge, each exercised; a bridge that cannot deliver a full block sends no datagram),
 *  the refusal of a frame too short to hold the page, and that the vector and the check are the
 *  primitives' answers rather than a second implementation.
 */
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43284/landing/esp_crypto.hpp"
#include "backend/cve_2026_43284/landing/real_splice_io.hpp"

namespace {
    using ghostlock::backend::cve_2026_43284::landing::esp_icv;
    using ghostlock::backend::cve_2026_43284::landing::esp_cbc_iv;
    using ghostlock::backend::cve_2026_43284::landing::HelperWriteSource;
    using ghostlock::backend::cve_2026_43284::landing::kBlockBytes;
    using ghostlock::backend::cve_2026_43284::landing::kHeaderBytes;
    using ghostlock::backend::cve_2026_43284::landing::OldPageSource;
    using ghostlock::backend::cve_2026_43284::landing::RealSpliceIo;
    using ghostlock::backend::cve_2026_43284::landing::SpliceIoOps;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    bool bytes_equal(const std::uint8_t *left, const std::uint8_t *right, std::size_t size) {
        for (std::size_t i = 0U; i < size; ++i) {
            if (left[i] != right[i]) {
                return false;
            }
        }
        return true;
    }

    /** How a scripted call answers. */
    enum class Answer { Full, ShortOnce, EintrOnce, Fail, Zero };

    /* The surface has no context parameter -- the old shape -- so the fake records in one file-scope
     * state, and the test drives one call at a time. */
    struct Fake final {
        std::size_t pipe2_calls = 0U;
        int pipe2_result = 0;
        int pipe_fds[2] = {41, 42};

        Answer vmsplice_answer = Answer::Full;
        bool vmsplice_short_pending = false;
        bool vmsplice_eintr_pending = false;
        std::size_t vmsplice_calls = 0U;
        std::size_t vmsplice_total = 0U;

        Answer span_answer = Answer::Full;
        bool span_short_pending = false;
        bool span_eintr_pending = false;
        std::size_t span_calls = 0U;
        std::size_t span_total = 0U;
        std::uint64_t span_offsets[4] = {};

        Answer datagram_answer = Answer::Full;
        bool datagram_eintr_pending = false;
        std::size_t datagram_calls = 0U;
        int datagram_fd = -1;
        std::size_t datagram_total = 0U;

        Answer read_answer = Answer::Full;
        bool read_short_pending = false;
        bool read_eintr_pending = false;
        std::size_t read_calls = 0U;
        std::uint64_t read_first_offset = 0U;

        /* The helper's pipe: how many bytes the fake helper wrote, and how the read answers. */
        Answer pipe_answer = Answer::Full;
        std::size_t pipe_left = kBlockBytes;
        bool pipe_eintr_pending = false;
        std::size_t pipe_calls = 0U;
        std::size_t pipe_total = 0U;
        std::uint8_t pipe_byte = 0x5AU;

        /* What each memory segment carried, in call order: the page segment is checked against
         * the block the one fetch returned, not against a constant. */
        std::size_t vmsplice_segments = 0U;
        std::uint8_t vmsplice_data[4][64] = {};
        std::size_t vmsplice_lens[4] = {};

        std::size_t send_calls = 0U;
        std::size_t close_calls = 0U;
    };
    Fake g_fake;

    void reset_fake() { g_fake = Fake{}; }

    int fake_pipe2(int fds[2], int) noexcept {
        ++g_fake.pipe2_calls;
        if (g_fake.pipe2_result != 0) {
            return g_fake.pipe2_result;
        }
        fds[0] = g_fake.pipe_fds[0];
        fds[1] = g_fake.pipe_fds[1];
        return 0;
    }

    long fake_vmsplice(int, const std::uint8_t *data, std::size_t len, unsigned) noexcept {
        ++g_fake.vmsplice_calls;
        if (g_fake.vmsplice_segments < 4U) {
            const std::size_t copy = len < 64U ? len : 64U;
            for (std::size_t i = 0U; i < copy; ++i) {
                g_fake.vmsplice_data[g_fake.vmsplice_segments][i] = data[i];
            }
            g_fake.vmsplice_lens[g_fake.vmsplice_segments] = len;
            ++g_fake.vmsplice_segments;
        }
        switch (g_fake.vmsplice_answer) {
        case Answer::Full:
            g_fake.vmsplice_total += len;
            return static_cast<long>(len);
        case Answer::ShortOnce:
            if (g_fake.vmsplice_short_pending) {
                g_fake.vmsplice_short_pending = false;
                g_fake.vmsplice_total += 6U;
                return 6L;
            }
            g_fake.vmsplice_total += len;
            return static_cast<long>(len);
        case Answer::EintrOnce:
            if (g_fake.vmsplice_eintr_pending) {
                g_fake.vmsplice_eintr_pending = false;
                return -EINTR;
            }
            g_fake.vmsplice_total += len;
            return static_cast<long>(len);
        case Answer::Fail:
            return -EIO;
        case Answer::Zero:
            return 0L;
        }
        return -EIO;
    }

    long fake_splice(int fd_in, const std::uint64_t *off_in, int fd_out,
                     const std::uint64_t *, std::size_t len, unsigned) noexcept {
        if (fd_in == g_fake.pipe_fds[0]) {
            ++g_fake.datagram_calls;
            g_fake.datagram_fd = fd_out;
            switch (g_fake.datagram_answer) {
            case Answer::Full:
                g_fake.datagram_total += len;
                return static_cast<long>(len);
            case Answer::ShortOnce:
                return static_cast<long>(len) - 4L;
            case Answer::EintrOnce:
                if (g_fake.datagram_eintr_pending) {
                    g_fake.datagram_eintr_pending = false;
                    return -EINTR;
                }
                g_fake.datagram_total += len;
                return static_cast<long>(len);
            case Answer::Fail:
                return -EIO;
            case Answer::Zero:
                return 0L;
            }
            return -EIO;
        }
        if (g_fake.span_calls < 4U) {
            g_fake.span_offsets[g_fake.span_calls] = (off_in != nullptr) ? *off_in : 0U;
        }
        ++g_fake.span_calls;
        switch (g_fake.span_answer) {
        case Answer::Full:
            g_fake.span_total += len;
            return static_cast<long>(len);
        case Answer::ShortOnce:
            if (g_fake.span_short_pending) {
                g_fake.span_short_pending = false;
                g_fake.span_total += 6U;
                return 6L;
            }
            g_fake.span_total += len;
            return static_cast<long>(len);
        case Answer::EintrOnce:
            if (g_fake.span_eintr_pending) {
                g_fake.span_eintr_pending = false;
                return -EINTR;
            }
            g_fake.span_total += len;
            return static_cast<long>(len);
        case Answer::Fail:
            return -EIO;
        case Answer::Zero:
            return 0L;
        }
        return -EIO;
    }

    long fake_send_datagram(int, const std::uint8_t *, std::size_t) noexcept {
        ++g_fake.send_calls;
        return -EIO;
    }

    /* The helper's block arrives here: the fake pipe holds pipe_left bytes, then end of file -- so
     * a short helper is a short read, and the fetch must refuse it. */
    long fake_read_pipe(int, std::uint8_t *out, std::size_t len) noexcept {
        ++g_fake.pipe_calls;
        if (g_fake.pipe_answer == Answer::Fail) {
            return -EIO;
        }
        if (g_fake.pipe_answer == Answer::EintrOnce && g_fake.pipe_eintr_pending) {
            g_fake.pipe_eintr_pending = false;
            return -EINTR;
        }
        const std::size_t give = len < g_fake.pipe_left ? len : g_fake.pipe_left;
        for (std::size_t i = 0U; i < give; ++i) {
            out[i] = static_cast<std::uint8_t>(g_fake.pipe_byte + i);
        }
        g_fake.pipe_left -= give;
        g_fake.pipe_total += give;
        return static_cast<long>(give);
    }

    long fake_read_at(int, std::uint8_t *out, std::size_t len, std::uint64_t offset) noexcept {
        if (g_fake.read_calls == 0U) {
            g_fake.read_first_offset = offset;
        }
        ++g_fake.read_calls;
        switch (g_fake.read_answer) {
        case Answer::Full:
            for (std::size_t i = 0U; i < len; ++i) out[i] = 0x77U;
            return static_cast<long>(len);
        case Answer::ShortOnce:
            if (g_fake.read_short_pending) {
                g_fake.read_short_pending = false;
                for (std::size_t i = 0U; i < len; ++i) out[i] = 0x77U;
                return 8L;
            }
            for (std::size_t i = 0U; i < len; ++i) out[i] = 0x77U;
            return static_cast<long>(len);
        case Answer::EintrOnce:
            if (g_fake.read_eintr_pending) {
                g_fake.read_eintr_pending = false;
                return -EINTR;
            }
            for (std::size_t i = 0U; i < len; ++i) out[i] = 0x77U;
            return static_cast<long>(len);
        case Answer::Fail:
            return -EIO;
        case Answer::Zero:
            return 0L;
        }
        return -EIO;
    }

    int fake_close_fd(int) noexcept {
        ++g_fake.close_calls;
        return 0;
    }

    SpliceIoOps fake_io() noexcept {
        return SpliceIoOps{&fake_pipe2, &fake_splice, &fake_vmsplice,
                           &fake_send_datagram, &fake_read_at, &fake_read_pipe, &fake_close_fd};
    }

    /** The injected bridge's record. */
    struct Bridge final {
        long read_result = 16L;
        std::size_t read_calls = 0U;
        std::uint64_t read_offset = 0U;
        long splice_result = 16L;
        std::size_t splice_calls = 0U;
        std::uint64_t splice_offset = 0U;
        int splice_pipe_fd = -1;
    };

    long bridge_read16(void *ctx, std::uint64_t offset, std::uint8_t out[kBlockBytes]) noexcept {
        auto &bridge = *static_cast<Bridge *>(ctx);
        ++bridge.read_calls;
        bridge.read_offset = offset;
        if (bridge.read_result == static_cast<long>(kBlockBytes)) {
            for (std::size_t i = 0U; i < kBlockBytes; ++i) out[i] = 0x5aU;
        }
        return bridge.read_result;
    }

    long bridge_splice16(void *ctx, int pipe_write_fd, std::uint64_t offset) noexcept {
        auto &bridge = *static_cast<Bridge *>(ctx);
        ++bridge.splice_calls;
        bridge.splice_offset = offset;
        bridge.splice_pipe_fd = pipe_write_fd;
        return bridge.splice_result;
    }

    /* A frame of the widest shape: header + vector + ciphertext = 40, then a sixteen-byte check. */
    constexpr std::size_t kFrameBytes = kHeaderBytes + 2U * kBlockBytes + kBlockBytes;

    std::array<std::uint8_t, kFrameBytes> make_frame() noexcept {
        std::array<std::uint8_t, kFrameBytes> frame{};
        for (std::size_t i = 0U; i < frame.size(); ++i) {
            frame[i] = static_cast<std::uint8_t>(i & 0xffU);
        }
        return frame;
    }

    void the_direct_descriptor_sends_one_datagram() {
        reset_fake();
        RealSpliceIo io(fake_io(), 7);
        const auto frame = make_frame();
        expect(io.splice_one(9, frame.data(), frame.size(), 0x40U), "the direct path sends");
        expect(g_fake.pipe2_calls == 1U, "one pipe");
        expect(g_fake.vmsplice_calls == 2U, "the two memory parts are mapped");
        expect(g_fake.vmsplice_total == frame.size() - kBlockBytes, "all but the page comes from memory");
        expect(g_fake.span_calls == 1U, "the page is spliced once");
        expect(g_fake.span_total == kBlockBytes, "and it is the sixteen-byte block");
        expect(g_fake.span_offsets[0] == 0x40U, "at the write offset");
        expect(g_fake.datagram_calls == 1U, "one datagram");
        expect(g_fake.datagram_total == frame.size(), "of the whole frame length");
        expect(g_fake.datagram_fd == 9, "on the socket it was given");
        expect(g_fake.send_calls == 0U, "the datagram entry is not the splice path");
        expect(g_fake.close_calls == 2U, "both pipe ends are closed");
    }

    void short_transfers_and_eintr_are_retried() {
        reset_fake();
        g_fake.vmsplice_answer = Answer::ShortOnce;
        g_fake.vmsplice_short_pending = true;
        g_fake.span_answer = Answer::ShortOnce;
        g_fake.span_short_pending = true;
        g_fake.datagram_answer = Answer::EintrOnce;
        g_fake.datagram_eintr_pending = true;
        g_fake.read_answer = Answer::ShortOnce;
        g_fake.read_short_pending = true;
        RealSpliceIo io(fake_io(), 7);
        const auto frame = make_frame();
        expect(io.splice_one(9, frame.data(), frame.size(), 0x40U), "the frame still goes out");
        expect(g_fake.vmsplice_calls == 3U, "the short mapping was retried");
        expect(g_fake.vmsplice_total == frame.size() - kBlockBytes, "and everything still got mapped");
        expect(g_fake.span_calls == 2U, "the short page splice was retried");
        expect(g_fake.span_total == kBlockBytes, "until the whole block moved");
        expect(g_fake.span_offsets[0] == 0x40U && g_fake.span_offsets[1] == 0x40U + 6U,
               "the retry advanced by what actually moved");
        expect(g_fake.datagram_calls == 2U, "-EINTR on the datagram splice was retried");
        expect(g_fake.datagram_total == frame.size(), "and then the whole datagram went");

        std::uint8_t old_block[kBlockBytes] = {};
        expect(io.read_at(7, old_block, kBlockBytes, 0x80U) == static_cast<long>(kBlockBytes),
               "a short read is retried to the whole block");
        expect(g_fake.read_calls == 2U, "in two calls");
        expect(g_fake.read_first_offset == 0x80U, "starting where it was asked to");
    }

    /** (a) One helper run supplies the block, and the datagram carries those same sixteen bytes:
     *  the preimage and the ciphertext are one fetch -- nothing runs the helper a second time. */
    void one_exec_yields_the_preimage_and_the_ciphertext() {
        reset_fake();
        Bridge bridge;
        RealSpliceIo io_bridge(fake_io(), -1, 0, OldPageSource{},
                               HelperWriteSource{&bridge, &bridge_splice16});
        std::uint8_t old_block[kBlockBytes] = {};
        expect(io_bridge.page_in_frame(), "with no descriptor the page travels in the frame");
        expect(io_bridge.fetch_block(-1, old_block, 0x40U) == static_cast<long>(kBlockBytes),
               "one helper run yields the block");
        expect(bridge.splice_calls == 1U && bridge.splice_offset == 0x40U,
               "the helper was asked for the block at the write offset");
        expect(bridge.splice_pipe_fd == g_fake.pipe_fds[1],
               "with its standard output on the pipe this process reads");
        expect(g_fake.pipe2_calls == 1U, "one pipe for that run");
        expect(g_fake.pipe_calls == 1U && g_fake.pipe_total == kBlockBytes,
               "and the whole block came back through it");
        expect(g_fake.close_calls == 2U, "both ends are given back");
        expect(old_block[0] == g_fake.pipe_byte, "the bytes are the helper's, not a constant");

        auto frame = make_frame();
        for (std::size_t i = 0U; i < kBlockBytes; ++i) {
            frame[kHeaderBytes + kBlockBytes + i] = old_block[i];
        }
        expect(io_bridge.splice_one(9, frame.data(), frame.size(), 0x40U), "the datagram goes out");
        expect(bridge.splice_calls == 1U, "without a second helper run");
        expect(g_fake.span_calls == 0U, "and with no page splice from a descriptor");
        expect(g_fake.vmsplice_calls == 3U, "header+vector, the page from the frame, and the check");
        expect(g_fake.vmsplice_lens[1] == kBlockBytes, "the page segment is one block");
        expect(bytes_equal(g_fake.vmsplice_data[1], old_block, kBlockBytes),
               "and it is exactly the block the one fetch returned");
        expect(g_fake.datagram_calls == 1U, "one datagram, of the whole frame");

        /* (c) three faults, three refusals: no short read ever becomes a datagram. */
        reset_fake();
        g_fake.pipe_left = 8U;
        Bridge slow_bridge;
        RealSpliceIo io_short_pipe(fake_io(), -1, 0, OldPageSource{},
                                   HelperWriteSource{&slow_bridge, &bridge_splice16});
        expect(io_short_pipe.fetch_block(-1, old_block, 0x40U) == 8L,
               "a short pipe read is a short fetch");

        reset_fake();
        Bridge short_bridge;
        short_bridge.splice_result = 8L;
        RealSpliceIo io_short_helper(fake_io(), -1, 0, OldPageSource{},
                                     HelperWriteSource{&short_bridge, &bridge_splice16});
        expect(io_short_helper.fetch_block(-1, old_block, 0x40U) == 8L,
               "a helper that does not move the block is a short fetch");
        expect(g_fake.pipe_calls == 0U, "and nothing is read from a helper that did not deliver");

        reset_fake();
        Bridge failed_bridge;
        failed_bridge.splice_result = -1L;
        RealSpliceIo io_failed_helper(fake_io(), -1, 0, OldPageSource{},
                                      HelperWriteSource{&failed_bridge, &bridge_splice16});
        expect(io_failed_helper.fetch_block(-1, old_block, 0x40U) == -1L,
               "a helper that failed is a failed fetch");
        expect(g_fake.pipe_calls == 0U, "with nothing read from its pipe either");
    }

    void both_sources_are_exercised() {
        const auto frame = make_frame();
        std::uint8_t old_block[kBlockBytes] = {};

        /* (b) the direct read fails: the bound source supplies the block. */
        reset_fake();
        g_fake.read_answer = Answer::Fail;
        Bridge reading;
        RealSpliceIo io_read(fake_io(), 7, 0, OldPageSource{&reading, &bridge_read16});
        expect(io_read.read_at(7, old_block, kBlockBytes, 0x80U) == static_cast<long>(kBlockBytes),
               "a failed direct read falls through to the source");
        expect(reading.read_calls == 1U && reading.read_offset == 0x80U, "which was asked for the block");
        expect(g_fake.read_calls == 1U, "and the descriptor was tried first");

        /* (b2) with a descriptor the fetch is the direct read; the page is spliced, not in the frame. */
        reset_fake();
        RealSpliceIo io_direct(fake_io(), 7);
        expect(!io_direct.page_in_frame(), "with a descriptor the page is spliced, not in the frame");
        expect(io_direct.fetch_block(7, old_block, 0x90U) == static_cast<long>(kBlockBytes),
               "and the fetch is the direct read");
        expect(g_fake.read_calls == 1U && g_fake.read_first_offset == 0x90U,
               "at the offset it was asked for");

        /* (d) no descriptor and no bridge: refused before anything is opened, and no fetch. */
        reset_fake();
        RealSpliceIo io_none(fake_io(), -1);
        expect(io_none.fetch_block(-1, old_block, 0U) == -EBADF, "no source is no block");
        expect(!io_none.splice_one(9, frame.data(), frame.size(), 0x40U), "no page source, no datagram");
        expect(g_fake.pipe2_calls == 0U, "and nothing is opened for it");
    }

    void a_failed_page_splice_fails_closed() {
        reset_fake();
        g_fake.span_answer = Answer::Fail;
        RealSpliceIo io(fake_io(), 7);
        const auto frame = make_frame();
        expect(!io.splice_one(9, frame.data(), frame.size(), 0x40U), "a failed page splice fails the block");
        expect(g_fake.datagram_calls == 0U, "and no datagram is sent");
    }

    void refusals() {
        reset_fake();
        RealSpliceIo io(fake_io(), 7);
        std::uint8_t short_frame[kHeaderBytes + 2U * kBlockBytes - 1U] = {};
        expect(!io.splice_one(9, short_frame, sizeof(short_frame), 0U),
               "a frame too short to hold the page is refused");
        expect(g_fake.pipe2_calls == 0U, "and nothing is opened for it");

        reset_fake();
        g_fake.datagram_answer = Answer::ShortOnce;
        const auto frame = make_frame();
        expect(!io.splice_one(9, frame.data(), frame.size(), 0U),
               "a short datagram splice fails the block");
        expect(g_fake.datagram_calls == 1U, "and is not retried (it cannot be appended)");
        expect(g_fake.close_calls == 2U, "the pipe is closed anyway");
    }

    void the_primitives_are_delegated() {
        std::uint8_t key[32] = {};
        std::uint8_t hmac_key[32] = {};
        std::uint8_t old_block[kBlockBytes] = {};
        for (std::size_t i = 0U; i < 32U; ++i) {
            key[i] = 0x11U;
            hmac_key[i] = 0x33U;
        }
        for (std::size_t i = 0U; i < kBlockBytes; ++i) {
            old_block[i] = 0x22U;
        }
        reset_fake();
        RealSpliceIo io(fake_io(), 7);

        std::uint8_t desired[kBlockBytes] = {};
        for (std::size_t i = 0U; i < kBlockBytes; ++i) {
            desired[i] = static_cast<std::uint8_t>(0x44U + i);
        }
        std::uint8_t got[kBlockBytes] = {};
        std::uint8_t want[kBlockBytes] = {};
        io.compute_iv(old_block, desired, key, got);
        static_cast<void>(esp_cbc_iv(old_block, desired, key, want));
        expect(bytes_equal(got, want, kBlockBytes), "the vector is the primitive's answer");
        std::uint8_t plain_vector[kBlockBytes] = {};
        io.compute_iv(old_block, old_block, key, plain_vector);
        bool differs = false;
        for (std::size_t i = 0U; i < kBlockBytes; ++i) {
            if (plain_vector[i] != got[i]) {
                differs = true;
            }
        }
        expect(differs, "and it changes with the block the caller wants there");

        std::uint8_t frame[kFrameBytes] = {};
        for (std::size_t i = 0U; i < sizeof(frame); ++i) {
            frame[i] = static_cast<std::uint8_t>(i);
        }
        std::uint8_t icv_got[kBlockBytes] = {};
        std::uint8_t icv_want[kBlockBytes] = {};
        io.compute_icv(frame, kHeaderBytes + 2U * kBlockBytes, hmac_key,
                       static_cast<std::uint8_t>(kBlockBytes), icv_got);
        static_cast<void>(esp_icv(frame, kHeaderBytes + 2U * kBlockBytes, hmac_key,
                                  static_cast<std::uint8_t>(kBlockBytes), icv_want, 32U));
        expect(bytes_equal(icv_got, icv_want, kBlockBytes), "the check is the primitive's answer");
    }
}  // namespace

int main() {
    std::printf("real splice io: the direct descriptor\n");
    the_direct_descriptor_sends_one_datagram();
    std::printf("real splice io: short transfers and -EINTR\n");
    short_transfers_and_eintr_are_retried();
    std::printf("real splice io: one exec for preimage and ciphertext\n");
    one_exec_yields_the_preimage_and_the_ciphertext();
    std::printf("real splice io: the two block sources\n");
    both_sources_are_exercised();
    std::printf("real splice io: a failed page splice\n");
    a_failed_page_splice_fails_closed();
    std::printf("real splice io: refusals\n");
    refusals();
    std::printf("real splice io: the primitives are delegated\n");
    the_primitives_are_delegated();
    if (g_failures != 0U) {
        std::printf("real splice io: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("real splice io: PASS (retries, sources, fail-closed, refusals)\n");
    return 0;
}
