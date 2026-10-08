/* S4 logging batch: the 43284 structured diagnostics.
 *
 * Two things are pinned here:
 *   1. DiagLine (diag_line.hpp) is BOUNDED and single-line: values are
 *      sanitized (a hostile path cannot inject a second line), the visible line
 *      never exceeds 256 bytes, and an over-long line is marked "truncated=1";
 *   2. the chain's milestone lines (chain.cpp via ChainOps::log) carry the
 *      named reasons and per-block context the design promises, stay inside the
 *      per-run budget, and never contain key material.
 *
 * The chain is pure over ChainOps, so the capture sink is a plain function that
 * appends to a vector; no device, no filesystem. */

#include "backend/cve_2026_43284/diag_line.hpp"
#include "backend/cve_2026_43284/steps/chain.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using ghostlock::backend::cve_2026_43284::DiagLine;
using ghostlock::backend::cve_2026_43284::kDiagLineMaxBytes;
using ghostlock::backend::cve_2026_43284::steps::ChainError;
using ghostlock::backend::cve_2026_43284::steps::ChainOps;
using ghostlock::backend::cve_2026_43284::steps::ChainRequest;
using ghostlock::backend::cve_2026_43284::steps::ChainResult;
using ghostlock::backend::cve_2026_43284::steps::ChainWaitOutcome;
using ghostlock::backend::cve_2026_43284::steps::ChainWorkspace;
using ghostlock::backend::cve_2026_43284::steps::PatchPlan;
using ghostlock::backend::cve_2026_43284::steps::PatchRegion;
using ghostlock::backend::cve_2026_43284::steps::run_chain;

namespace {

    void check(bool condition, const char *what) {
        if (!condition) {
            std::fprintf(stderr, "cve_2026_43284_logging_test: FAIL %s\n", what);
            std::abort();
        }
    }

    /* ---- 1. DiagLine is bounded, sanitized and single-line ---- */

    void test_diag_line_bounds() {
        {
            DiagLine line("write");
            line.u("i", 3u).u("of", 92u).x("off", 0x1234u).b("verify", true).u("n", 7u);
            const std::string text(line.c_str());
            check(text.starts_with("run.43284 write "), "prefix");
            check(text.ends_with("\n"), "single trailing newline");
            check(text.find("i=3") != std::string::npos, "decimal field");
            check(text.find("off=0x1234") != std::string::npos, "hex field");
            check(text.find("verify=1") != std::string::npos, "bool field");
            check(line.size() <= kDiagLineMaxBytes, "bounded visible length");
            check(text.find('\n') == text.size() - 1u, "no embedded newline");
        }
        {
            /* A value carrying whitespace/control bytes cannot forge a second
             * line: every byte outside (0x20, 0x7f) becomes '_'. */
            DiagLine line("carrier");
            line.s("path", "/vendor/lib64/a b.so\nrun.43284 fake fail reason=X");
            const std::string text(line.c_str());
            check(text.find("a_b.so") != std::string::npos, "space sanitized");
            check(text.find('\n') == text.size() - 1u, "still exactly one line");
        }
        {
            /* Over-long input is cut at the cap and marked. */
            DiagLine line("module");
            const std::string huge(600u, 'x');
            line.s("blob", huge);
            const std::string text(line.c_str());
            check(line.size() <= kDiagLineMaxBytes, "cap respected");
            check(text.find("truncated=1") != std::string::npos, "truncation is marked");
        }
        {
            DiagLine line("policy");
            line.fail("KmiFieldMismatch");
            const std::string text(line.c_str());
            check(text.find("fail reason=KmiFieldMismatch") != std::string::npos,
                  "named failure reason");
        }
        std::puts("cve_2026_43284_logging_test: diag_line_bounds ok");
    }

    /* ---- 2. the chain's milestone lines ---- */

    struct Capture final {
        std::vector<std::string> lines;
        std::array<std::uint8_t, 512> target{};
        std::uint32_t writes = 0U;
        bool fail_write = false;
    };

    void capture_log(void *ctx, const char *line) noexcept {
        static_cast<Capture *>(ctx)->lines.emplace_back(line != nullptr ? line : "");
    }

    std::int32_t write16(void *ctx, std::uint64_t offset, const void *bytes) noexcept {
        Capture &capture = *static_cast<Capture *>(ctx);
        if (capture.fail_write) {
            return -5;
        }
        if (offset + 16u > capture.target.size()) {
            return -1;
        }
        std::memcpy(capture.target.data() + offset, bytes, 16u);
        ++capture.writes;
        return 0;
    }

    long read_block(void *ctx, std::uint64_t offset, std::uint8_t *out) noexcept {
        Capture &capture = *static_cast<Capture *>(ctx);
        if (offset + 16u > capture.target.size()) {
            return -1;
        }
        std::memcpy(out, capture.target.data() + offset, 16u);
        return 16;
    }

    ChainError apply_hook_ok(void *) noexcept { return ChainError::None; }
    bool restore_hook(void *) noexcept { return true; }
    int trigger(void *) noexcept { return 0; }
    ChainWaitOutcome wait_loaded(void *, std::uint32_t) noexcept {
        return ChainWaitOutcome::LkmLoaded;
    }
    bool open_window(void *) noexcept { return true; }
    bool run_window(void *) noexcept { return true; }
    void close_window(void *) noexcept {}
    void release(void *) noexcept {}

    ChainOps ops_for(Capture &capture) {
        ChainOps ops{};
        ops.write.write16 = &write16;
        ops.write.ctx = &capture;
        ops.read_block = &read_block;
        ops.apply_hook = &apply_hook_ok;
        ops.restore_hook = &restore_hook;
        ops.trigger = &trigger;
        ops.wait_result = &wait_loaded;
        ops.open_lkm_channel = &open_window;
        ops.run_lkm_window = &run_window;
        ops.close_lkm_channel = &close_window;
        ops.release = &release;
        ops.log = &capture_log;
        return ops;
    }

