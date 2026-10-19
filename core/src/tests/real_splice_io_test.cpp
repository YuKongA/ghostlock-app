/** @file tests/real_splice_io_test.cpp
 *  The system side of the write, against a recording surface: no real call is made.
 *
 *  What is pinned: the retry rules the old implementation measured -- a short transfer is retried
 *  on the buffer, span and block-read writes, and -EINTR is retried everywhere without advancing --
 *  the one place a short transfer is not retried (the pipe-to-socket datagram splice, because it
 *  cannot be appended to the same datagram), both block sources (the direct descriptor and the
 *  injected bridge, each exercised; a bridge that cannot deliver a full block sends no datagram),
 *  the refusal of a frame too short to hold the page, that the helper path's diagnostic read is
 *  sampled rather than per block (and that the preimage still comes from the same source), and that
 *  the vector and the check are the primitives' answers rather than a second implementation.
 *
 *  It also pins the one-run helper path: with a surface that can tee, a block this process cannot
 *  read costs one helper run -- the page is teed for the preimage and then moved pipe to pipe into
 *  the datagram, so it never stops being the carrier's page -- a failed or short tee fails the
 *  block closed, a pending page for another offset fails it closed instead of falling back to the
 *  frame's own bytes, and a surface with no tee keeps the two helper runs it had.
 *
 *  It also pins the switch that turns the bridge from one exec per block into one session
 *  (bridge_session.hpp, default off): a normal answer, a short answer, a child that leaves, a child
 *  that goes quiet (the 200 ms deadline), an idempotent close, and -- the number the switch exists for
 *  -- how many helper children the two forms start for the same blocks. The fake helper is this test
 *  binary re-executed: the bridge execs its helper with the retired chain's argv, so the child is told
 *  which role to play by an environment variable the parent sets, and it records its pid in a ledger
 *  file so a fork count is a fact rather than an intention.
 */
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "backend/cve_2026_43284/landing/bridge_session.hpp"
#include "backend/cve_2026_43284/landing/crash_dump_bridge.hpp"
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

        Answer tee_answer = Answer::Full;
        bool tee_short_pending = false;
        bool tee_eintr_pending = false;
        std::size_t tee_calls = 0U;
        int tee_in_fd = -1;
        int tee_out_fd = -1;
        std::size_t tee_len = 0U;

        Answer read_pipe_answer = Answer::Full;
        bool read_pipe_short_pending = false;
        bool read_pipe_eintr_pending = false;
        std::size_t read_pipe_calls = 0U;
        int read_pipe_fd = -1;
        std::size_t read_pipe_len = 0U;

        /* Every pipe this fake hands out, in order: a test can say which pipe a splice or a tee
         * actually used instead of trusting a number. */
        static constexpr std::size_t kPipePairs = 8U;
        int pipe_pairs[kPipePairs][2] = {};
        std::size_t pipe_pairs_made = 0U;
        int next_pipe_fd = 41;

        std::size_t span_in_fds[4] = {};

        Answer read_answer = Answer::Full;
        bool read_short_pending = false;
        bool read_eintr_pending = false;
        std::size_t read_calls = 0U;
        std::uint64_t read_first_offset = 0U;

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
        /* A fresh pair per call: read_at's page pipe, its copy pipe and the send pipe must be
         * distinguishable, or the dispatch below would mistake one for another. */
        fds[0] = g_fake.next_pipe_fd;
        fds[1] = g_fake.next_pipe_fd + 1;
        g_fake.next_pipe_fd += 2;
        if (g_fake.pipe_pairs_made < Fake::kPipePairs) {
            g_fake.pipe_pairs[g_fake.pipe_pairs_made][0] = fds[0];
            g_fake.pipe_pairs[g_fake.pipe_pairs_made][1] = fds[1];
        }
        ++g_fake.pipe_pairs_made;
        g_fake.pipe_fds[0] = fds[0];
        g_fake.pipe_fds[1] = fds[1];
        return 0;
    }

    long fake_tee(int fd_in, int fd_out, std::size_t len, unsigned) noexcept {
        ++g_fake.tee_calls;
        g_fake.tee_in_fd = fd_in;
        g_fake.tee_out_fd = fd_out;
        g_fake.tee_len = len;
        switch (g_fake.tee_answer) {
        case Answer::Full:
            return static_cast<long>(len);
        case Answer::ShortOnce:
            if (g_fake.tee_short_pending) {
                g_fake.tee_short_pending = false;
                return 8L;
            }
            return static_cast<long>(len);
        case Answer::EintrOnce:
            if (g_fake.tee_eintr_pending) {
                g_fake.tee_eintr_pending = false;
                return -EINTR;
            }
            return static_cast<long>(len);
        case Answer::Fail:
            return -EIO;
        case Answer::Zero:
            return 0L;
        }
        return -EIO;
    }

    long fake_read_pipe(int fd, std::uint8_t *out, std::size_t len) noexcept {
        ++g_fake.read_pipe_calls;
        g_fake.read_pipe_fd = fd;
        g_fake.read_pipe_len = len;
        switch (g_fake.read_pipe_answer) {
        case Answer::Full:
            for (std::size_t i = 0U; i < len; ++i) out[i] = 0x5aU;
            return static_cast<long>(len);
        case Answer::ShortOnce:
            if (g_fake.read_pipe_short_pending) {
                g_fake.read_pipe_short_pending = false;
                for (std::size_t i = 0U; i < len; ++i) out[i] = 0x5aU;
                return 8L; /* eight, then EOF: the teed pipe held less than a block */
            }
            return 0L;
        case Answer::EintrOnce:
            if (g_fake.read_pipe_eintr_pending) {
                g_fake.read_pipe_eintr_pending = false;
                return -EINTR;
            }
            for (std::size_t i = 0U; i < len; ++i) out[i] = 0x5aU;
            return static_cast<long>(len);
        case Answer::Fail:
            return -EIO;
        case Answer::Zero:
            return 0L;
        }
        return -EIO;
    }

    long fake_vmsplice(int, const std::uint8_t *, std::size_t len, unsigned) noexcept {
        ++g_fake.vmsplice_calls;
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
            g_fake.span_in_fds[g_fake.span_calls] = static_cast<std::size_t>(fd_in);
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
                           &fake_send_datagram, &fake_read_at, &fake_close_fd};
    }

    /** The same surface with the two entries the one-run helper path needs. */
    SpliceIoOps fake_io_with_tee() noexcept {
        SpliceIoOps ops = fake_io();
        ops.tee = &fake_tee;
        ops.read_pipe = &fake_read_pipe;
        return ops;
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

    void both_sources_are_exercised() {
        const auto frame = make_frame();
        std::uint8_t old_block[kBlockBytes] = {};

        /* (a) no descriptor at all: the bridge reads the block and puts the page in the pipe. */
        reset_fake();
        Bridge bridge;
        RealSpliceIo io_bridge(fake_io(), -1, 0, OldPageSource{},
                               HelperWriteSource{&bridge, &bridge_splice16});
        expect(io_bridge.splice_one(9, frame.data(), frame.size(), 0x40U), "the bridge path sends");
        expect(bridge.splice_calls == 1U && bridge.splice_offset == 0x40U,
               "the helper was asked for the page at the offset");
        expect(g_fake.span_calls == 0U, "no descriptor, no page splice");
        expect(g_fake.datagram_calls == 1U, "and the datagram still went out");

        /* (b) the direct read fails: the bound source supplies the block. */
        reset_fake();
        g_fake.read_answer = Answer::Fail;
        Bridge reading;
        RealSpliceIo io_read(fake_io(), 7, 0, OldPageSource{&reading, &bridge_read16});
        expect(io_read.read_at(7, old_block, kBlockBytes, 0x80U) == static_cast<long>(kBlockBytes),
               "a failed direct read falls through to the source");
        expect(reading.read_calls == 1U && reading.read_offset == 0x80U, "which was asked for the block");
        expect(g_fake.read_calls == 1U, "and the descriptor was tried first");

        /* (c) a bridge that cannot deliver the full block sends no datagram. */
        reset_fake();
        Bridge short_bridge;
        short_bridge.splice_result = 8L;
        RealSpliceIo io_short(fake_io(), -1, 0, OldPageSource{},
                              HelperWriteSource{&short_bridge, &bridge_splice16});
        expect(!io_short.splice_one(9, frame.data(), frame.size(), 0x40U),
               "a short bridge delivery fails the block closed");
        expect(g_fake.datagram_calls == 0U, "and nothing is sent");
        expect(g_fake.close_calls == 2U, "the pipe is still given back");

        Bridge error_bridge;
        error_bridge.splice_result = -5L;
        RealSpliceIo io_error(fake_io(), -1, 0, OldPageSource{},
                              HelperWriteSource{&error_bridge, &bridge_splice16});
        expect(!io_error.splice_one(9, frame.data(), frame.size(), 0x40U),
               "a failing bridge fails the block closed too");

        /* (d) no descriptor and no bridge: refused before anything is opened. */
        reset_fake();
        RealSpliceIo io_none(fake_io(), -1);
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

    /** 探针的粒度，与 preimage 的来源：载体不可直读时，**一块只该有一次 helper 调用去送页**，
     *  诊断用的 read16 只在 512 字节的抽样点上出现；而向量所依赖的那次 preimage 读照旧按需发生
     *  （抽样掉的是诊断，不是 preimage）。 */
    void the_diagnostic_probe_is_sampled_not_per_block() {
        const auto frame = make_frame();
        reset_fake();
        Bridge bridge;
        RealSpliceIo io(fake_io(), -1, 0, OldPageSource{&bridge, &bridge_read16},
                        HelperWriteSource{&bridge, &bridge_splice16});
        expect(io.splice_one(9, frame.data(), frame.size(), 0x40U), "a block off the cadence still sends");
        expect(bridge.splice_calls == 1U, "with exactly one helper call for the page");
        expect(bridge.read_calls == 0U, "and no diagnostic read: the probe is not per block");
        expect(g_fake.span_calls == 0U, "no descriptor, no page splice");

        expect(io.splice_one(9, frame.data(), frame.size(), 0x200U), "a block on the 512-byte cadence sends");
        expect(bridge.splice_calls == 2U && bridge.read_calls == 1U,
               "and only the sampled block also reads the source, once");
        expect(g_fake.datagram_calls == 2U, "both datagrams went out");

        std::uint8_t old_block[kBlockBytes] = {};
        expect(io.read_at(-1, old_block, kBlockBytes, 0x80U) == static_cast<long>(kBlockBytes),
               "the preimage still comes back without the per-block probe");
        expect(bridge.read_calls == 2U && old_block[0] == 0x5aU && old_block[kBlockBytes - 1U] == 0x5aU,
               "from one helper read, with the source's own bytes");
    }

    /** 载体不可直读时一块只 fork 一次：read_at 让 helper 把**目标页**切进管道，tee 复制十六字节
     *  当 preimage（页不动），splice_one 再把同一页从管道移进发送管道。 */
    void the_helper_page_is_teed_once_per_block() {
        const auto frame = make_frame();
        reset_fake();
        Bridge bridge;
        RealSpliceIo io(fake_io_with_tee(), -1, 0, OldPageSource{&bridge, &bridge_read16},
                        HelperWriteSource{&bridge, &bridge_splice16});
        std::uint8_t old_block[kBlockBytes] = {};
        expect(io.read_at(-1, old_block, kBlockBytes, 0x40U) == static_cast<long>(kBlockBytes),
               "the helper page comes back as the whole block");
        expect(bridge.splice_calls == 1U && bridge.splice_offset == 0x40U,
               "one helper run, for the page at the offset asked");
        expect(bridge.splice_pipe_fd == g_fake.pipe_pairs[0][1],
               "the helper wrote into the page pipe this side opened");
        expect(bridge.read_calls == 0U, "and no second helper run to read the preimage");
        expect(g_fake.tee_calls == 1U && g_fake.tee_len == kBlockBytes,
               "the page was teed once, sixteen bytes");
        expect(g_fake.tee_in_fd == g_fake.pipe_pairs[0][0] &&
               g_fake.tee_out_fd == g_fake.pipe_pairs[1][1],
               "from the page pipe into a copy pipe, not out of this process's memory");
        expect(old_block[0] == 0x5aU && old_block[kBlockBytes - 1U] == 0x5aU,
               "the preimage is the page's own bytes");

        expect(io.splice_one(9, frame.data(), frame.size(), 0x40U), "the block still sends");
        expect(bridge.splice_calls == 1U, "and the send ran no helper: one fork for the whole block");
        expect(g_fake.span_calls == 1U &&
               g_fake.span_in_fds[0] == static_cast<std::size_t>(g_fake.pipe_pairs[0][0]),
               "the page moved from its pipe into the send pipe");
        expect(g_fake.datagram_calls == 1U, "one datagram");
        expect(g_fake.datagram_total == frame.size(), "of the whole frame length");
        expect(g_fake.datagram_fd == 9, "on the socket it was given");

        /* 页被消费掉之后 pending 槽必须清空：下一块没有 read_at 时只能由 helper 自己送页。 */
        expect(io.splice_one(9, frame.data(), frame.size(), 0x50U),
               "a later block with no pending page still sends");
        expect(bridge.splice_calls == 2U, "through the helper itself: the pending page was consumed");
    }

    /** tee 失败或短读 ⇒ fail-closed：这一块没有 preimage，也就没有报文。 */
    void a_failed_tee_fails_the_block_closed() {
        const auto frame = make_frame();
        std::uint8_t old_block[kBlockBytes] = {};

        reset_fake();
        g_fake.tee_answer = Answer::Fail;
        Bridge bridge;
        RealSpliceIo io(fake_io_with_tee(), -1, 0, OldPageSource{&bridge, &bridge_read16},
                        HelperWriteSource{&bridge, &bridge_splice16});
        expect(io.read_at(-1, old_block, kBlockBytes, 0x40U) < 0L, "a failed tee fails the read");
        expect(g_fake.datagram_calls == 0U, "and no datagram is sent for that block");
        expect(g_fake.close_calls == 4U, "every pipe the path opened is given back");
        expect(io.splice_one(9, frame.data(), frame.size(), 0x40U),
               "a block with no pending page still sends");
        expect(bridge.splice_calls == 2U, "through a fresh helper run, not a stale page");

        reset_fake();
        g_fake.tee_answer = Answer::ShortOnce;
        g_fake.tee_short_pending = true;
        Bridge short_bridge;
        RealSpliceIo short_io(fake_io_with_tee(), -1, 0,
                              OldPageSource{&short_bridge, &bridge_read16},
                              HelperWriteSource{&short_bridge, &bridge_splice16});
        expect(short_io.read_at(-1, old_block, kBlockBytes, 0x40U) == 8L, "a short tee is a short read");
        expect(g_fake.datagram_calls == 0U, "and nothing is sent");

        reset_fake();
        g_fake.read_pipe_answer = Answer::ShortOnce;
        g_fake.read_pipe_short_pending = true;
        Bridge eof_bridge;
        RealSpliceIo eof_io(fake_io_with_tee(), -1, 0,
                            OldPageSource{&eof_bridge, &bridge_read16},
                            HelperWriteSource{&eof_bridge, &bridge_splice16});
        expect(eof_io.read_at(-1, old_block, kBlockBytes, 0x40U) == 8L,
               "a copy that ends after eight bytes is a short block, not data");
        expect(g_fake.datagram_calls == 0U, "and it is not sent either");
    }

    /** pending 页的 offset 与要发的块不符 ⇒ fail-closed，绝不拿报文里的字节顶替页。 */
    void a_pending_page_for_another_offset_fails_closed() {
        const auto frame = make_frame();
        reset_fake();
        Bridge bridge;
        RealSpliceIo io(fake_io_with_tee(), -1, 0, OldPageSource{&bridge, &bridge_read16},
                        HelperWriteSource{&bridge, &bridge_splice16});
        std::uint8_t old_block[kBlockBytes] = {};
        expect(io.read_at(-1, old_block, kBlockBytes, 0x40U) == static_cast<long>(kBlockBytes),
               "a block is read and its page is pending");
        const std::size_t closes_before = g_fake.close_calls;
        expect(!io.splice_one(9, frame.data(), frame.size(), 0x50U),
               "a pending page for another offset fails the block closed");
        expect(g_fake.datagram_calls == 0U, "and nothing is sent");
        expect(bridge.splice_calls == 1U, "no fallback page is produced for the other offset");
        expect(g_fake.close_calls == closes_before + 1U, "the pending pipe is given back");
        expect(io.last_error() == 5, "and the refusal names the pending mismatch");
        expect(io.splice_one(9, frame.data(), frame.size(), 0x50U),
               "the next attempt has no pending page and sends");
        expect(bridge.splice_calls == 2U, "through the helper");
    }

    /** fd >= 0 的路径不碰 tee：直读照旧，页仍从描述符切。 */
    void the_descriptor_path_never_tees() {
        const auto frame = make_frame();
        reset_fake();
        RealSpliceIo io(fake_io_with_tee(), 7);
        std::uint8_t old_block[kBlockBytes] = {};
        expect(io.read_at(7, old_block, kBlockBytes, 0x40U) == static_cast<long>(kBlockBytes),
               "a readable descriptor still reads directly");
        expect(g_fake.tee_calls == 0U && g_fake.read_pipe_calls == 0U,
               "the tee path is not taken when the carrier can be read");
        expect(io.splice_one(9, frame.data(), frame.size(), 0x40U), "the block still sends");
        expect(g_fake.span_calls == 1U && g_fake.span_in_fds[0] == 7U,
               "and the page still comes from the descriptor");
        expect(g_fake.datagram_calls == 1U, "one datagram");
    }

    /** 面没有 tee：载体不可直读时仍是两次 helper（先读 preimage，再送页），行为不变。 */
    void without_tee_the_two_helper_runs_stay() {
        const auto frame = make_frame();
        reset_fake();
        Bridge bridge;
        RealSpliceIo io(fake_io(), -1, 0, OldPageSource{&bridge, &bridge_read16},
                        HelperWriteSource{&bridge, &bridge_splice16});
        std::uint8_t old_block[kBlockBytes] = {};
        expect(io.read_at(-1, old_block, kBlockBytes, 0x40U) == static_cast<long>(kBlockBytes),
               "the preimage still comes from the source read");
        expect(bridge.read_calls == 1U && bridge.splice_calls == 0U,
               "one helper run for the read");
        expect(g_fake.tee_calls == 0U && g_fake.read_pipe_calls == 0U,
               "no tee entry is used on a surface that has none");
        expect(io.splice_one(9, frame.data(), frame.size(), 0x40U), "and the block still sends");
        expect(bridge.splice_calls == 1U, "with a second helper run for the page");
        expect(g_fake.datagram_calls == 1U, "one datagram");

        /* 面能 tee，但没有 helper（没有桥）：没有页管道可复制，仍走原来的 read16 来源。 */
        reset_fake();
        Bridge read_only;
        RealSpliceIo io_read_only(fake_io_with_tee(), -1, 0,
                                  OldPageSource{&read_only, &bridge_read16});
        expect(io_read_only.read_at(-1, old_block, kBlockBytes, 0x40U) ==
               static_cast<long>(kBlockBytes),
               "without a helper the source read still serves the preimage");
        expect(read_only.read_calls == 1U, "one source read");
        expect(g_fake.tee_calls == 0U, "and no tee: there is no page pipe to copy");
    }


    /* ------------------------------------------------------------------ *
     * The persistent bridge: the fake helper, and the switch that selects it.
     * ------------------------------------------------------------------ */

    using ghostlock::backend::cve_2026_43284::landing::BridgeSession;
    using ghostlock::backend::cve_2026_43284::landing::CrashDumpBridge;
    using ghostlock::backend::cve_2026_43284::landing::kBridgeModeSession;
    using ghostlock::backend::cve_2026_43284::landing::kBridgeModeSpawn;
    using ghostlock::backend::cve_2026_43284::landing::kBridgeReplyBytes;
    using ghostlock::backend::cve_2026_43284::landing::kBridgeRequestBytes;
    using ghostlock::backend::cve_2026_43284::landing::kBridgeSessionMagic;

    /** Where a fake child records that it ran: one pid per line, so "how many helpers did this cost"
     *  is counted rather than assumed. */
    constexpr const char *kLedgerEnv = "GLK_TEST_BRIDGE_LEDGER";
    /** Which behaviour the fake child plays (normal / short / exit / silent). */
    constexpr const char *kBehaviourEnv = "GLK_TEST_BRIDGE_BEHAVIOUR";
    constexpr const char *kFakeCarrier = "/vendor/lib64/libbinderdebug.so";

    std::string g_ledger;

    void store_le32_at(std::uint8_t *out, std::uint32_t value) {
        for (std::size_t i = 0U; i < 4U; ++i) {
            out[i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xffU);
        }
    }

    void store_le64_at(std::uint8_t *out, std::uint64_t value) {
        for (std::size_t i = 0U; i < 8U; ++i) {
            out[i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xffULL);
        }
    }

    std::uint64_t load_le64_at(const std::uint8_t *in) {
        std::uint64_t value = 0ULL;
        for (std::size_t i = 0U; i < 8U; ++i) {
            value |= static_cast<std::uint64_t>(in[i]) << (8U * i);
        }
        return value;
    }

    /** The block the fake answers with at an offset: a pattern that proves the offset travelled. */
    void fake_block(std::uint64_t offset, std::uint8_t *out) {
        for (std::size_t i = 0U; i < kBlockBytes; ++i) {
            out[i] = static_cast<std::uint8_t>((offset + i) & 0xffULL);
        }
    }

    bool write_all(int fd, const std::uint8_t *data, std::size_t size) {
        std::size_t done = 0U;
        while (done < size) {
            const ssize_t wrote = ::write(fd, data + done, size - done);
            if (wrote <= 0) {
                return false;
            }
            done += static_cast<std::size_t>(wrote);
        }
        return true;
    }

    void record_child() {
        const char *path = std::getenv(kLedgerEnv);
        if (path == nullptr) {
            return;
        }
        const int fd = ::open(path, O_WRONLY | O_APPEND | O_CREAT, 0600);
        if (fd < 0) {
            return;
        }
        char line[32] = {};
        const int length = std::snprintf(line, sizeof(line), "%d\n", static_cast<int>(::getpid()));
        if (length > 0) {
            (void) write_all(fd, reinterpret_cast<const std::uint8_t *>(line),
                             static_cast<std::size_t>(length));
        }
        (void) ::close(fd);
    }

    /** Reads one request off the session's standard input, with the descriptor a splice rides on. */
    bool read_request(int fd, std::uint8_t &op, std::uint64_t &offset, int &passed_fd) {
        std::uint8_t message[kBridgeRequestBytes] = {};
        char control[CMSG_SPACE(sizeof(int))] = {};
        struct iovec body {};
        body.iov_base = message;
        body.iov_len = sizeof(message);
        struct msghdr headers {};
        headers.msg_iov = &body;
        headers.msg_iovlen = 1U;
        headers.msg_control = control;
        headers.msg_controllen = sizeof(control);
        for (;;) {
            const ssize_t got = ::recvmsg(fd, &headers, 0);
            if (got < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
            if (got != static_cast<ssize_t>(kBridgeRequestBytes)) {
                return false;
            }
            break;
        }
        op = message[0];
        offset = load_le64_at(message + 8);
        passed_fd = -1;
        for (struct cmsghdr *header = CMSG_FIRSTHDR(&headers); header != nullptr;
             header = CMSG_NXTHDR(&headers, header)) {
            if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS &&
                header->cmsg_len >= CMSG_LEN(sizeof(int))) {
                std::memcpy(&passed_fd, CMSG_DATA(header), sizeof(int));
            }
        }
        return true;
    }

    /** The child side of the protocol, as the session helper is expected to speak it. */
    int run_fake_session_child() {
        record_child();
        const char *behaviour = std::getenv(kBehaviourEnv);
        const bool silent = behaviour != nullptr && std::strcmp(behaviour, "silent") == 0;
        const bool short_answer = behaviour != nullptr && std::strcmp(behaviour, "short") == 0;
        const bool leaves_early = behaviour != nullptr && std::strcmp(behaviour, "exit") == 0;
        for (;;) {
            std::uint8_t op = 0U;
            std::uint64_t offset = 0U;
            int passed_fd = -1;
            if (!read_request(STDIN_FILENO, op, offset, passed_fd)) {
                return 0;
            }
            if (op == 0x48U) { /* the handshake */
                std::uint8_t reply[kBridgeReplyBytes] = {};
                store_le32_at(reply, 0U);
                reply[4] = op;
                store_le64_at(reply + 8, kBridgeSessionMagic);
                if (!write_all(STDOUT_FILENO, reply, kBridgeReplyBytes)) {
                    return 0;
                }
                if (leaves_early) {
                    return 0; /* the child that leaves right after introducing itself */
                }
                continue;
            }
            if (silent) {
                continue; /* read the request, answer nothing: the parent's deadline is the test */
            }
            if (short_answer && op == 0x52U) {
                const std::uint8_t half[kBridgeReplyBytes] = {};
                (void) write_all(STDOUT_FILENO, half, 8U);
                return 0; /* half a reply and gone: a short read, not data */
            }
            if (op == 0x52U) {
                std::uint8_t block[kBlockBytes] = {};
                fake_block(offset, block);
                if (!write_all(STDOUT_FILENO, block, kBlockBytes)) {
                    return 0;
                }
                continue;
            }
            if (op == 0x53U) {
                bool moved = false;
                if (passed_fd >= 0) {
                    std::uint8_t page[kBlockBytes] = {};
                    fake_block(offset, page);
                    moved = write_all(passed_fd, page, kBlockBytes);
                    (void) ::close(passed_fd);
                }
                std::uint8_t status[kBridgeReplyBytes] = {};
                store_le32_at(status, moved ? 0U : 2U);
                status[4] = op;
                store_le64_at(status + 8, offset);
                if (!write_all(STDOUT_FILENO, status, kBridgeReplyBytes)) {
                    return 0;
                }
                continue;
            }
            return 0; /* an op the helper does not know is the end of the session */
        }
    }

    /** The one-shot helper, which is what the spawn form execs: sixteen bytes on standard output. */
    int run_fake_one_shot() {
        record_child();
        std::uint8_t block[kBlockBytes] = {};
        for (std::size_t i = 0U; i < kBlockBytes; ++i) {
            block[i] = 0x5aU;
        }
        (void) write_all(STDOUT_FILENO, block, kBlockBytes);
        return 0;
    }

    /** This binary's own path: the fake helper is this test, re-executed. */
    std::string own_exe() {
#if defined(__APPLE__)
        char raw[4096] = {};
        std::uint32_t size = sizeof(raw);
        if (_NSGetExecutablePath(raw, &size) != 0) {
            return {};
        }
        char resolved[4096] = {};
        return ::realpath(raw, resolved) != nullptr ? std::string(resolved) : std::string(raw);
#else
        char raw[4096] = {};
        const ssize_t got = ::readlink("/proc/self/exe", raw, sizeof(raw) - 1U);
        if (got <= 0) {
            return {};
        }
        return std::string(raw, static_cast<std::size_t>(got));
#endif
    }

    /** A fresh ledger and the behaviour for it: every case starts from a known child count. */
    void reset_ledger(const char *behaviour) {
        char path[] = "/tmp/glk_bridge_ledger_XXXXXX";
        const int fd = ::mkstemp(path);
        if (fd >= 0) {
            (void) ::close(fd);
            g_ledger = path;
            (void) ::setenv(kLedgerEnv, g_ledger.c_str(), 1);
        }
        if (behaviour != nullptr) {
            (void) ::setenv(kBehaviourEnv, behaviour, 1);
        } else {
            (void) ::unsetenv(kBehaviourEnv);
        }
    }

    std::size_t ledger_children() {
        std::size_t lines = 0U;
        if (g_ledger.empty()) {
            return lines;
        }
        std::FILE *file = std::fopen(g_ledger.c_str(), "r");
        if (file == nullptr) {
            return lines;
        }
        int ch = 0;
        while ((ch = std::fgetc(file)) != EOF) {
            if (ch == '\n') {
                ++lines;
            }
        }
        (void) std::fclose(file);
        return lines;
    }

    std::int64_t elapsed_ms(std::int64_t started_us, std::int64_t ended_us) {
        return (ended_us - started_us) / 1000;
    }

    std::int64_t now_us_test() {
        timespec stamp{};
        if (::clock_gettime(CLOCK_MONOTONIC, &stamp) != 0) {
            return 0;
        }
        return static_cast<std::int64_t>(stamp.tv_sec) * 1000000 +
               static_cast<std::int64_t>(stamp.tv_nsec) / 1000;
    }

    /** A block, a page and the handshake over one child -- and the count that proves it. */
    void the_session_carries_blocks_over_one_child() {
        reset_ledger(nullptr);
        const std::string exe = own_exe();
        expect(!exe.empty(), "the test knows its own path");
        expect(exe.find(' ') == std::string::npos, "and the path can be argv, as the bridge requires");
        CrashDumpBridge bridge;
        expect(bridge.bind(exe.c_str(), exe.size(), kFakeCarrier, std::strlen(kFakeCarrier),
                           kBridgeModeSession),
               "the bridge binds in session mode");
        expect(bridge.bridge_mode() == kBridgeModeSession, "and says which form it is in");
        std::uint8_t first[kBlockBytes] = {};
        const OldPageSource source = bridge.old_page();
        expect(source.read16(source.ctx, 0x10U, first) == static_cast<long>(kBlockBytes),
               "a block comes back through the session");
        std::uint8_t want[kBlockBytes] = {};
        fake_block(0x10U, want);
        expect(bytes_equal(first, want, kBlockBytes), "and it is the answer for the offset asked");

        int fds[2] = {-1, -1};
        expect(::pipe(fds) == 0, "the test made a pipe");
        const HelperWriteSource helper = bridge.helper();
        const long spliced = helper.splice16(helper.ctx, fds[1], 0x20U);
        (void) ::close(fds[1]);
        std::uint8_t page[kBlockBytes] = {};
        std::size_t got = 0U;
        while (got < kBlockBytes) {
            const ssize_t read_bytes = ::read(fds[0], page + got, kBlockBytes - got);
            if (read_bytes <= 0) {
                break;
            }
            got += static_cast<std::size_t>(read_bytes);
        }
        (void) ::close(fds[0]);
        expect(spliced == static_cast<long>(kBlockBytes), "the page is reported as a whole block");
        fake_block(0x20U, want);
        expect(got == kBlockBytes && bytes_equal(page, want, kBlockBytes),
               "and the child put it in the caller's pipe, not in this process");
        expect(bridge.session_open(), "the session is up");
        expect(bridge.session_requests() == 3U, "three requests: the handshake and the two blocks");
        expect(ledger_children() == 1U, "one helper child for both blocks, not one per block");
        bridge.close_session();
        expect(!bridge.session_open(), "closing ends the session");
        expect(bridge.session_pid() < 0, "and reaps the child");
    }

    /** A reply that is not a whole answer must not be taken for data. */
    void a_short_reply_fails_the_session_closed() {
        reset_ledger("short");
        const std::string exe = own_exe();
        BridgeSession session;
        expect(session.bind(exe.c_str(), exe.size(), kFakeCarrier, std::strlen(kFakeCarrier)),
               "the session binds");
        expect(session.open(), "a session helper answers the handshake");
        std::uint8_t block[kBlockBytes] = {};
        expect(session.read16(0U, block) == -1, "half a reply is not a block");
        expect(session.last_outcome() == BridgeSession::Outcome::ShortReply, "and it says which fault");
        expect(session.dead(), "and the session is latched dead");
        expect(session.read16(0x10U, block) == -1, "so the next block fails too, without being asked");
        expect(ledger_children() == 1U, "nothing restarted behind it");
        session.close();
    }

    /** A child that leaves early ends the run of blocks, not just the one that found it gone. */
    void a_child_that_leaves_fails_every_later_block() {
        reset_ledger("exit");
        const std::string exe = own_exe();
        CrashDumpBridge bridge;
        static_cast<void>(bridge.bind(exe.c_str(), exe.size(), kFakeCarrier,
                                      std::strlen(kFakeCarrier), kBridgeModeSession));
        std::uint8_t block[kBlockBytes] = {};
        const OldPageSource source = bridge.old_page();
        expect(source.read16(source.ctx, 0U, block) == -1, "the block that found the child gone fails");
        expect(bridge.last_outcome() == CrashDumpBridge::Outcome::HelperFailed, "and is named a helper failure");
        expect(source.read16(source.ctx, 0x10U, block) == -1, "and so does every block after it");
        expect(source.read16(source.ctx, 0x20U, block) == -1, "all of them, not the next one only");
        expect(ledger_children() == 1U, "with no second child started to replace it");
        bridge.close_session();
    }

    /** Silence is a failure inside the deadline, and the deadline is 200 ms. */
    void a_quiet_child_times_out() {
        reset_ledger("silent");
        const std::string exe = own_exe();
        BridgeSession session;
        static_cast<void>(session.bind(exe.c_str(), exe.size(), kFakeCarrier,
                                       std::strlen(kFakeCarrier)));
        expect(session.open(), "the handshake still went through");
        std::uint8_t block[kBlockBytes] = {};
        const std::int64_t started = now_us_test();
        expect(session.read16(0U, block) == -1, "a request nobody answers fails");
        const std::int64_t waited = elapsed_ms(started, now_us_test());
        expect(waited >= 150, "after roughly the 200 ms deadline");
        expect(waited < 2000, "and not after a hang");
        expect(session.last_outcome() == BridgeSession::Outcome::TimedOut, "named a timeout");
        const std::int64_t again = now_us_test();
        expect(session.read16(0x10U, block) == -1, "and the next block fails at once");
        expect(elapsed_ms(again, now_us_test()) < 150, "because the session is already latched");
        session.close();
    }

    /** Closing twice is closing once, and the second call does not touch the child. */
    void close_is_idempotent() {
        reset_ledger(nullptr);
        const std::string exe = own_exe();
        BridgeSession session;
        static_cast<void>(session.bind(exe.c_str(), exe.size(), kFakeCarrier,
                                       std::strlen(kFakeCarrier)));
        expect(session.open(), "the session opens");
        const pid_t pid = session.pid();
        expect(pid > 0, "with a child");
        const std::size_t requests = session.requests();
        session.close();
        expect(!session.is_open(), "closing closes");
        expect(session.pid() < 0, "and reaps");
        expect(session.request_fd() < 0 && session.reply_fd() < 0, "and gives the channels back");
        session.close();
        session.close();
        expect(session.requests() == requests, "a second close asks the child nothing");
        expect(session.pid() < 0, "and has nothing left to reap");
        expect(ledger_children() == 1U, "one child ran, and it ran once");
    }

    /** The default form: no session is ever created, and the count says so. */
    void the_default_mode_starts_no_session() {
        reset_ledger(nullptr);
        const std::string exe = own_exe();
        CrashDumpBridge bridge;
        expect(bridge.bind(exe.c_str(), exe.size(), kFakeCarrier, std::strlen(kFakeCarrier)),
               "the bridge binds with the default form");
        expect(bridge.bridge_mode() == kBridgeModeSpawn, "which is spawn, as it is today");
        std::uint8_t block[kBlockBytes] = {};
        const OldPageSource source = bridge.old_page();
        expect(source.read16(source.ctx, 0U, block) == static_cast<long>(kBlockBytes),
               "a block still comes back, byte for byte as before");
        expect(source.read16(source.ctx, 0x10U, block) == static_cast<long>(kBlockBytes),
               "and so does the next one");
        expect(!bridge.session_open(), "no session was opened");
        expect(bridge.session_pid() < 0, "no session child exists");
        expect(bridge.session_requests() == 0U, "and nothing was asked of one");
        expect(ledger_children() == 2U, "one helper child per block: the cost the switch removes");
    }

    /** The surface's half of the switch: in session mode the diagnostic read is per block, because it
     *  is an IPC rather than a fork. The default keeps the sampling the device measurement asked for. */
    void the_surface_reads_its_bridge_form() {
        const auto frame = make_frame();
        SpliceIoOps session_ops = fake_io();
        session_ops.bridge_mode = kBridgeModeSession;
        reset_fake();
        Bridge probed;
        RealSpliceIo with_session(session_ops, -1, 0, OldPageSource{&probed, &bridge_read16},
                                  HelperWriteSource{&probed, &bridge_splice16});
        expect(with_session.bridge_mode() == kBridgeModeSession, "the surface reports the form it was built with");
        expect(with_session.splice_one(9, frame.data(), frame.size(), 0x40U), "and still sends the block");
        expect(probed.splice_calls == 1U, "one page from the bridge");
        expect(probed.read_calls == 1U, "and one diagnostic read, though 0x40 is off the 512-byte cadence");

        reset_fake();
        Bridge sampled;
        RealSpliceIo as_before(fake_io(), -1, 0, OldPageSource{&sampled, &bridge_read16},
                               HelperWriteSource{&sampled, &bridge_splice16});
        expect(as_before.bridge_mode() == kBridgeModeSpawn, "the default surface is spawn");
        expect(as_before.splice_one(9, frame.data(), frame.size(), 0x40U), "and sends the block too");
        expect(sampled.read_calls == 0U, "with no probe off the cadence, exactly as before");
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

int main(int argc, char **argv) {
    /* 本测试同时充当桥的子进程（假 helper）：桥的 exec 用的是一次性 helper 的 argv 形状，所以子进程
     * 靠父进程设的环境变量认出自己，而不是靠 argv[0]（桥把它写成 crashdump64）。 */
    if (std::getenv(kLedgerEnv) != nullptr) {
        if (argc == 4 && std::strcmp(argv[3], "session") == 0) {
            return run_fake_session_child();
        }
        if (argc == 3) {
            return run_fake_one_shot();
        }
    }
    std::printf("real splice io: the direct descriptor\n");
    the_direct_descriptor_sends_one_datagram();
    std::printf("real splice io: short transfers and -EINTR\n");
    short_transfers_and_eintr_are_retried();
    std::printf("real splice io: the two block sources\n");
    both_sources_are_exercised();
    std::printf("real splice io: a failed page splice\n");
    a_failed_page_splice_fails_closed();
    std::printf("real splice io: refusals\n");
    refusals();
    std::printf("real splice io: the sampled probe and the preimage\n");
    the_diagnostic_probe_is_sampled_not_per_block();
    std::printf("real splice io: one helper run per block (tee)\n");
    the_helper_page_is_teed_once_per_block();
    a_failed_tee_fails_the_block_closed();
    a_pending_page_for_another_offset_fails_closed();
    the_descriptor_path_never_tees();
    without_tee_the_two_helper_runs_stay();
    std::printf("real splice io: the persistent bridge session\n");
    the_session_carries_blocks_over_one_child();
    a_short_reply_fails_the_session_closed();
    a_child_that_leaves_fails_every_later_block();
    a_quiet_child_times_out();
    close_is_idempotent();
    std::printf("real splice io: the switch that selects it\n");
    the_default_mode_starts_no_session();
    the_surface_reads_its_bridge_form();
    std::printf("real splice io: the primitives are delegated\n");
    the_primitives_are_delegated();
    if (g_failures != 0U) {
        std::printf("real splice io: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("real splice io: PASS (retries, sources, fail-closed, refusals)\n");
    return 0;
}
