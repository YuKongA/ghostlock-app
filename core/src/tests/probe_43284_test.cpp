/** @file tests/probe_43284_test.cpp
 *  The temporary probe, tested through the stream it actually reads.
 *
 *  The probe takes its document through stdio and its frame through descriptor zero, so these cases
 *  redirect descriptor zero to a pipe rather than hand it anything else. The document is written
 *  first and the frame second, because that is the order the app frames the stream and the reverse
 *  would be eaten as document bytes.
 *
 *  Three of the probe named paths are reachable on this build and are each exercised once: no
 *  document, a document with no frame, and a frame the reader refuses. The fourth, a build without
 *  the page cache backend, cannot be produced here because the backend is present, so it is not
 *  tested -- a case that cannot happen is not a case.
 */
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <unistd.h>

#include "backend/cve_2026_43284/session_frame.hpp"
#include "pipeline/probe_43284.cpp"

namespace {
    using ghostlock::backend::cve_2026_43284::encode_session_secret_frame;
    using ghostlock::backend::cve_2026_43284::SaMaterial;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** Run the probe with exactly these bytes on descriptor zero. Returns its exit code. */
    int run_probe(const std::uint8_t *document, std::size_t document_len, const std::uint8_t *frame,
                  std::size_t frame_len) {
        std::array<std::uint8_t, 64U> scratch{};
        /* The stdio stream was built at process start and keeps its own end-of-file flag, which the
         *  first case sets on a pipe that ends immediately. Redirecting descriptor zero does not clear
         *  it, so every case has to. */
        std::clearerr(stdin);
        int fds[2] = {-1, -1};
        if (::pipe(fds) != 0) {
            return -1;
        }
        const int saved = ::dup(0);
        if (::dup2(fds[0], 0) < 0) {
            return -1;
        }
        ::close(fds[0]);
        if (document_len != 0U) {
            const std::uint8_t prefix[4] = {static_cast<std::uint8_t>(document_len >> 24U),
                                            static_cast<std::uint8_t>(document_len >> 16U),
                                            static_cast<std::uint8_t>(document_len >> 8U),
                                            static_cast<std::uint8_t>(document_len)};
            static_cast<void>(::write(fds[1], prefix, sizeof(prefix)));
            static_cast<void>(::write(fds[1], document, document_len));
        }
        if (frame_len != 0U) {
            static_cast<void>(::write(fds[1], frame, frame_len));
        }
        ::close(fds[1]);
        const int code = ghostlock::pipeline::probe_43284_write_once(scratch.data(), scratch.size());
        static_cast<void>(::dup2(saved, 0));
        ::close(saved);
        return code;
    }

    void no_document_is_named() {
        expect(run_probe(nullptr, 0U, nullptr, 0U) == 4, "no document is reported as step four");
    }

    void no_frame_means_not_this_backend() {
        const std::uint8_t document[8] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
        /* An absent frame costs one poll timeout in the reader, which is why this case is slow. */
        expect(run_probe(document, sizeof(document), nullptr, 0U) == 3,
               "a document with no frame is not a 43284 run, and says so");
    }

    void a_refused_frame_is_named() {
        const std::uint8_t document[8] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
        SaMaterial sa;
        sa.spi = 1U;
        sa.encap_port = 4500U;
        sa.sender_port = 4501U;
        sa.icv_len = 16U;
        std::array<std::uint8_t, 84U> frame = encode_session_secret_frame(sa);
        frame[1] = 9U;   /* a kind the reader does not know */
        expect(run_probe(document, sizeof(document), frame.data(), frame.size()) == 5,
               "a frame the reader refuses stops the probe at step five");
    }
}  // namespace

int main() {
    std::printf("probe 43284: no document\n");
    no_document_is_named();
    std::printf("probe 43284: no frame\n");
    no_frame_means_not_this_backend();
    std::printf("probe 43284: a refused frame\n");
    a_refused_frame_is_named();
    if (g_failures != 0U) {
        std::printf("probe 43284: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("probe 43284: PASS (no document, no frame, refused frame)\n");
    return 0;
}