    bool any_line_contains(const Capture &capture, std::string_view needle) {
        for (const std::string &line : capture.lines) {
            if (line.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    /* Every line must be bounded and free of long hex runs (the only hex the
     * chain prints are offsets; a 64-hex-digit run would mean key material
     * leaked into the chain). */
    void check_line_shape(const Capture &capture) {
        for (const std::string &line : capture.lines) {
            check(line.size() <= kDiagLineMaxBytes + 20u, "chain line bounded");
            check(line.starts_with("run.43284 "), "chain line prefix");
            check(line.ends_with("\n"), "chain line newline");
            std::size_t run = 0u;
            for (const char ch : line) {
                const bool hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
                run = hex ? run + 1u : 0u;
                check(run < 40u, "no long hex run (no key material)");
            }
        }
    }

    void test_chain_happy_path_lines() {
        Capture capture{};
        const std::array<std::uint8_t, 32> patch = [] {
            std::array<std::uint8_t, 32> bytes{};
            for (std::size_t i = 0u; i < bytes.size(); ++i) {
                bytes[i] = static_cast<std::uint8_t>(i + 1u);
            }
            return bytes;
        }();
        PatchRegion region{};
        region.offset = 0x100u;
        region.bytes = patch.data();
        region.len = patch.size();
        region.verify = true;
        region.rollback = true;
        region.label = "patch";

        ChainRequest request{};
        const auto carriers = std::array<ghostlock::backend::cve_2026_43284::steps::CarrierTarget, 1>{
                ghostlock::backend::cve_2026_43284::steps::CarrierTarget{
                        "/vendor/lib64/libbinderdebug.so", 0u}};
        request.carriers = carriers.data();
        request.carrier_count = carriers.size();
        request.plan = PatchPlan{&region, 1u, 0u};
        request.wait_timeout_ms = 1500u;

        const ChainOps ops = ops_for(capture);
        ChainWorkspace workspace{};
        const ChainResult result = run_chain(request, ops, workspace);

        check(result.error == ChainError::None, "chain succeeds");
        check(result.lkm_loaded, "lkm loaded");
        check_line_shape(capture);
        check(any_line_contains(capture, "run.43284 write stage=region i=1 of=1"),
              "per-region start line with i/of");
        check(any_line_contains(capture, "off=0x100"), "region offset");
        check(any_line_contains(capture, "run.43284 write result=ok i=1"), "region done line");
        check(any_line_contains(capture, "run.43284 hook stage=apply result=applied"),
              "hook applied line");
        check(any_line_contains(capture, "run.43284 trigger result=launched"), "trigger line");
        check(any_line_contains(capture, "run.43284 wait outcome=LkmLoaded timeout_ms=1500"),
              "wait line names the outcome");
        check(any_line_contains(capture, "run.43284 window opened=1"), "window line");
        check(any_line_contains(capture, "run.43284 finish error=None"), "finish summary");
        check(any_line_contains(capture, "written=2 verified=2"), "finish counters");
        check(capture.lines.size() <= 20u, "happy path stays well inside the run budget");
        std::puts("cve_2026_43284_logging_test: chain_happy_path_lines ok");
    }

    void test_chain_failure_lines_are_named() {
        {
            /* No write surface: the entry line must name the reason. */
            Capture capture{};
            ChainRequest request{};
            /* No write surface bound: only the log sink is installed. */
            ChainOps ops{};
            ops.write.ctx = &capture;
            ops.log = &capture_log;
            ChainWorkspace workspace{};
            const ChainResult result = run_chain(request, ops, workspace);
            check(result.error == ChainError::NotAvailable, "write surface missing");
            check(any_line_contains(capture, "run.43284 entry stage=write_ready"),
                  "entry line names the stage");
            check(any_line_contains(capture, "fail reason=NotAvailable"),
                  "entry line names the reason");
        }
        {
            /* A failing first block: named reason + block context + rollback. */
            Capture capture{};
            capture.fail_write = true;
            const std::array<std::uint8_t, 32> patch{};
            PatchRegion region{};
            region.offset = 0x40u;
            region.bytes = patch.data();
            region.len = patch.size();
            ChainRequest request{};
            const auto carriers = std::array<ghostlock::backend::cve_2026_43284::steps::CarrierTarget, 1>{
                    ghostlock::backend::cve_2026_43284::steps::CarrierTarget{
                            "/vendor/lib64/libbinderdebug.so", 0u}};
            request.carriers = carriers.data();
            request.carrier_count = carriers.size();
            request.plan = PatchPlan{&region, 1u, 0u};
            const ChainOps ops = ops_for(capture);
            ChainWorkspace workspace{};
            const ChainResult result = run_chain(request, ops, workspace);
            check(result.error == ChainError::WriteFailed, "write failure propagates");
            check(any_line_contains(capture, "run.43284 write fail reason=WriteFailed"),
                  "block failure names the reason");
            check(any_line_contains(capture, "block=0 off=0x40"), "block context");
            check(any_line_contains(capture, "run.43284 rollback"), "rollback line");
            check_line_shape(capture);
        }
        std::puts("cve_2026_43284_logging_test: chain_failure_lines_are_named ok");
    }

} // namespace

int main() {
    test_diag_line_bounds();
    test_chain_happy_path_lines();
    test_chain_failure_lines_are_named();
    std::puts("cve_2026_43284_logging_test: ok");
    return 0;
}
