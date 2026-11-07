/**
 * @file tools/tests/diagnose_abi_test.cpp
 * @brief Verify the unversioned Diagnose call contract, two log windows, and one handoff call.
 * @note The production implementation is linked with a deterministic handoff stub; run_handoff_test exercises
 *       the real common locator/spawn implementation.
 */

#include "backends/diagnose/api/diagnose_api.h"
#include "common/gl_log.h"
#include "common/run_handoff.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <sys/types.h>
#include <unistd.h>

namespace {
    std::int32_t stub_handoff_status = 0;
    int stub_handoff_calls = 0;

    /** @return One request whose borrowed log fds are supplied by each test. */
    gl_diagnose_request request_fixture() noexcept {
        gl_diagnose_request request{};
        request.nonce = UINT64_C(0x0123456789abcdef);
        request.native_log_fd = -1;
        request.shell_log_fd = -1;
        return request;
    }

    /** @return One result with sentinel payload and the required handoff-not-run value. */
    gl_diagnose_result result_fixture() noexcept {
        gl_diagnose_result result{};
        result.uid = INT32_C(-101);
        result.pid = INT32_C(-202);
        result.echoed_nonce = UINT64_C(0xfedcba9876543210);
        result.handoff_status = GL_DIAGNOSE_HANDOFF_NOT_RUN;
        return result;
    }

    /** Assert a failed call returns one status and leaves every result field unchanged. */
    void expect_unchanged(
        const std::int32_t expected_status,
        const gl_diagnose_request *const request,
        gl_diagnose_result *const result
    ) {
        const gl_diagnose_result before = *result;
        assert(gl_diagnose_run(request, result) == expected_status);
        assert(result->uid == before.uid);
        assert(result->pid == before.pid);
        assert(result->echoed_nonce == before.echoed_nonce);
        assert(result->handoff_status == before.handoff_status);
    }

    /** Read one complete bounded record after every writer for the pipe has closed. */
    std::string read_record(const int fd) {
        std::array<char, 769U> bytes{};
        const ssize_t count = read(fd, bytes.data(), bytes.size());
        assert(count > 0);
        return std::string(bytes.data(), static_cast<std::size_t>(count));
    }

    /** Execute one successful call and verify both phase-specific records and the final result. */
    void run_chain(const std::uint64_t nonce, const std::int32_t handoff_status) {
        std::array<int, 2U> native_pipe{-1, -1};
        std::array<int, 2U> shell_pipe{-1, -1};
        assert(pipe(native_pipe.data()) == 0);
        assert(pipe(shell_pipe.data()) == 0);
        gl_diagnose_request request = request_fixture();
        gl_diagnose_result result = result_fixture();
        request.nonce = nonce;
        request.native_log_fd = native_pipe[1];
        request.shell_log_fd = shell_pipe[1];
        stub_handoff_status = handoff_status;
        stub_handoff_calls = 0;

        assert(gl_diagnose_run(&request, &result) == GL_DIAGNOSE_STATUS_OK);
        assert(stub_handoff_calls == 1);
        assert(close(native_pipe[1]) == 0);
        assert(close(shell_pipe[1]) == 0);
        const std::string native_record = read_record(native_pipe[0]);
        const std::string shell_record = read_record(shell_pipe[0]);
        assert(close(native_pipe[0]) == 0);
        assert(close(shell_pipe[0]) == 0);

        assert(result.uid == static_cast<std::int32_t>(getuid()));
        assert(result.pid == static_cast<std::int32_t>(getpid()));
        assert(result.echoed_nonce == nonce);
        assert(result.handoff_status == handoff_status);
        assert(native_record.starts_with("[I/"));
        assert(native_record.ends_with("ms] diagnose:native:roundtrip-ok\n"));
        assert(native_record.find("diagnose:shell") == std::string::npos);
        assert(shell_record.starts_with("[I/"));
        assert(shell_record.ends_with("ms] diagnose:shell:handoff-ok\n"));
        assert(shell_record.find("diagnose:native") == std::string::npos);
    }

    /** Verify pointer, sentinel, and both borrowed-fd failure boundaries. */
    void test_failure_statuses() {
        gl_diagnose_request request = request_fixture();
        gl_diagnose_result result = result_fixture();
        assert(gl_diagnose_run(nullptr, &result) == GL_DIAGNOSE_STATUS_NULL_REQUEST);
        assert(gl_diagnose_run(&request, nullptr) == GL_DIAGNOSE_STATUS_NULL_RESULT);

        result.handoff_status = 0;
        expect_unchanged(GL_DIAGNOSE_STATUS_RESULT_SENTINEL, &request, &result);
        result = result_fixture();
        stub_handoff_calls = 0;
        expect_unchanged(GL_DIAGNOSE_STATUS_LOG_ATTACH, &request, &result);
        assert(stub_handoff_calls == 0);

        std::array<int, 2U> native_pipe{-1, -1};
        assert(pipe(native_pipe.data()) == 0);
        request.native_log_fd = native_pipe[1];
        result = result_fixture();
        expect_unchanged(GL_DIAGNOSE_STATUS_LOG_ATTACH, &request, &result);
        assert(stub_handoff_calls == 0);
        assert(close(native_pipe[1]) == 0);
        const std::string native_record = read_record(native_pipe[0]);
        assert(native_record.ends_with("ms] diagnose:native:roundtrip-ok\n"));
        assert(close(native_pipe[0]) == 0);
    }

    /** Verify signed Kotlin Long representatives and successful/failed helper returns. */
    void test_nonce_records_and_handoff_status() {
        constexpr std::array<std::uint64_t, 5U> nonces{
            UINT64_C(0x8000000000000000),
            UINT64_C(0xffffffffffffffff),
            UINT64_C(0),
            UINT64_C(1),
            UINT64_C(0x7fffffffffffffff),
        };
        for (const std::uint64_t nonce : nonces) {
            run_chain(nonce, 0);
        }
        run_chain(UINT64_C(7), GL_HANDOFF_SCRIPT_MAGIC);
        run_chain(UINT64_C(8), GL_HANDOFF_CHILD_EXIT_127_AMBIGUOUS);
    }
}

extern "C" int32_t run_handoff(void) {
    ++stub_handoff_calls;
    info("diagnose:shell:handoff-ok");
    return stub_handoff_status;
}

/** @return Zero after every ownership, log-routing, call-status, and result assertion passes. */
int main() {
    test_failure_statuses();
    test_nonce_records_and_handoff_status();
    return 0;
}
