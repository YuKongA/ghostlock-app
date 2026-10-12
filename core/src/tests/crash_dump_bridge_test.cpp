/** @file tests/crash_dump_bridge_test.cpp
 *  The bridge, against a helper this test writes itself.
 *
 *  What is pinned is the plumbing, because that is where a bridge goes wrong: the helper must get
 *  {offset, path, "r"} as argv in that order, its standard output must be the pipe the caller named
 *  (ours for a read, the caller's own for a splice), a short or failed helper must be refused rather
 *  than trusted, and a path that cannot survive being argv must be refused before anything runs.
 *  The fake helper prints the first sixteen bytes of its path argument, so the bytes that come back
 *  prove which argument the helper was handed.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "backend/cve_2026_43284/seam/crash_dump_bridge.hpp"

namespace {
    using ghostlock::backend::cve_2026_43284::seam::CrashDumpBridge;
    using ghostlock::backend::cve_2026_43284::seam::HelperWriteSource;
    using ghostlock::backend::cve_2026_43284::seam::kBlockBytes;
    using ghostlock::backend::cve_2026_43284::seam::OldPageSource;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** Write a script that prints the first sixteen bytes of its second argument, and make it
     *  executable. Returns its path, or an empty string. */
    std::string make_helper(const char *body) {
        char path[] = "/tmp/glk_helper_XXXXXX";
        const int fd = ::mkstemp(path);
        if (fd < 0) {
            return {};
        }
        const std::string text = std::string("#!/bin/sh\n") + body;
        const ssize_t written = ::write(fd, text.data(), text.size());
        (void) ::fchmod(fd, 0700);
        (void) ::close(fd);
        if (written != static_cast<ssize_t>(text.size())) {
            return {};
        }
        return std::string(path);
    }

    void a_block_comes_back_through_the_helper() {
        const std::string helper = make_helper("printf '%.16s' \"$2\"\n");
        expect(!helper.empty(), "the test wrote a helper");
        const char carrier[] = "/vendor/lib64/libbinderdebug.so";
        CrashDumpBridge bridge;
        expect(bridge.bind(helper.c_str(), helper.size(), carrier, std::strlen(carrier)),
               "the bridge binds a helper and a carrier");
        std::uint8_t out[kBlockBytes] = {};
        /* Through the surface a caller actually gets: the source the window is handed. */
        const OldPageSource source = bridge.old_page();
        const long got = source.read16(source.ctx, 0x1234U, out);
        expect(got == static_cast<long>(kBlockBytes), "one block comes back");
        expect(std::memcmp(out, carrier, kBlockBytes) == 0,
               "and it is the helper's answer for the path it was handed, so argv carried the path");
        expect(bridge.last_outcome() == CrashDumpBridge::Outcome::Ok, "with the outcome named ok");
        expect(bridge.last_status() == 0, "and the helper's status clean");
        (void) ::unlink(helper.c_str());
    }

    void the_splice_source_writes_into_the_callers_pipe() {
        const std::string helper = make_helper("printf '%.16s' \"$2\"\n");
        expect(!helper.empty(), "the test wrote a helper");
        const char carrier[] = "/vendor/lib64/abcdefghijklmnop";
        CrashDumpBridge bridge;
        static_cast<void>(bridge.bind(helper.c_str(), helper.size(), carrier, std::strlen(carrier)));
        int fds[2] = {-1, -1};
        expect(::pipe(fds) == 0, "the test made a pipe");
        const HelperWriteSource source = bridge.helper();
        const long spliced = source.splice16(source.ctx, fds[1], 0U);
        (void) ::close(fds[1]);
        std::uint8_t back[kBlockBytes] = {};
        std::size_t got = 0U;
        while (got < kBlockBytes) {
            const ssize_t n = ::read(fds[0], back + got, kBlockBytes - got);
            if (n <= 0) {
                break;
            }
            got += static_cast<std::size_t>(n);
        }
        (void) ::close(fds[0]);
        expect(spliced == static_cast<long>(kBlockBytes), "the splice reports a whole block");
        expect(got == kBlockBytes && std::memcmp(back, carrier, kBlockBytes) == 0,
               "and the bytes are in the caller's pipe, not in this process");
        (void) ::unlink(helper.c_str());
    }

    void what_cannot_be_trusted_is_refused() {
        const std::string good = make_helper("printf '%.16s' \"$2\"\n");
        const std::string failing = make_helper("exit 3\n");
        const std::string short_one = make_helper("printf 'abc'\n");
        expect(!good.empty() && !failing.empty() && !short_one.empty(), "the test wrote its helpers");
        const char carrier[] = "/vendor/lib64/libbinderdebug.so";

        CrashDumpBridge unbound;
        std::uint8_t out[kBlockBytes] = {};
        const OldPageSource unbound_source = unbound.old_page();
        expect(unbound_source.read16(unbound_source.ctx, 0U, out) == -1, "an unbound bridge refuses");
        expect(unbound.last_outcome() == CrashDumpBridge::Outcome::NotBound, "by name");

        CrashDumpBridge refused;
        expect(!refused.bind(nullptr, 4U, carrier, std::strlen(carrier)), "a missing helper is refused");
        expect(!refused.bind("h", 0U, carrier, std::strlen(carrier)), "an empty helper is refused");
        expect(!refused.bind("/tmp/has space", 13U, carrier, std::strlen(carrier)),
               "a helper whose path cannot be argv is refused before anything runs");
        expect(!refused.bind(good.c_str(), good.size(), "carrier with space", 18U),
               "and so is a carrier whose path cannot be argv");
        expect(refused.last_outcome() == CrashDumpBridge::Outcome::NotBound, "both by name");

        CrashDumpBridge failing_bridge;
        static_cast<void>(failing_bridge.bind(failing.c_str(), failing.size(), carrier,
                                              std::strlen(carrier)));
        const OldPageSource failing_source = failing_bridge.old_page();
        expect(failing_source.read16(failing_source.ctx, 0U, out) == -1,
               "a helper that fails is refused, not trusted");
        expect(failing_bridge.last_outcome() == CrashDumpBridge::Outcome::HelperFailed, "by name");

        CrashDumpBridge short_bridge;
        static_cast<void>(short_bridge.bind(short_one.c_str(), short_one.size(), carrier,
                                            std::strlen(carrier)));
        const OldPageSource short_source = short_bridge.old_page();
        expect(short_source.read16(short_source.ctx, 0U, out) == -1, "a short answer is refused too");
        expect(short_bridge.last_outcome() == CrashDumpBridge::Outcome::ShortRead, "and says so");
        (void) ::unlink(good.c_str());
        (void) ::unlink(failing.c_str());
        (void) ::unlink(short_one.c_str());
    }
}  // namespace

int main() {
    std::printf("crash dump bridge: a block through the helper\n");
    a_block_comes_back_through_the_helper();
    std::printf("crash dump bridge: the splice into the caller's pipe\n");
    the_splice_source_writes_into_the_callers_pipe();
    std::printf("crash dump bridge: what cannot be trusted\n");
    what_cannot_be_trusted_is_refused();
    if (g_failures != 0U) {
        std::printf("crash dump bridge: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("crash dump bridge: PASS (argv, pipe, refusals)\n");
    return 0;
}
