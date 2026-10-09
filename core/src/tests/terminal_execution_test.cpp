/** @file tests/terminal_execution_test.cpp
 *  A6-4 evidence: the declarative finishing.
 *
 *  Sections: 1. which moments run, for both outcomes and including the moments
 *  the plan left empty; 2. a payload really runs and its status is reported;
 *  3. what cannot be run is refused by name.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include <sys/stat.h>
#include <unistd.h>

#include "terminal/execution.hpp"
#include "terminal/payload_store.hpp"

namespace {
    using ghostlock::terminal::declared_payload;
    using ghostlock::terminal::ExecResult;
    using ghostlock::terminal::FinishDeclaration;
    using ghostlock::terminal::FinishMoment;
    using ghostlock::terminal::finish_moment_name;
    using ghostlock::terminal::moment_runs;
    using ghostlock::terminal::remove_payload;
    using ghostlock::terminal::run_payload;
    using ghostlock::terminal::store_payload;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /* ---- 1: the declaration decides, and an empty moment never runs ---- */
    void the_declaration_decides() {
        FinishDeclaration full{};
        full.on_success = "success.bin";
        full.always = "always.bin";
        full.on_failure = "failure.bin";
        expect(moment_runs(full, FinishMoment::OnSuccess, true), "on_success runs on success");
        expect(!moment_runs(full, FinishMoment::OnSuccess, false),
               "on_success does not run on failure");
        expect(moment_runs(full, FinishMoment::OnFailure, false), "on_failure runs on failure");
        expect(!moment_runs(full, FinishMoment::OnFailure, true),
               "on_failure does not run on success");
        expect(moment_runs(full, FinishMoment::Always, true) &&
                   moment_runs(full, FinishMoment::Always, false),
               "always runs either way");
        expect(std::string_view(finish_moment_name(FinishMoment::OnFailure)) ==
                   std::string_view("on_failure"),
               "the moments keep the names the plan uses");

        FinishDeclaration empty{};
        expect(!moment_runs(empty, FinishMoment::OnSuccess, true) &&
                   !moment_runs(empty, FinishMoment::Always, true) &&
                   !moment_runs(empty, FinishMoment::OnFailure, false),
               "a moment with no payload never runs");
        expect(declared_payload(empty, FinishMoment::Always) == nullptr,
               "an empty moment declares nothing");
        expect(declared_payload(full, FinishMoment::Always) != nullptr,
               "a declared moment names its payload");
    }

    /* ---- 2: a payload runs and reports its status ---- */
    void a_payload_runs(const char *directory) {
        const char script[] = "#!/bin/sh\nexit 7\n";
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(script);
        const std::size_t size = sizeof(script) - 1U;
        const auto stored = store_payload(directory, "finish.sh", bytes, size);
        expect(stored.ok, "the payload is stored");
        const ExecResult ran = run_payload(stored.path);
        expect(ran.ok, "the payload runs");
        expect(ran.exit_code == 7, "the payload's own status is reported");
        struct stat info{};
        expect(::stat(stored.path, &info) == 0 && (info.st_mode & S_IXUSR) != 0,
               "running is what adds the owner-executable bit");
        expect((info.st_mode & 0777) == 0700, "and it adds nothing else");
        expect(remove_payload(stored.path), "the payload is removed afterwards");
    }

    /* ---- 3: what cannot be run is refused by name ---- */
    void refusals_are_named(const char *directory) {
        const ExecResult missing = run_payload("/tmp/ghostlock-nonexistent-payload");
        expect(!missing.ok, "a payload that is not there cannot run");
        expect(std::string_view(missing.reason) == std::string_view("executor-unavailable"),
               "the refusal carries the registered name");
        expect(!run_payload(nullptr).ok, "no payload is refused");
        expect(!run_payload("").ok, "an empty path is refused");
        const ExecResult directory_result = run_payload(directory);
        expect(!directory_result.ok, "a directory is not a payload");
    }
}  // namespace

int main() {
    char directory[] = "/tmp/ghostlock-exec-XXXXXX";
    if (::mkdtemp(directory) == nullptr) {
        std::printf("FAIL: no temporary directory\n");
        return 1;
    }
    std::printf("execution: the declaration decides\n");
    the_declaration_decides();
    std::printf("execution: running a payload\n");
    a_payload_runs(directory);
    std::printf("execution: refusals\n");
    refusals_are_named(directory);
    (void)::rmdir(directory);
    if (g_failures != 0U) {
        std::printf("execution: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("execution: PASS (moments declared + payload runs + refusals named)\n");
    return 0;
}
