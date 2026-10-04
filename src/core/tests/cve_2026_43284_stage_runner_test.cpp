/* Host tests for B5-9c: the staged CVE-2026-43284 execution entry, the CLI
 * parsing it hangs off, the real ChainOps binding/run_ready and the chain
 * stop-after-write semantics.
 *
 * The chain ops are fakes and the real double-fork is never called, so this
 * test writes no target, forks no process and loads no module. It covers CLI
 * mode/stage/conflict parsing, the plan builder, plan/write/trigger/full stage
 * semantics, terminus and failure propagation, the real-op binding and the
 * fail-closed catalog. */

#include "backend/cve_2026_43284/lkm/lkm_image.hpp"
#include "backend/cve_2026_43284/real_ops.hpp"
#include "backend/cve_2026_43284/stage_runner.hpp"
#include "backend/cve_2026_43284/steps/chain.hpp"
#include "pipeline/component_catalog.hpp"
#include "platform/device_facts.hpp"
#include "support/cli.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace {
    using ghostlock::backend::cve_2026_43284::RealChainContext;
    using ghostlock::backend::cve_2026_43284::chain_error_name;
    using ghostlock::backend::cve_2026_43284::chain_wait_name;
    using ghostlock::backend::cve_2026_43284::make_real_chain_ops;
    using ghostlock::backend::cve_2026_43284::real_chain_read_block;
    using ghostlock::backend::cve_2026_43284::real_chain_release;
    using ghostlock::backend::cve_2026_43284::real_chain_wait_result;
    using ghostlock::backend::cve_2026_43284::stage_runner::build_module_plan;
    using ghostlock::backend::cve_2026_43284::stage_runner::format_stage_report;
    using ghostlock::backend::cve_2026_43284::lkm::LkmImageError;
    using ghostlock::backend::cve_2026_43284::lkm::ModuleFacts;
    using ghostlock::backend::cve_2026_43284::stage_runner::parse_stage;
    using ghostlock::backend::cve_2026_43284::stage_runner::precheck_staged_module;
    using ghostlock::backend::cve_2026_43284::stage_runner::PlanBuffer;
    using ghostlock::backend::cve_2026_43284::stage_runner::run_stage;
    using ghostlock::backend::cve_2026_43284::stage_runner::stage_error_name;
    using ghostlock::backend::cve_2026_43284::stage_runner::stage_exit_code;
    using ghostlock::backend::cve_2026_43284::stage_runner::stage_name;
    using ghostlock::backend::cve_2026_43284::stage_runner::Stage;
    using ghostlock::backend::cve_2026_43284::stage_runner::StageError;
    using ghostlock::backend::cve_2026_43284::stage_runner::StageReport;
    using ghostlock::backend::cve_2026_43284::steps::ChainError;
    using ghostlock::backend::cve_2026_43284::steps::ChainOps;
    using ghostlock::backend::cve_2026_43284::steps::ChainRequest;
    using ghostlock::backend::cve_2026_43284::steps::ChainStopAfter;
    using ghostlock::backend::cve_2026_43284::steps::ChainWaitOutcome;
    using ghostlock::backend::cve_2026_43284::steps::ChainWorkspace;
    using ghostlock::backend::cve_2026_43284::steps::CarrierTarget;
    using ghostlock::backend::cve_2026_43284::steps::PatchPlan;
    using ghostlock::backend::cve_2026_43284::steps::PatchRegion;
    using ghostlock::platform::DeviceProbeOps;
    using ghostlock::platform::FileFact;
    using ghostlock::platform::VendorCandidate;
    using ghostlock::support::cli::Cve43284Stage;
    using ghostlock::support::cli::Mode;
    using ghostlock::support::cli::Options;
    using ghostlock::support::cli::ParseError;

    /* ---- fake chain over a 64-byte in-memory target ---- */

    struct FakeTarget final {
        std::array<std::uint8_t, 64U> bytes{};
        bool fail_write = false;
        bool ignore_write = false;
        int trigger_rc = 0;
        ChainWaitOutcome wait = ChainWaitOutcome::LkmLoaded;
        std::uint32_t writes = 0U;
        std::uint32_t triggers = 0U;
        std::uint32_t releases = 0U;
    };

    std::int32_t fake_write16(void *raw, std::uint64_t offset,
                              const void *bytes16) noexcept {
        auto *target = static_cast<FakeTarget *>(raw);
        if (target == nullptr || bytes16 == nullptr || offset + 16U > target->bytes.size()) {
            return 1;
        }
        if (target->fail_write) {
            return 2;
        }
        ++target->writes;
        if (!target->ignore_write) {
            std::memcpy(target->bytes.data() + offset, bytes16, 16U);
        }
        return 0;
    }

    long fake_read_block(void *raw, std::uint64_t offset,
                         std::uint8_t out[16]) noexcept {
        auto *target = static_cast<FakeTarget *>(raw);
        if (target == nullptr || offset + 16U > target->bytes.size()) {
            return -1;
        }
        std::memcpy(out, target->bytes.data() + offset, 16U);
        return 16;
    }

    int fake_trigger(void *raw) noexcept {
        auto *target = static_cast<FakeTarget *>(raw);
        if (target == nullptr) {
            return -1;
        }
        ++target->triggers;
        return target->trigger_rc;
    }

    ChainWaitOutcome fake_wait(void *raw, std::uint32_t) noexcept {
        auto *target = static_cast<FakeTarget *>(raw);
        return target == nullptr ? ChainWaitOutcome::Pending : target->wait;
    }

    void fake_release(void *raw) noexcept {
        auto *target = static_cast<FakeTarget *>(raw);
        if (target != nullptr) {
            ++target->releases;
        }
    }

    ChainOps fake_ops(FakeTarget &target) noexcept {
        ChainOps ops{};
        ops.write.ctx = &target;
        ops.write.write16 = &fake_write16;
        ops.read_block = &fake_read_block;
        ops.trigger = &fake_trigger;
        ops.wait_result = &fake_wait;
        ops.release = &fake_release;
        return ops;
    }

    /* ---- fake splice surface, bound but never invoked ---- */

    int fake_pipe2(int fds[2], int) noexcept {
        fds[0] = -1;
        fds[1] = -1;
        return 0;
    }
    long fake_splice(int, const std::uint64_t *, int, const std::uint64_t *,
                     std::size_t, unsigned) noexcept {
        return 0;
    }
    long fake_vmsplice(int, const std::uint8_t *, std::size_t, unsigned) noexcept {
        return 0;
    }
    long fake_send(int, const std::uint8_t *, std::size_t len) noexcept {
        return static_cast<long>(len);
    }
    long fake_read_at(int, std::uint8_t *, std::size_t, std::uint64_t) noexcept {
        return 0;
    }
    int fake_close(int) noexcept { return 0; }

    ghostlock::backend::cve_2026_43284::pagecache::SpliceIoOps fake_splice_io() noexcept {
        return ghostlock::backend::cve_2026_43284::pagecache::SpliceIoOps{
                &fake_pipe2, &fake_splice, &fake_vmsplice, &fake_send, &fake_read_at,
                &fake_close};
    }

    /* ---- fake device probe for the terminus and readiness ---- */

    struct FakeDevice final {
        bool success_marker = false;
        bool failure_marker = false;
        bool module_present = false;
        bool hook_marker = false;
        /* -1: use module_present. >= 0: the module is present for the first N
         * module probes of the call, then absent (the self-unload transition). */
        int module_present_polls = -1;
        int module_probes = 0;
    };

    bool fake_file_fact(void *raw, const char *path, FileFact &out) noexcept {
        auto *device = static_cast<FakeDevice *>(raw);
        out = FileFact{};
        if (device == nullptr || path == nullptr) {
            return false;
        }
        const std::string_view name(path);
        out.path.set(name);
        if (name == ghostlock::backend::cve_2026_43284::kLkmSuccessMarker) {
            out.exists = device->success_marker;
        } else if (name == ghostlock::backend::cve_2026_43284::kLkmFailureMarker) {
            out.exists = device->failure_marker;
        } else if (name == ghostlock::backend::cve_2026_43284::kLkmHookMarker) {
            out.exists = device->hook_marker;
        } else if (name == ghostlock::backend::cve_2026_43284::kLkmModulePath) {
            if (device->module_present_polls >= 0) {
                out.exists = device->module_probes < device->module_present_polls;
                ++device->module_probes;
            } else {
                out.exists = device->module_present;
            }
        }
        return true;
    }

    long stub_read_text(void *, char *, std::size_t) noexcept { return -1; }
    int stub_selinux(void *) noexcept { return -1; }
    std::size_t stub_list_vendor(void *, VendorCandidate *, std::size_t) noexcept {
        return 0U;
    }
    bool stub_symbol(void *, const char *) noexcept { return false; }

    DeviceProbeOps fake_device_ops(FakeDevice &device) noexcept {
        DeviceProbeOps ops{};
        ops.ctx = &device;
        ops.read_release = &stub_read_text;
        ops.read_proc_version = &stub_read_text;
        ops.read_selinux_enforce = &stub_selinux;
        ops.file_fact = &fake_file_fact;
        ops.list_vendor_candidates = &stub_list_vendor;
        ops.symbol_present = &stub_symbol;
        return ops;
    }

    /* ---- plan/request helpers ---- */

    struct PlanFixture final {
        std::array<std::uint8_t, 32U> desired{};
        PatchRegion region{};
        PatchPlan plan{};
    };

    PlanFixture make_plan() {
        PlanFixture fixture{};
        for (std::size_t i = 0U; i < fixture.desired.size(); ++i) {
            fixture.desired[i] = static_cast<std::uint8_t>(0xA0U + i);
        }
        fixture.region.offset = 0U;
        fixture.region.bytes = fixture.desired.data();
        fixture.region.len = fixture.desired.size();
        fixture.region.verify = true;
        fixture.region.rollback = true;
        fixture.region.label = "test";
        fixture.plan.regions = &fixture.region;
        fixture.plan.region_count = 1U;
        return fixture;
    }

    CarrierTarget make_carrier() {
        CarrierTarget carrier{};
        carrier.path = "/vendor/lib64/libbinderdebug.so";
        carrier.size = 64U;
        return carrier;
    }

    ChainRequest make_request(const PatchPlan &plan, const CarrierTarget &carrier) {
        ChainRequest request{};
        request.carriers = &carrier;
        request.carrier_count = 1U;
        request.plan = plan;
        request.wait_timeout_ms = 0U;
        /* Mirror the staged entry: the boundary surface is the carrier's known
         * size (the device path fills it from fstat(2)). */
        request.target_size = carrier.size;
        return request;
    }

    std::string temp_module_path(const char *tag) {
        std::string path = "/tmp/ghostlock-stage-";
        path += tag;
        path += "-";
        path += std::to_string(static_cast<long>(getpid()));
        path += ".ko";
        return path;
    }

    bool write_bytes(const std::string &path, const std::vector<std::uint8_t> &bytes) {
        const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) {
            return false;
        }
        const ssize_t written =
                ::write(fd, bytes.data(), bytes.size());
        ::close(fd);
        return written == static_cast<ssize_t>(bytes.size());
    }

    constexpr char kKoPositive[] =
            "license=GPL\0name=dirtyfrag\0"
            "vermagic=5.15.202-dirty SMP preempt mod_unload modversions aarch64\0";
    constexpr char kKoMismatch[] =
            "license=GPL\0name=dirtyfrag\0"
            "vermagic=6.1.100-dirty SMP preempt mod_unload modversions aarch64\0";

    /* Minimal ELF64 AArch64 .ko with .modinfo and an empty __versions, enough
     * for the B5-4 precheck. Mirrors cve_2026_43284_lkm_test's fixture. */
    std::vector<std::uint8_t> build_ko_elf(std::string_view modinfo) {
        std::string shstr;
        shstr.push_back('\0');
        const std::size_t shstrtab_name = shstr.size();
        shstr += ".shstrtab";
        shstr.push_back('\0');
        const std::size_t modinfo_name = shstr.size();
        shstr += ".modinfo";
        shstr.push_back('\0');
        const std::size_t versions_name = shstr.size();
        shstr += "__versions";
        shstr.push_back('\0');

        std::vector<std::uint8_t> out(64U, 0U);
        const auto put16 = [&out](std::size_t at, std::uint16_t value) {
            out[at] = static_cast<std::uint8_t>(value & 0xFFU);
            out[at + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
        };
        const auto put32 = [&out](std::size_t at, std::uint32_t value) {
            out[at] = static_cast<std::uint8_t>(value & 0xFFU);
            out[at + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
            out[at + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
            out[at + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
        };
        const auto put64 = [&out](std::size_t at, std::uint64_t value) {
            for (std::size_t i = 0U; i < 8U; ++i) {
                out[at + i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
            }
        };
        out[0] = 0x7FU;
        out[1] = 'E';
        out[2] = 'L';
        out[3] = 'F';
        out[4] = 2U;
        out[5] = 1U;
        out[6] = 1U;
        put16(0x10U, 1U);
        put16(0x12U, 0xB7U);

        std::size_t pos = 64U;
        const std::size_t shstr_off = pos;
        out.insert(out.end(), shstr.begin(), shstr.end());
        pos += shstr.size();
        const std::size_t modinfo_off = pos;
        out.insert(out.end(), modinfo.begin(), modinfo.end());
        pos += modinfo.size();
        const std::size_t versions_off = pos;
        while (pos % 8U != 0U) {
            out.push_back(0U);
            ++pos;
        }
        const std::size_t shoff = pos;
        out.resize(out.size() + 4U * 64U, 0U);
        put64(0x28U, shoff);
        put16(0x3AU, 64U);
        put16(0x3CU, 4U);
        put16(0x3EU, 1U);

        std::size_t section = shoff + 64U;
        put32(section, static_cast<std::uint32_t>(shstrtab_name));
        put32(section + 4U, 3U);
        put64(section + 0x18U, shstr_off);
        put64(section + 0x20U, shstr.size());

        section = shoff + 128U;
        put32(section, static_cast<std::uint32_t>(modinfo_name));
        put32(section + 4U, 1U);
        put64(section + 0x18U, modinfo_off);
        put64(section + 0x20U, modinfo.size());

        section = shoff + 192U;
        put32(section, static_cast<std::uint32_t>(versions_name));
        put32(section + 4U, 1U);
        put64(section + 0x18U, versions_off);
        put64(section + 0x20U, 0U);
        return out;
    }

    ParseError parse_args(std::vector<std::string> args, Options &opts) {
        static std::vector<std::string> storage;
        storage = std::move(args);
        static char program[] = "ghostlock";
        std::vector<char *> argv;
        argv.reserve(storage.size() + 1U);
        argv.push_back(program);
        for (std::string &arg : storage) {
            argv.push_back(arg.data());
        }
        ParseError error = ParseError::None;
        (void)ghostlock::support::cli::parse_arguments(static_cast<int>(argv.size()),
                                                      argv.data(), opts, error);
        return error;
    }
} // namespace

int main() {
    /* ---- CLI parsing: the staged entry and its stage selector. ---- */
    {
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "/tmp/a.ko", "/vendor/x.so"}, opts) ==
               ParseError::None);
        assert(opts.mode == Mode::RunCve2026_43284);
        assert(std::string_view(opts.run_module_path) == "/tmp/a.ko");
        assert(std::string_view(opts.run_target_path) == "/vendor/x.so");
        assert(opts.run_stage == Cve43284Stage::Full);
    }
    {
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "a", "b", "--stage=plan"}, opts) ==
               ParseError::None);
        assert(opts.run_stage == Cve43284Stage::Plan);
    }
    {
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "a", "b", "--stage=write"}, opts) ==
               ParseError::None);
        assert(opts.run_stage == Cve43284Stage::Write);
    }
    {
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "a", "b", "--stage=trigger"}, opts) ==
               ParseError::None);
        assert(opts.run_stage == Cve43284Stage::Trigger);
    }
    {
        /* The dev-target hatch is off by default and only accepted with the
         * staged entry. */
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "a", "b"}, opts) == ParseError::None);
        assert(!opts.allow_dev_target);
    }
    {
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "a", "b", "--allow-dev-target"}, opts) ==
               ParseError::None);
        assert(opts.mode == Mode::RunCve2026_43284);
        assert(opts.allow_dev_target);
    }
    {
        Options opts{};
        assert(parse_args({"--allow-dev-target"}, opts) ==
               ParseError::DevTargetRequiresRun);
    }
    {
        Options opts{};
        assert(parse_args({"--ghostlock-app-call", "--allow-dev-target"}, opts) ==
               ParseError::DevTargetRequiresRun);
    }
    {
        Options opts{};
        assert(parse_args({"--probe-cve-2026-43284", "a", "--allow-dev-target"}, opts) ==
               ParseError::ProbeConflict);
    }
    {
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "a"}, opts) == ParseError::MissingArgument);
    }
    {
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "a", "b", "--stage=bogus"}, opts) ==
               ParseError::BadStage);
    }
    {
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "a", "b", "--stage"}, opts) ==
               ParseError::MissingArgument);
    }
    {
        Options opts{};
        assert(parse_args({"--stage=plan"}, opts) == ParseError::StageRequiresRun);
    }
    {
        Options opts{};
        assert(parse_args({"--probe-cve-2026-43284", "a", "--stage=plan"}, opts) ==
               ParseError::StageRequiresRun);
    }
    {
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "a", "b", "--force-attack"}, opts) ==
               ParseError::RunConflict);
    }
    {
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "a", "b", "--enable-status-record"}, opts) ==
               ParseError::RunConflict);
    }
    {
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "a", "b", "--dump-kernel-log", "d"}, opts) ==
               ParseError::RunConflict);
    }
    {
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "a", "b", "--ghostlock-app-call"}, opts) ==
               ParseError::MultipleEntrypoints);
    }
    {
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "a", "b",
                           "--probe-cve-2026-43284", "c"}, opts) ==
               ParseError::MultipleEntrypoints);
    }
    {
        Options opts{};
        assert(parse_args({"--ghostlock-app-call"}, opts) == ParseError::None);
        assert(opts.mode == Mode::AppCall);
    }

    /* ---- Catalog stays fail-closed. ---- */
    {
        using ghostlock::pipeline::BackendKind;
        using ghostlock::pipeline::ComponentSelection;
        using ghostlock::pipeline::StepSetKind;
        using ghostlock::pipeline::TerminalKind;
        const ComponentSelection selection{BackendKind::Cve2026_43284,
                                           StepSetKind::PageCacheWrite,
                                           TerminalKind::UmhForward};
        assert(!ghostlock::pipeline::selection_supported(selection));
        assert(!ghostlock::pipeline::backend_available(BackendKind::Cve2026_43284));
        assert(ghostlock::pipeline::combination_supported(selection));
    }

    /* ---- Stage vocabulary and exit-code mapping. ---- */
    {
        Stage stage = Stage::Plan;
        assert(parse_stage("plan", stage) && stage == Stage::Plan);
        assert(parse_stage("write", stage) && stage == Stage::Write);
        assert(parse_stage("trigger", stage) && stage == Stage::Trigger);
        assert(parse_stage("full", stage) && stage == Stage::Full);
        assert(!parse_stage("", stage));
        assert(!parse_stage("FULL", stage));
        assert(stage_name(Stage::Plan) == "plan");
        assert(stage_name(Stage::Full) == "full");
        assert(stage_error_name(StageError::WriteRejected) == "WriteRejected");
        assert(chain_error_name(ChainError::VerifyMismatch) == "VerifyMismatch");
        assert(chain_wait_name(ChainWaitOutcome::LkmLoaded) == "LkmLoaded");

        StageReport report{};
        report.error = StageError::None;
        assert(stage_exit_code(report) == 0);
        report.error = StageError::WriteRejected;
        assert(stage_exit_code(report) == 3);
        report.error = StageError::TriggerRejected;
        assert(stage_exit_code(report) == 4);
        report.error = StageError::WaitRejected;
        assert(stage_exit_code(report) == 5);
        report.error = StageError::CleanupRejected;
        assert(stage_exit_code(report) == 6);
        report.error = StageError::SessionRejected;
        assert(stage_exit_code(report) == 2);
    }

    /* ---- Plan builder: pad, single region, size limits. ---- */
    {
        const std::string path = temp_module_path("plan");
        std::vector<std::uint8_t> module(100U, 0x5AU);
        assert(write_bytes(path, module));
        PlanBuffer plan{};
        StageError error = StageError::None;
        assert(build_module_plan(path, plan, error));
        assert(error == StageError::None);
        assert(plan.module_bytes == 100U);
        assert(plan.bytes.size() == 112U);
        assert(plan.blocks == 7U);
        assert(plan.plan.regions == &plan.region);
        assert(plan.plan.region_count == 1U);
        assert(plan.region.offset == 0U);
        assert(plan.region.bytes == plan.bytes.data());
        assert(plan.region.len == 112U);
        assert(plan.region.verify && plan.region.rollback);
        for (std::size_t i = 100U; i < 112U; ++i) {
            assert(plan.bytes[i] == 0U);
        }
        ::unlink(path.c_str());
    }
    {
        PlanBuffer plan{};
        StageError error = StageError::None;
        assert(!build_module_plan("", plan, error));
        assert(error == StageError::InvalidArgument);
    }
    {
        const std::string path = temp_module_path("tiny");
        assert(write_bytes(path, std::vector<std::uint8_t>(8U, 0U)));
        PlanBuffer plan{};
        StageError error = StageError::None;
        assert(!build_module_plan(path, plan, error));
        assert(error == StageError::ModuleReadFailed);
        ::unlink(path.c_str());
    }

    /* ---- Stage semantics over fake chain ops. ---- */
    {
        PlanFixture fixture = make_plan();
        CarrierTarget carrier = make_carrier();
        const ChainRequest request = make_request(fixture.plan, carrier);

        /* plan: validates only, writes nothing. */
        FakeTarget target{};
        ChainWorkspace workspace{};
        const StageReport plan_report = run_stage(Stage::Plan, request, fake_ops(target), workspace);
        assert(plan_report.error == StageError::None);
        assert(!plan_report.wrote);
        assert(target.writes == 0U);
        assert(target.releases == 0U);
    }
    {
        PlanFixture fixture = make_plan();
        CarrierTarget carrier = make_carrier();
        const ChainRequest request = make_request(fixture.plan, carrier);

        FakeTarget target{};
        ChainWorkspace workspace{};
        const StageReport report = run_stage(Stage::Write, request, fake_ops(target), workspace);
        assert(report.error == StageError::None);
        assert(report.wrote);
        assert(report.verified);
        assert(!report.triggered);
        assert(report.chain.error == ChainError::None);
        assert(report.chain.cleanup_ran);
        assert(target.triggers == 0U);
        assert(target.releases == 1U);
        assert(target.writes == 2U);
        assert(std::memcmp(target.bytes.data(), fixture.desired.data(), 32U) == 0);
    }
    {
        /* Dev-only hatch: a non-vendor one-shot target is rejected by default
         * and accepted (with the mode marked) only on explicit opt-in. */
        PlanFixture fixture = make_plan();
        CarrierTarget dev_carrier{};
        dev_carrier.path = "/data/local/tmp/ghostlock-stage-dev.bin";
        dev_carrier.size = 0U;

        ChainRequest request = make_request(fixture.plan, dev_carrier);
        FakeTarget rejected_target{};
        ChainWorkspace rejected_workspace{};
        const StageReport rejected =
                run_stage(Stage::Write, request, fake_ops(rejected_target),
                          rejected_workspace);
        assert(rejected.error == StageError::WriteRejected);
        assert(rejected.chain.error == ChainError::NoCarrier);
        assert(rejected.chain.blocks_written == 0U);
        assert(!rejected.dev_target);
        assert(rejected_target.writes == 0U);

        request.allow_dev_carrier_path = true;
        FakeTarget dev_target{};
        ChainWorkspace dev_workspace{};
        const StageReport accepted =
                run_stage(Stage::Write, request, fake_ops(dev_target), dev_workspace);
        assert(accepted.error == StageError::None);
        assert(accepted.wrote);
        assert(accepted.verified);
        assert(accepted.dev_target);
        assert(accepted.chain.blocks_written == 2U);
        assert(dev_target.releases == 1U);
        const std::string dev_text =
                format_stage_report(accepted, "/tmp/a.ko", dev_carrier.path, 100U);
        assert(dev_text.find("run.dev_target allow=1") != std::string::npos);

        const std::string default_text =
                format_stage_report(rejected, "/tmp/a.ko", dev_carrier.path, 100U);
        assert(default_text.find("run.dev_target allow=0") != std::string::npos);
    }
    {
        PlanFixture fixture = make_plan();
        CarrierTarget carrier = make_carrier();
        const ChainRequest request = make_request(fixture.plan, carrier);

        /* write failure propagates, release still runs. */
        FakeTarget target{};
        target.fail_write = true;
        ChainWorkspace workspace{};
        const StageReport report = run_stage(Stage::Write, request, fake_ops(target), workspace);
        assert(report.error == StageError::WriteRejected);
        assert(report.chain.error == ChainError::WriteFailed);
        assert(report.chain.cleanup_ran);
        assert(target.releases == 1U);
        assert(target.triggers == 0U);
    }
    {
        PlanFixture fixture = make_plan();
        CarrierTarget carrier = make_carrier();
        const ChainRequest request = make_request(fixture.plan, carrier);

        /* verify mismatch triggers the rollback journal. */
        FakeTarget target{};
        target.ignore_write = true;
        ChainWorkspace workspace{};
        const StageReport report = run_stage(Stage::Write, request, fake_ops(target), workspace);
        assert(report.error == StageError::WriteRejected);
        assert(report.chain.error == ChainError::VerifyMismatch);
        assert(report.chain.blocks_rolled_back == 1U);
        assert(report.chain.cleanup_ran);
        assert(target.releases == 1U);
    }
    {
        PlanFixture fixture = make_plan();
        CarrierTarget carrier = make_carrier();
        const ChainRequest request = make_request(fixture.plan, carrier);

        /* full success: terminus reached. */
        FakeTarget target{};
        target.wait = ChainWaitOutcome::LkmLoaded;
        ChainWorkspace workspace{};
        const StageReport report = run_stage(Stage::Full, request, fake_ops(target), workspace);
        assert(report.error == StageError::None);
        assert(report.terminus);
        assert(report.triggered);
        assert(target.triggers == 1U);
        assert(target.releases == 1U);
        assert(stage_exit_code(report) == 0);
    }
    {
        PlanFixture fixture = make_plan();
        CarrierTarget carrier = make_carrier();
        const ChainRequest request = make_request(fixture.plan, carrier);

        /* trigger stage: the sentry fired but the ksud marker never appeared;
         * upstream rc 2 (timeout). */
        FakeTarget target{};
        target.wait = ChainWaitOutcome::Timeout;
        ChainWorkspace workspace{};
        const StageReport report = run_stage(Stage::Trigger, request, fake_ops(target), workspace);
        assert(report.error == StageError::WaitRejected);
        assert(report.triggered);
        assert(report.wait_incomplete);
        assert(!report.terminus);
        assert(report.chain.error == ChainError::WaitTimeout);
        assert(target.triggers == 1U);
        assert(stage_exit_code(report) == 2);
    }
    {
        PlanFixture fixture = make_plan();
        CarrierTarget carrier = make_carrier();
        const ChainRequest request = make_request(fixture.plan, carrier);

        /* full: the same timeout is fatal. */
        FakeTarget target{};
        target.wait = ChainWaitOutcome::Timeout;
        ChainWorkspace workspace{};
        const StageReport report = run_stage(Stage::Full, request, fake_ops(target), workspace);
        assert(report.error == StageError::WaitRejected);
        assert(report.triggered);
        assert(report.wait_incomplete);
        assert(stage_exit_code(report) == 2);
    }
    {
        PlanFixture fixture = make_plan();
        CarrierTarget carrier = make_carrier();
        const ChainRequest request = make_request(fixture.plan, carrier);

        /* LKM failure marker. */
        FakeTarget target{};
        target.wait = ChainWaitOutcome::Failed;
        ChainWorkspace workspace{};
        const StageReport trigger_report =
                run_stage(Stage::Trigger, request, fake_ops(target), workspace);
        assert(trigger_report.error == StageError::KsudFailed);
        assert(trigger_report.wait_incomplete);
        assert(stage_exit_code(trigger_report) == 1);
        ChainWorkspace workspace2{};
        const StageReport full_report =
                run_stage(Stage::Full, request, fake_ops(target), workspace2);
        assert(full_report.error == StageError::KsudFailed);
        assert(stage_exit_code(full_report) == 1);
    }
    {
        PlanFixture fixture = make_plan();
        CarrierTarget carrier = make_carrier();
        const ChainRequest request = make_request(fixture.plan, carrier);

        /* trigger failure. */
        FakeTarget target{};
        target.trigger_rc = -1;
        ChainWorkspace workspace{};
        const StageReport report = run_stage(Stage::Trigger, request, fake_ops(target), workspace);
        assert(report.error == StageError::TriggerRejected);
        assert(report.chain.error == ChainError::TriggerFailed);
        assert(!report.triggered);
        assert(target.releases == 1U);
        assert(stage_exit_code(report) == 3);
    }
    {
        /* invalid plan fails before any op is bound. */
        PatchPlan empty{};
        CarrierTarget carrier = make_carrier();
        ChainRequest request{};
        request.carriers = &carrier;
        request.carrier_count = 1U;
        request.plan = empty;
        FakeTarget target{};
        ChainWorkspace workspace{};
        ChainOps ops = fake_ops(target);
        const StageReport report = run_stage(Stage::Write, request, ops, workspace);
        assert(report.error == StageError::PlanInvalid);
        assert(target.writes == 0U);
        assert(target.releases == 0U);
    }

    /* ---- Boundary / closure / pre-image asserts. ---- */
    {
        /* A plan reaching past the known target size fails closed before any
         * op is bound (no write, no release). */
        PlanFixture fixture = make_plan();
        CarrierTarget carrier = make_carrier();
        ChainRequest request = make_request(fixture.plan, carrier);
        request.target_size = 16U; /* the plan writes [0, 32) */
        FakeTarget target{};
        ChainWorkspace workspace{};
        const StageReport report = run_stage(Stage::Write, request, fake_ops(target), workspace);
        assert(report.error == StageError::TargetOutOfBounds);
        assert(stage_exit_code(report) == 2);
        assert(target.writes == 0U);
        assert(target.releases == 0U);
        assert(stage_error_name(StageError::TargetOutOfBounds) == "TargetOutOfBounds");
        assert(stage_error_name(StageError::PreImageMismatch) == "PreImageMismatch");
    }
    {
        /* Overlapping regions are rejected as PlanInvalid before any write. */
        PlanFixture fixture = make_plan();
        std::array<std::uint8_t, 16> other{};
        PatchRegion regions[2] = {
            fixture.region,
            {16U, other.data(), other.size(), true, true, "b"},
        };
        PatchPlan plan{regions, 2U};
        CarrierTarget carrier = make_carrier();
        ChainRequest request = make_request(plan, carrier);
        FakeTarget target{};
        ChainWorkspace workspace{};
        const StageReport report = run_stage(Stage::Write, request, fake_ops(target), workspace);
        assert(report.error == StageError::PlanInvalid);
        assert(target.writes == 0U);
        assert(target.releases == 0U);
    }
    {
        /* Pre-image match is reported preimage=ok; a mismatch fails closed. */
        PlanFixture fixture = make_plan();
        std::array<std::uint8_t, 32> original{};
        fixture.region.preimage = original.data(); /* the target starts zeroed */
        fixture.plan.regions = &fixture.region;
        CarrierTarget carrier = make_carrier();
        ChainRequest request = make_request(fixture.plan, carrier);
        FakeTarget target{};
        ChainWorkspace workspace{};
        const StageReport report = run_stage(Stage::Write, request, fake_ops(target), workspace);
        assert(report.error == StageError::None);
        assert(report.preimage_ok);
        assert(target.writes == 2U);
        const std::string text = format_stage_report(report, "/tmp/a.ko", carrier.path, 100U);
        assert(text.find("run.target path=/vendor/lib64/libbinderdebug.so size=64 "
                         "offset=0 len=32 preimage=ok") != std::string::npos);

        PlanFixture bad = make_plan();
        std::array<std::uint8_t, 32> wrong{};
        wrong.fill(0x7FU);
        bad.region.preimage = wrong.data();
        bad.plan.regions = &bad.region;
        ChainRequest bad_request = make_request(bad.plan, carrier);
        FakeTarget bad_target{};
        ChainWorkspace bad_workspace{};
        const StageReport bad_report =
                run_stage(Stage::Write, bad_request, fake_ops(bad_target), bad_workspace);
        assert(bad_report.error == StageError::PreImageMismatch);
        assert(!bad_report.preimage_ok);
        assert(bad_target.writes == 0U);
        assert(stage_exit_code(bad_report) == 2);
    }

    /* ---- Structured records: framed, greppable, no secret fields. ---- */
    {
        PlanFixture fixture = make_plan();
        CarrierTarget carrier = make_carrier();
        const ChainRequest request = make_request(fixture.plan, carrier);
        FakeTarget target{};
        ChainWorkspace workspace{};
        const StageReport report = run_stage(Stage::Full, request, fake_ops(target), workspace);
        const std::string text = format_stage_report(report, "/tmp/a.ko", carrier.path, 100U);
        assert(!text.empty());
        assert(text.find("run.cve_2026_43284 stage=full") != std::string::npos);
        assert(text.find("run.module path=/tmp/a.ko bytes=100 wrote=1 verified=1") !=
               std::string::npos);
        assert(text.find("run.target path=/vendor/lib64/libbinderdebug.so size=64 "
                         "offset=0 len=32 preimage=absent") != std::string::npos);
        assert(text.find("run.dev_target allow=0") != std::string::npos);
        assert(text.find("run.chain written=2 verified=2") != std::string::npos);
        assert(text.find("run.trigger fired=1") != std::string::npos);
        assert(text.find("run.wait outcome=LkmLoaded terminus=1") != std::string::npos);
        assert(text.find("run.cve_2026_43284 ok error=None") != std::string::npos);
        /* The record carries counts and enum names, never key material. */
        assert(text.find("aes") == std::string::npos);
    }

    /* ---- Real-op binding and run_ready. ---- */
    {
        RealChainContext ctx{};
        ctx.page.io = fake_splice_io();
        ctx.page.file_fd = 7;
        ctx.page.socket_fd = 8;
        ctx.page.sa.icv_len = 16U;
        FakeDevice device{};
        ctx.device = fake_device_ops(device);

        assert(ctx.run_ready());
        const ChainOps ops = make_real_chain_ops(ctx);
        assert(ops.write_ready());
        assert(ops.read_ready());
        assert(ops.trigger != nullptr);
        assert(ops.wait_result != nullptr);
        assert(ops.release != nullptr);
        assert(ops.run_ready());
        assert(!ctx.released_cleanly());

        /* read_block recovers the enclosing context from the shared pointer. */
        std::uint8_t block[16] = {};
        assert(real_chain_read_block(&ctx.page, 0U, block) < 0); /* fd 7 invalid */

        /* wait_result polls the injected markers. */
        device.success_marker = true;
        assert(real_chain_wait_result(&ctx.page, 0U) == ChainWaitOutcome::LkmLoaded);
        device.success_marker = false;
        device.failure_marker = true;
        assert(real_chain_wait_result(&ctx.page, 0U) == ChainWaitOutcome::Failed);
        device.failure_marker = false;
        /* A present module directory is no longer terminal: the wait keeps
         * polling until the module self-unloads or a ksud marker appears. */
        device.module_present = true;
        assert(real_chain_wait_result(&ctx.page, 0U) == ChainWaitOutcome::Timeout);
        device.module_present = false;
        assert(real_chain_wait_result(&ctx.page, 0U) == ChainWaitOutcome::Timeout);
        /* /dev/df ("module loading in flight") is likewise not terminal. */
        device.hook_marker = true;
        assert(real_chain_wait_result(&ctx.page, 0U) == ChainWaitOutcome::Timeout);
        device.hook_marker = false;
        /* Module observed present then gone (self-unload) with no ksud marker:
         * the endgame finished without a success marker -> Failed. */
        device.module_present_polls = 1;
        device.module_probes = 0;
        assert(real_chain_wait_result(&ctx.page, 100U) == ChainWaitOutcome::Failed);
        device.module_present_polls = -1;

        /* release closes both fds, wipes the SA and is idempotent. */
        for (std::size_t i = 0U; i < ctx.page.sa.aes_key.size(); ++i) {
            ctx.page.sa.aes_key[i] = static_cast<std::uint8_t>(0xABU);
            ctx.page.sa.hmac_key[i] = static_cast<std::uint8_t>(0xCDU);
        }
        real_chain_release(&ctx.page);
        assert(ctx.released_cleanly());
        assert(ctx.page.file_fd == -1);
        assert(ctx.page.socket_fd == -1);
        for (std::size_t i = 0U; i < ctx.page.sa.aes_key.size(); ++i) {
            assert(ctx.page.sa.aes_key[i] == 0U);
            assert(ctx.page.sa.hmac_key[i] == 0U);
        }
    }
    {
        /* No device surface: run_ready fails closed even with a bound write. */
        RealChainContext ctx{};
        ctx.page.io = fake_splice_io();
        ctx.page.file_fd = 7;
        ctx.page.socket_fd = 8;
        ctx.page.sa.icv_len = 16U;
        assert(!ctx.run_ready());
        const ChainOps ops = make_real_chain_ops(ctx);
        assert(ops.write_ready());
        /* The device gate lives in RealChainContext::run_ready(); the ChainOps
         * surface itself is fully bound. */
        assert(ops.run_ready());
    }
    {
        /* No fds: the write binding is null and run_ready fails closed. */
        RealChainContext ctx{};
        ctx.page.io = fake_splice_io();
        ctx.page.sa.icv_len = 16U;
        FakeDevice device{};
        ctx.device = fake_device_ops(device);
        assert(!ctx.run_ready());
        const ChainOps ops = make_real_chain_ops(ctx);
        assert(!ops.write_ready());
        assert(!ops.run_ready());
    }
    {
        /* malformed icv_len also disables the write binding. */
        RealChainContext ctx{};
        ctx.page.io = fake_splice_io();
        ctx.page.file_fd = 7;
        ctx.page.socket_fd = 8;
        ctx.page.sa.icv_len = 0U;
        FakeDevice device{};
        ctx.device = fake_device_ops(device);
        assert(!ctx.run_ready());
    }

    /* ---- real read_block still works for the B5-9a read-only context. ---- */
    {
        const std::string path = temp_module_path("read");
        assert(write_bytes(path, std::vector<std::uint8_t>(48U, 0x11U)));
        const int fd = ::open(path.c_str(), O_RDONLY);
        assert(fd >= 0);
        RealChainContext ctx{};
        ctx.page.io = fake_splice_io();
        ctx.page.file_fd = fd;
        std::uint8_t block[16] = {};
        assert(real_chain_read_block(&ctx.page, 0U, block) == 16);
        for (std::size_t i = 0U; i < 16U; ++i) {
            assert(block[i] == static_cast<std::uint8_t>(0x11U));
        }
        assert(real_chain_read_block(nullptr, 0U, block) < 0);
        ::close(fd);
        ::unlink(path.c_str());
    }

    /* ---- B5-4 staged .ko precheck wiring (fail-closed). ---- */
    {
        const char release[] = "5.15.202-android14-8-gabc";
        ModuleFacts facts{};
        LkmImageError error = LkmImageError::ReadFailed;

        error = LkmImageError::None;
        assert(!precheck_staged_module("", release, facts, error));
        assert(error == LkmImageError::ReadFailed);

        error = LkmImageError::None;
        assert(!precheck_staged_module(temp_module_path("precheck-missing"), release,
                                       facts, error));
        assert(error == LkmImageError::ReadFailed);

        error = LkmImageError::None;
        assert(!precheck_staged_module("/tmp/ghostlock-precheck.ko", "", facts, error));
        assert(error == LkmImageError::ReadFailed);

        const std::string good = temp_module_path("precheck-good");
        assert(write_bytes(good, build_ko_elf(std::string_view(
                                         kKoPositive, sizeof(kKoPositive) - 1U))));
        error = LkmImageError::None;
        assert(precheck_staged_module(good, release, facts, error));
        assert(error == LkmImageError::None);
        assert(facts.elf_valid && facts.has_modinfo && facts.has_name &&
               facts.has_vermagic && facts.vermagic_matches && facts.versions_empty &&
               !facts.signed_module);
        ::unlink(good.c_str());

        const std::string bad = temp_module_path("precheck-bad");
        assert(write_bytes(bad, build_ko_elf(std::string_view(
                                        kKoMismatch, sizeof(kKoMismatch) - 1U))));
        error = LkmImageError::None;
        assert(!precheck_staged_module(bad, release, facts, error));
        assert(error == LkmImageError::VermagicMismatch);
        ::unlink(bad.c_str());
    }

    std::puts("cve_2026_43284_stage_runner_test: OK");
    return 0;
}
