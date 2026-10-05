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
#include "backend/cve_2026_43284/lkm_window.hpp"
#include "backend/cve_2026_43284/real_ops.hpp"
#include "backend/cve_2026_43284/stage_runner.hpp"
#include "backend/cve_2026_43284/steps/chain.hpp"
#include "backend/cve_2026_43284/steps/hook_patch.hpp"
#include "pipeline/component_catalog.hpp"
#include "platform/device_facts.hpp"
#include "support/cli.hpp"

#include <array>
#include <cassert>
#include <cerrno>
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
    using ghostlock::backend::cve_2026_43284::PageCacheWriteContext;
    using ghostlock::backend::cve_2026_43284::RealChainContext;
    using ghostlock::backend::cve_2026_43284::chain_error_name;
    using ghostlock::backend::cve_2026_43284::make_real_hook_io;
    using ghostlock::backend::cve_2026_43284::chain_wait_name;
    using ghostlock::backend::cve_2026_43284::make_real_chain_ops;
    using ghostlock::backend::cve_2026_43284::real_chain_read_block;
    using ghostlock::backend::cve_2026_43284::real_chain_release;
    using ghostlock::backend::cve_2026_43284::real_chain_wait_result;
    using ghostlock::backend::cve_2026_43284::stage_runner::build_module_plan;
    using ghostlock::backend::cve_2026_43284::stage_runner::format_stage_report;
    using ghostlock::backend::cve_2026_43284::lkm::LkmImageError;
    using ghostlock::backend::cve_2026_43284::lkm::ModuleFacts;
    using ghostlock::backend::cve_2026_43284::lkm::VermagicDiffReason;
    using ghostlock::backend::cve_2026_43284::stage_runner::parse_stage;
    using ghostlock::backend::cve_2026_43284::stage_runner::precheck_staged_module;
    using ghostlock::backend::cve_2026_43284::stage_runner::reconcile_module_vermagic;
    using ghostlock::backend::cve_2026_43284::lkm::DeviceKernelFacts;
    using ghostlock::backend::cve_2026_43284::lkm::VermagicOutcome;
    using ghostlock::backend::cve_2026_43284::lkm::vermagic_outcome_name;
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
    using ghostlock::backend::cve_2026_43284::LkmWindowRuntime;
    using ghostlock::contract::CapabilityState;
    using ghostlock::contract::MemoryChannel;
    using ghostlock::plugin::LkmTransport;
    using ghostlock::platform::DeviceProbeOps;
    using ghostlock::platform::FileFact;
    using ghostlock::platform::VendorCandidate;
    using ghostlock::support::cli::Cve43284HookGuard;
    using ghostlock::support::cli::Cve43284Stage;
    using ghostlock::support::cli::Mode;
    using ghostlock::support::cli::Options;
    using ghostlock::support::cli::ParseError;
    using ghostlock::backend::cve_2026_43284::stage_runner::hook_error_name;
    using ghostlock::backend::cve_2026_43284::stage_runner::hook_guard_name;
    using ghostlock::backend::cve_2026_43284::stage_runner::plan_staged_hook;
    using ghostlock::backend::cve_2026_43284::stage_runner::prepare_staged_hook;
    using ghostlock::backend::cve_2026_43284::stage_runner::read_hook_image;
    using ghostlock::backend::cve_2026_43284::stage_runner::StagedHookAssets;
    using ghostlock::backend::cve_2026_43284::stage_runner::StagedHookPlan;
    using ghostlock::backend::cve_2026_43284::stage_runner::StagedHookRequest;
    using ghostlock::backend::cve_2026_43284::stage_runner::StagedHookStatus;
    using ghostlock::backend::cve_2026_43284::steps::HookGuardPolicy;
    using ghostlock::backend::cve_2026_43284::steps::HookPatchError;
    using ghostlock::backend::cve_2026_43284::steps::HookPatchIo;
    using ghostlock::backend::cve_2026_43284::steps::kHookTrampolineBytes;
    using ghostlock::backend::cve_2026_43284::steps::kLibcxxSentrySymbol;
    using ghostlock::backend::cve_2026_43284::steps::kShellcodeMaxBytes;

    /* ---- fake LKM transport for the delta-2 residency window ---- */

    struct FakeLkm final {
        bool opened = false;
        bool closed = false;
        bool unloaded = false;
    };

    FakeLkm g_lkm;

    int fake_lkm_open(void *ctx) noexcept {
        (void)ctx;
        g_lkm.opened = true;
        g_lkm.closed = false;
        return 0;
    }

    void fake_lkm_close(void *ctx) noexcept {
        (void)ctx;
        g_lkm.closed = true;
    }

    int fake_lkm_call(void *ctx, glk_lkm_req &req) noexcept {
        (void)ctx;
        if (req.abi_version != GLK_LKM_ABI_VERSION) {
            req.status = static_cast<std::uint32_t>(-EPROTO);
            return 0;
        }
        req.status = 0U;
        if (req.op == GLK_LKM_UNLOAD) {
            g_lkm.unloaded = true;
        } else if (req.op != GLK_LKM_PING) {
            req.status = static_cast<std::uint32_t>(-EOPNOTSUPP);
        }
        return 0;
    }

    LkmTransport fake_lkm_transport() noexcept {
        LkmTransport t{};
        t.ctx = &g_lkm;
        t.open = &fake_lkm_open;
        t.close = &fake_lkm_close;
        t.call = &fake_lkm_call;
        return t;
    }

    /* ---- fake chain over a 64-byte in-memory target ---- */

    struct FakeTarget final {
        std::array<std::uint8_t, 64U> bytes{};
        bool fail_write = false;
        bool ignore_write = false;
        int trigger_rc = 0;
        ChainWaitOutcome wait = ChainWaitOutcome::LkmLoaded;
        ChainError hook_error = ChainError::None;
        std::uint32_t writes = 0U;
        std::uint32_t triggers = 0U;
        std::uint32_t releases = 0U;
        /* B5-9h-4 hook stage call counts. */
        std::uint32_t hook_applies = 0U;
        std::uint32_t hook_restores = 0U;
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

    ChainError fake_apply_hook(void *raw) noexcept {
        auto *target = static_cast<FakeTarget *>(raw);
        if (target == nullptr) {
            return ChainError::NotAvailable;
        }
        ++target->hook_applies;
        return target->hook_error;
    }

    bool fake_restore_hook(void *raw) noexcept {
        auto *target = static_cast<FakeTarget *>(raw);
        if (target == nullptr) {
            return false;
        }
        ++target->hook_restores;
        /* Mirror the real binding: a restore is owed only after an apply was
         * attempted, so hook_restored stays a decidable field. */
        return target->hook_applies != 0U;
    }

    ChainOps fake_ops(FakeTarget &target) noexcept {
        ChainOps ops{};
        ops.write.ctx = &target;
        ops.write.write16 = &fake_write16;
        ops.read_block = &fake_read_block;
        ops.apply_hook = &fake_apply_hook;
        ops.restore_hook = &fake_restore_hook;
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
            "vermagic=5.15.202-android14-8-gabc SMP preempt mod_unload modversions "
            "aarch64\0";
    /* Same wire length as the required value but a different release token, so
     * the rewrite fits the original vermagic slot. */
    constexpr char kKoMismatch[] =
            "license=GPL\0name=dirtyfrag\0"
            "vermagic=9.99.999-android14-8-gabc SMP preempt mod_unload modversions "
            "aarch64\0";
    /* Same wire length as the required value but a different option tail, so the
     * in-place rewrite fits and the mismatch is a tail (Options) one. */
    constexpr char kKoTailMismatch[] =
            "license=GPL\0name=dirtyfrag\0"
            "vermagic=5.15.202-android14-8-gabc SMP preempt modversions mod_unload "
            "aarch64\0";

    /* Minimal ELF64 AArch64 .ko with .modinfo and an empty __versions whose
     * section header does not set SHF_ALLOC, so the kernel's find_sec() would
     * not select it and the full-string same_magic() path applies. Mirrors
     * cve_2026_43284_lkm_test's fixture. */
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

    /* ---- B5-9h-1 hook-plan fixture ----
     *
     * A minimal ELF64 AArch64 image with a sentry symbol, an optionally guarded
     * second entry and an executable segment large enough for the 480-byte
     * libcxx payload. The in-memory read surface serves the image; the write
     * surface only counts calls so a plan run is proven zero-write. */

    constexpr std::uint32_t kFixtureNop = 0xD503201FU;
    constexpr std::uint32_t kFixturePaciasp = 0xD503233FU;
    constexpr std::uint32_t kFixtureBranch = 0x14000000U;

    constexpr std::uint64_t kHookTextOff = 0x100U;
    constexpr std::uint64_t kHookTextVaddr = 0x1000U;
    constexpr std::uint64_t kHookTextSize = 0x40U;
    constexpr std::uint64_t kHookDynstrOff = 0x200U;
    constexpr std::uint64_t kHookDynsymOff = 0x280U;
    constexpr std::uint64_t kHookShstrOff = 0x300U;
    constexpr std::uint64_t kHookShoff = 0x340U;
    constexpr std::uint64_t kHookImageBytes = 0x480U;

    void hook_put16(std::vector<std::uint8_t> &out, std::size_t at,
                    std::uint16_t value) {
        out[at] = static_cast<std::uint8_t>(value & 0xFFU);
        out[at + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    }
    void hook_put32(std::vector<std::uint8_t> &out, std::size_t at,
                    std::uint32_t value) {
        out[at] = static_cast<std::uint8_t>(value & 0xFFU);
        out[at + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
        out[at + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
        out[at + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    }
    void hook_put64(std::vector<std::uint8_t> &out, std::size_t at,
                    std::uint64_t value) {
        for (std::size_t i = 0U; i < 8U; ++i) {
            out[at + i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
        }
    }

    std::vector<std::uint8_t> build_hook_fixture(
            std::uint64_t exec_memsz = 0x400U,
            std::uint32_t sentry_first = kFixtureNop) {
        std::vector<std::uint8_t> out(kHookImageBytes, 0U);
        out[0] = 0x7FU;
        out[1] = 'E';
        out[2] = 'L';
        out[3] = 'F';
        out[4] = 2U;
        out[5] = 1U;
        out[6] = 1U;
        hook_put16(out, 0x10U, 3U);    /* ET_DYN */
        hook_put16(out, 0x12U, 0xB7U); /* EM_AARCH64 */
        hook_put32(out, 0x14U, 1U);
        hook_put64(out, 0x20U, 0x40U); /* e_phoff */
        hook_put64(out, 0x28U, kHookShoff);
        hook_put16(out, 0x36U, 56U);
        hook_put16(out, 0x38U, 1U);
        hook_put16(out, 0x3AU, 64U);
        hook_put16(out, 0x3CU, 5U);
        hook_put16(out, 0x3EU, 4U);

        hook_put32(out, 0x40U, 1U);            /* PT_LOAD */
        hook_put32(out, 0x44U, 5U);            /* PF_R | PF_X */
        hook_put64(out, 0x48U, kHookTextOff);
        hook_put64(out, 0x50U, kHookTextVaddr);
        hook_put64(out, 0x58U, kHookTextVaddr);
        hook_put64(out, 0x60U, kHookTextSize); /* p_filesz */
        hook_put64(out, 0x68U, exec_memsz);    /* p_memsz */

        for (std::size_t i = 0U; i < kHookTextSize / 4U; ++i) {
            hook_put32(out, static_cast<std::size_t>(kHookTextOff) + i * 4U,
                       kFixtureNop);
        }
        hook_put32(out, static_cast<std::size_t>(kHookTextOff) + 0x8U,
                   sentry_first);
        hook_put32(out, static_cast<std::size_t>(kHookTextOff) + 0x10U,
                   kFixturePaciasp);
        hook_put32(out, static_cast<std::size_t>(kHookTextOff) + 0x14U,
                   kFixtureNop);

        std::string dynstr;
        dynstr.push_back('\0');
        const std::uint32_t sentry_name = static_cast<std::uint32_t>(dynstr.size());
        dynstr += kLibcxxSentrySymbol;
        dynstr.push_back('\0');
        const std::uint32_t pac_name = static_cast<std::uint32_t>(dynstr.size());
        dynstr += "pac_entry";
        dynstr.push_back('\0');
        std::memcpy(out.data() + kHookDynstrOff, dynstr.data(), dynstr.size());
        const auto put_symbol = [&out](std::size_t index, std::uint32_t name,
                                       std::uint64_t value) {
            const std::size_t at =
                    static_cast<std::size_t>(kHookDynsymOff) + index * 24U;
            hook_put32(out, at, name);
            out[at + 4U] = 0x12U; /* GLOBAL | FUNC */
            hook_put16(out, at + 6U, 1U);
            hook_put64(out, at + 8U, value);
            hook_put64(out, at + 16U, 8U);
        };
        put_symbol(1U, sentry_name, kHookTextVaddr + 0x8U);
        put_symbol(2U, pac_name, kHookTextVaddr + 0x10U);

        const char shstr[] = "\0.text\0.dynstr\0.dynsym\0.shstrtab\0";
        std::memcpy(out.data() + kHookShstrOff, shstr, sizeof(shstr));

        const auto put_section = [&out](std::size_t index, std::uint32_t name,
                                        std::uint32_t type, std::uint64_t flags,
                                        std::uint64_t addr, std::uint64_t off,
                                        std::uint64_t size, std::uint32_t link,
                                        std::uint64_t entsize) {
            const std::size_t at =
                    static_cast<std::size_t>(kHookShoff) + index * 64U;
            hook_put32(out, at, name);
            hook_put32(out, at + 4U, type);
            hook_put64(out, at + 8U, flags);
            hook_put64(out, at + 0x10U, addr);
            hook_put64(out, at + 0x18U, off);
            hook_put64(out, at + 0x20U, size);
            hook_put32(out, at + 0x28U, link);
            hook_put64(out, at + 0x38U, entsize);
        };
        put_section(1U, 1U, 1U, 0x6U, kHookTextVaddr, kHookTextOff, kHookTextSize,
                    0U, 0U);
        put_section(2U, 7U, 3U, 0x2U, 0x2000U, kHookDynstrOff, dynstr.size(), 0U,
                    0U);
        put_section(3U, 15U, 11U, 0x2U, 0x3000U, kHookDynsymOff, 72U, 2U, 24U);
        put_section(4U, 23U, 3U, 0U, 0U, kHookShstrOff, sizeof(shstr), 0U, 0U);
        return out;
    }

    struct FakeHookImage final {
        std::vector<std::uint8_t> bytes{};
        std::uint32_t writes = 0U;
        bool fail_read = false;
    };

    std::int32_t fake_hook_write16(void *raw, std::uint64_t,
                                   const void *) noexcept {
        auto *image = static_cast<FakeHookImage *>(raw);
        if (image == nullptr) {
            return 1;
        }
        ++image->writes;
        return 1;
    }

    long fake_hook_read16(void *raw, std::uint64_t offset,
                          std::uint8_t out[16]) noexcept {
        auto *image = static_cast<FakeHookImage *>(raw);
        if (image == nullptr || out == nullptr || image->fail_read) {
            return -1;
        }
        if (offset > image->bytes.size() ||
            image->bytes.size() - static_cast<std::size_t>(offset) < 16U) {
            return -1;
        }
        std::memcpy(out, image->bytes.data() + static_cast<std::size_t>(offset),
                    16U);
        return 16;
    }

    HookPatchIo fake_hook_io(FakeHookImage &image) noexcept {
        HookPatchIo io{};
        io.ctx = &image;
        io.write16 = &fake_hook_write16;
        io.read16 = &fake_hook_read16;
        return io;
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
        using ghostlock::contract::BackendKind;
        using ghostlock::contract::ComponentSelection;
        using ghostlock::contract::StepSetKind;
        using ghostlock::contract::TerminalKind;
        const ComponentSelection selection{BackendKind::Cve2026_43284,
                                           StepSetKind::PageCacheWrite,
                                           TerminalKind::UmhForward};
        assert(ghostlock::contract::selection_supported(selection));
        assert(ghostlock::contract::backend_available(BackendKind::Cve2026_43284));
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
        /* B5-9h-4 hook stage: write stops after Verify and never applies the
         * hook; trigger/full apply it once after the carrier verify and the
         * terminus restores it once. */
        PlanFixture fixture = make_plan();
        CarrierTarget carrier = make_carrier();
        const ChainRequest request = make_request(fixture.plan, carrier);

        FakeTarget write_target{};
        ChainWorkspace write_ws{};
        const StageReport write_report =
                run_stage(Stage::Write, request, fake_ops(write_target), write_ws);
        assert(write_report.error == StageError::None);
        assert(!write_report.chain.hook_applied);
        /* The terminus always calls the restore callback, but the fake reports
         * no real restore because no apply was attempted. */
        assert(!write_report.chain.hook_restored);
        assert(write_target.hook_applies == 0U);
        assert(write_target.hook_restores == 1U);

        FakeTarget trigger_target{};
        ChainWorkspace trigger_ws{};
        const StageReport trigger_report = run_stage(
                Stage::Trigger, request, fake_ops(trigger_target), trigger_ws);
        assert(trigger_report.error == StageError::None);
        assert(trigger_report.chain.hook_applied);
        assert(trigger_report.chain.hook_restored);
        assert(trigger_target.hook_applies == 1U);
        assert(trigger_target.hook_restores == 1U);

        FakeTarget full_target{};
        ChainWorkspace full_ws{};
        const StageReport full_report =
                run_stage(Stage::Full, request, fake_ops(full_target), full_ws);
        assert(full_report.error == StageError::None);
        assert(full_target.hook_applies == 1U);
        assert(full_target.hook_restores == 1U);

        /* A hook apply failure stops the chain before the trigger (the sentry
         * must not fire against an unpatched image) and still runs the
         * terminus restore. */
        FakeTarget fail_target{};
        fail_target.hook_error = ChainError::HookFailed;
        ChainWorkspace fail_ws{};
        const StageReport fail_report =
                run_stage(Stage::Trigger, request, fake_ops(fail_target), fail_ws);
        assert(fail_report.chain.error == ChainError::HookFailed);
        assert(!fail_report.chain.hook_applied);
        assert(fail_ws.journal_count == 0U);
        assert(fail_target.hook_restores == 1U);
        assert(fail_target.triggers == 0U);
        assert(fail_report.error == StageError::WriteRejected);
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

    /* ---- B5-9h-4 hook diagnostics in the run.chain record. ---- */
    {
        PlanFixture fixture = make_plan();
        CarrierTarget carrier = make_carrier();
        const ChainRequest request = make_request(fixture.plan, carrier);

        FakeTarget armed_target{};
        ChainWorkspace armed_ws{};
        StageReport armed_report =
                run_stage(Stage::Full, request, fake_ops(armed_target), armed_ws);
        armed_report.hook_planned = true;
        armed_report.hook_armed = true;
        armed_report.hook_error = StageError::None;
        const std::string armed_text =
                format_stage_report(armed_report, "/tmp/a.ko", carrier.path, 100U);
        assert(armed_text.find(" hook_planned=1 hook_armed=1 hook=1") !=
               std::string::npos);
        assert(armed_text.find(" hook_restored=1 hook_error=None") !=
               std::string::npos);
        assert(armed_text.find("run.trigger fired=1") != std::string::npos);

        /* An unwired hook is greppable: planned but not armed, the hook did not
         * apply and the reason is a named enum. */
        StageReport unwired{};
        unwired.stage = Stage::Trigger;
        unwired.hook_planned = true;
        unwired.hook_armed = false;
        unwired.hook_error = StageError::HookIoUnavailable;
        unwired.error = StageError::WriteRejected;
        const std::string unwired_text =
                format_stage_report(unwired, "/tmp/a.ko", carrier.path, 100U);
        assert(unwired_text.find(" hook_planned=1 hook_armed=0 hook=0") !=
               std::string::npos);
        assert(unwired_text.find(" hook_restored=0 hook_error=HookIoUnavailable") !=
               std::string::npos);
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
        /* Delta-2: with no LkmWindowRuntime bound, the window callbacks stay
         * null so the chain can never open /dev/glk by accident. */
        assert(ops.open_lkm_channel == nullptr);
        assert(ops.run_lkm_window == nullptr);
        assert(ops.close_lkm_channel == nullptr);

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
        /* Delta-2 LKM residency window: make_real_chain_ops binds all three
         * callbacks when a runtime is present, and the callbacks recover it from
         * the shared ctx pointer. The fake transport stands in for /dev/glk. */
        g_lkm = FakeLkm{};
        RealChainContext ctx{};
        ctx.page.io = fake_splice_io();
        ctx.page.file_fd = 7;
        ctx.page.socket_fd = 8;
        ctx.page.sa.icv_len = 16U;
        FakeDevice device{};
        ctx.device = fake_device_ops(device);
        LkmWindowRuntime window{};
        window.set_test_transport(fake_lkm_transport());
        ctx.lkm_window = &window;

        const ChainOps ops = make_real_chain_ops(ctx);
        assert(ops.open_lkm_channel != nullptr);
        assert(ops.run_lkm_window != nullptr);
        assert(ops.close_lkm_channel != nullptr);

        assert(ops.open_lkm_channel(&ctx.page));
        assert(window.is_open());
        assert(window.capabilities().kernel != nullptr);
        assert(window.capabilities().alias != nullptr);
        assert(ops.run_lkm_window(&ctx.page));
        assert(window.window_calls() == 1U);
        ops.close_lkm_channel(&ctx.page);
        assert(window.is_closed());
        assert(g_lkm.unloaded);
        assert(window.memory()->state(MemoryChannel::LkmProxy) ==
               CapabilityState::Closed);
        assert(window.capabilities().kernel == nullptr);
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

    /* ---- B5-9h-4 staged hook arming and conditional apply_hook binding. ---- */
    {
        const std::vector<std::uint8_t> image = build_hook_fixture(0x400U);
        const std::string path = temp_module_path("hook-arm");
        assert(write_bytes(path, image));
        FakeHookImage fake{image, 0U, false};
        std::array<std::uint8_t, kShellcodeMaxBytes> shell{};
        std::array<std::uint8_t, kShellcodeMaxBytes> shell_orig{};

        /* The image is read from disk and the context is armed, so the real
         * ops bind the hook stage. */
        RealChainContext armed{};
        std::vector<std::uint8_t> armed_image{};
        const StagedHookAssets armed_assets = prepare_staged_hook(
                armed, path, kLibcxxSentrySymbol, HookGuardPolicy::Skip,
                armed_image, shell.data(), shell.size(), shell_orig.data(),
                fake_hook_io(fake));
        assert(armed_assets.status == StagedHookStatus::Armed);
        assert(armed_assets.error == StageError::None);
        assert(armed.libcxx_image == armed_image.data());
        assert(armed.libcxx_image_size == armed_image.size());
        assert(armed.hook_io.available());
        assert(armed.hook_io.ctx == static_cast<void *>(&fake));
        assert(armed.hook_shellcode == shell.data());
        assert(armed.hook_shellcode_cap == shell.size());
        assert(armed.hook_shellcode_orig == shell_orig.data());
        assert(armed.hook_guard == HookGuardPolicy::Skip);
        const ChainOps armed_ops = make_real_chain_ops(armed);
        assert(armed_ops.apply_hook != nullptr);
        assert(armed_ops.restore_hook != nullptr);

        /* A missing image is an explicit fatal error and never arms. */
        RealChainContext missing{};
        std::vector<std::uint8_t> missing_image{};
        const StagedHookAssets missing_assets = prepare_staged_hook(
                missing, "/nonexistent/ghostlock-hook.so", kLibcxxSentrySymbol,
                HookGuardPolicy::Skip, missing_image, shell.data(), shell.size(),
                shell_orig.data(), fake_hook_io(fake));
        assert(missing_assets.status == StagedHookStatus::ImageReadFailed);
        assert(missing_assets.error == StageError::HookReadFailed);
        assert(missing.libcxx_image == nullptr);
        assert(make_real_chain_ops(missing).apply_hook == nullptr);
        assert(make_real_chain_ops(missing).restore_hook == nullptr);

        /* No page-cache write face: stay unarmed and report the reason rather
         * than applying through a null surface. */
        RealChainContext no_io{};
        std::vector<std::uint8_t> no_io_image{};
        const StagedHookAssets no_io_assets = prepare_staged_hook(
                no_io, path, kLibcxxSentrySymbol, HookGuardPolicy::Skip,
                no_io_image, shell.data(), shell.size(), shell_orig.data(),
                HookPatchIo{});
        assert(no_io_assets.status == StagedHookStatus::IoUnavailable);
        assert(no_io_assets.error == StageError::HookIoUnavailable);
        assert(no_io.libcxx_image == nullptr);
        assert(!no_io.hook_io.available());
        assert(make_real_chain_ops(no_io).apply_hook == nullptr);

        /* Null shellcode buffers are an unavailable arm too. */
        RealChainContext no_buf{};
        std::vector<std::uint8_t> no_buf_image{};
        const StagedHookAssets no_buf_assets = prepare_staged_hook(
                no_buf, path, kLibcxxSentrySymbol, HookGuardPolicy::Skip,
                no_buf_image, nullptr, 0U, nullptr, fake_hook_io(fake));
        assert(no_buf_assets.status == StagedHookStatus::IoUnavailable);
        assert(no_buf_assets.error == StageError::HookIoUnavailable);
        ::unlink(path.c_str());
    }
    {
        /* make_real_hook_io derives availability from the page-cache write
         * readiness: bound io/fds/icv is usable, a missing ciphertext source is
         * not. */
        PageCacheWriteContext ready{};
        ready.io = fake_splice_io();
        ready.file_fd = 7;
        ready.socket_fd = 8;
        ready.sa.icv_len = 16U;
        assert(make_real_hook_io(ready).available());

        PageCacheWriteContext no_source{};
        no_source.io = fake_splice_io();
        no_source.file_fd = -1;
        no_source.socket_fd = 8;
        no_source.sa.icv_len = 16U;
        assert(!make_real_hook_io(no_source).available());

        PageCacheWriteContext no_socket{};
        no_socket.io = fake_splice_io();
        no_socket.file_fd = 7;
        no_socket.socket_fd = -1;
        no_socket.sa.icv_len = 16U;
        assert(!make_real_hook_io(no_socket).available());
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

    /* ---- B5-4/B5-9h-3 staged .ko precheck wiring (fail-closed). ---- */
    {
        DeviceKernelFacts required{};
        required.release = "5.15.202-android14-8-gabc";
        required.preempt = true;
        required.modversions = true;
        required.module_force_unload = false;
        ModuleFacts facts{};
        LkmImageError error = LkmImageError::ReadFailed;

        error = LkmImageError::None;
        assert(!precheck_staged_module("", required, facts, error));
        assert(error == LkmImageError::ReadFailed);

        error = LkmImageError::None;
        assert(!precheck_staged_module(temp_module_path("precheck-missing"), required,
                                       facts, error));
        assert(error == LkmImageError::ReadFailed);

        const std::string good = temp_module_path("precheck-good");
        assert(write_bytes(good, build_ko_elf(std::string_view(
                                         kKoPositive, sizeof(kKoPositive) - 1U))));
        error = LkmImageError::None;
        assert(precheck_staged_module(good, required, facts, error));
        assert(error == LkmImageError::None);
        assert(facts.elf_valid && facts.has_modinfo && facts.has_name &&
               facts.has_vermagic && facts.vermagic_matches && !facts.has_crcs &&
               facts.versions_empty && !facts.signed_module);

        /* An empty release cannot construct a required value: fail closed even
         * for a well-formed module. */
        DeviceKernelFacts no_release{};
        error = LkmImageError::None;
        assert(!precheck_staged_module(good, no_release, facts, error));
        assert(error == LkmImageError::VermagicMissing);
        ::unlink(good.c_str());

        const std::string bad = temp_module_path("precheck-bad");
        assert(write_bytes(bad, build_ko_elf(std::string_view(
                                        kKoMismatch, sizeof(kKoMismatch) - 1U))));
        error = LkmImageError::None;
        assert(!precheck_staged_module(bad, required, facts, error));
        assert(error == LkmImageError::VermagicMismatch);
        ::unlink(bad.c_str());
    }

    /* ---- B5-9h-3 reconcile: the rewrite policy and the in-place fix. ---- */
    {
        DeviceKernelFacts required{};
        required.release = "5.15.202-android14-8-gabc";
        required.preempt = true;

        std::vector<std::uint8_t> good = build_ko_elf(
                std::string_view(kKoPositive, sizeof(kKoPositive) - 1U));
        ModuleFacts facts{};
        VermagicOutcome outcome = VermagicOutcome::Unchecked;
        LkmImageError error = LkmImageError::None;
        assert(reconcile_module_vermagic(good.data(), good.size(), required, false,
                                         facts, outcome, error));
        assert(outcome == VermagicOutcome::Original);
        assert(facts.vermagic_matches && !facts.vermagic_rewritten);

        /* Option-tail mismatch with the policy off: rejected, image untouched. */
        std::vector<std::uint8_t> tail = build_ko_elf(
                std::string_view(kKoTailMismatch, sizeof(kKoTailMismatch) - 1U));
        const std::vector<std::uint8_t> tail_before = tail;
        outcome = VermagicOutcome::Unchecked;
        assert(!reconcile_module_vermagic(tail.data(), tail.size(), required, false,
                                          facts, outcome, error));
        assert(error == LkmImageError::VermagicMismatch);
        assert(outcome == VermagicOutcome::Required);
        assert(facts.vermagic_diff == VermagicDiffReason::Options);
        assert(tail == tail_before);

        /* Option-tail mismatch with the policy on: rewritten and re-verified. */
        outcome = VermagicOutcome::Unchecked;
        error = LkmImageError::None;
        assert(reconcile_module_vermagic(tail.data(), tail.size(), required, true,
                                         facts, outcome, error));
        assert(outcome == VermagicOutcome::Rewritten);
        assert(facts.vermagic_matches && facts.vermagic_rewritten);
        assert(vermagic_outcome_name(outcome) == "rewritten");

        /* A release-token-only mismatch is not a tail fix: even with the policy
         * on it is refused and the image is left untouched. */
        std::vector<std::uint8_t> release_only = build_ko_elf(
                std::string_view(kKoMismatch, sizeof(kKoMismatch) - 1U));
        const std::vector<std::uint8_t> release_before = release_only;
        outcome = VermagicOutcome::Unchecked;
        error = LkmImageError::None;
        assert(!reconcile_module_vermagic(release_only.data(), release_only.size(),
                                          required, true, facts, outcome, error));
        assert(error == LkmImageError::VermagicMismatch);
        assert(outcome == VermagicOutcome::Required);
        assert(facts.vermagic_diff == VermagicDiffReason::Release);
        assert(release_only == release_before);

        /* A non-vermagic precheck failure is never rewritten. */
        std::vector<std::uint8_t> not_elf(80U, 0U);
        const std::vector<std::uint8_t> not_elf_before = not_elf;
        outcome = VermagicOutcome::Unchecked;
        error = LkmImageError::None;
        assert(!reconcile_module_vermagic(not_elf.data(), not_elf.size(), required, true,
                                          facts, outcome, error));
        assert(error == LkmImageError::NotElf);
        assert(outcome == VermagicOutcome::Unchecked);
        assert(not_elf == not_elf_before);
    }

    /* ---- B5-9h-1 CLI: staged-hook asset selectors. ---- */
    {
        /* Defaults: unset path overrides, guard skip. */
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "a", "b"}, opts) ==
               ParseError::None);
        assert(opts.run_hook_target == nullptr);
        assert(opts.run_hook_symbol == nullptr);
        assert(opts.run_hook_guard == Cve43284HookGuard::Skip);
        assert(opts.run_carrier_path == nullptr);
        assert(opts.run_patch1_target == nullptr);
        assert(!opts.allow_vermagic_rewrite);
    }
    {
        /* B5-9h-3 explicit rewrite policy. */
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "a", "b",
                           "--cve43284-allow-vermagic-rewrite"},
                          opts) == ParseError::None);
        assert(opts.allow_vermagic_rewrite);
        assert(parse_args({"--cve43284-allow-vermagic-rewrite"}, opts) ==
               ParseError::Cve43284OptionRequiresRun);
    }
    {
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "a", "b",
                           "--cve43284-hook-target", "/system/lib64/libc++.so",
                           "--cve43284-hook-symbol", "_ZNfoo",
                           "--cve43284-hook-guard", "reject",
                           "--cve43284-carrier", "/vendor/lib64/x.so",
                           "--cve43284-patch1-target", "/apex/x/crash_dump64"},
                          opts) == ParseError::None);
        assert(opts.mode == Mode::RunCve2026_43284);
        assert(std::string_view(opts.run_hook_target) == "/system/lib64/libc++.so");
        assert(std::string_view(opts.run_hook_symbol) == "_ZNfoo");
        assert(opts.run_hook_guard == Cve43284HookGuard::Reject);
        assert(std::string_view(opts.run_carrier_path) == "/vendor/lib64/x.so");
        assert(std::string_view(opts.run_patch1_target) ==
               "/apex/x/crash_dump64");
    }
    {
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "a", "b",
                           "--cve43284-hook-guard", "skip"}, opts) ==
               ParseError::None);
        assert(opts.run_hook_guard == Cve43284HookGuard::Skip);
    }
    {
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "a", "b",
                           "--cve43284-hook-guard", "bogus"}, opts) ==
               ParseError::BadHookGuard);
    }
    {
        Options opts{};
        assert(parse_args({"--cve43284-hook-target", "x"}, opts) ==
               ParseError::Cve43284OptionRequiresRun);
    }
    {
        Options opts{};
        assert(parse_args({"--cve43284-carrier", "x"}, opts) ==
               ParseError::Cve43284OptionRequiresRun);
    }
    {
        Options opts{};
        assert(parse_args({"--ghostlock-app-call", "--cve43284-patch1-target",
                           "x"}, opts) ==
               ParseError::Cve43284OptionRequiresRun);
    }
    {
        Options opts{};
        assert(parse_args({"--cve43284-hook-target"}, opts) ==
               ParseError::MissingArgument);
    }
    {
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "a", "b",
                           "--cve43284-hook-guard"}, opts) ==
               ParseError::MissingArgument);
    }
    {
        Options opts{};
        assert(parse_args({"--probe-cve-2026-43284", "a",
                           "--cve43284-carrier", "x"}, opts) ==
               ParseError::ProbeConflict);
    }

    /* ---- delta-4 dev/gate-only --plugin: legal only with the staged run. ---- */
    {
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "a", "b",
                           "--plugin", "/data/local/tmp/cm.so"}, opts) ==
               ParseError::None);
        assert(opts.run_plugin_path != nullptr);
        assert(std::string(opts.run_plugin_path) == "/data/local/tmp/cm.so");
    }
    {
        Options opts{};
        assert(parse_args({"--plugin", "/data/local/tmp/cm.so"}, opts) ==
               ParseError::PluginRequiresRun);
    }
    {
        Options opts{};
        assert(parse_args({"--ghostlock-app-call", "--plugin", "x"}, opts) ==
               ParseError::PluginRequiresRun);
    }
    {
        Options opts{};
        assert(parse_args({"--probe-cve-2026-43284", "a", "--plugin", "x"},
                          opts) == ParseError::ProbeConflict);
    }
    {
        Options opts{};
        assert(parse_args({"--plugin"}, opts) == ParseError::MissingArgument);
    }

    /* ---- B5-9h-1 staged hook plan (read-only) and diagnostics. ---- */
    {
        assert(hook_error_name(HookPatchError::GuardRejected) == "GuardRejected");
        assert(hook_error_name(HookPatchError::TargetNotFound) == "TargetNotFound");
        assert(hook_guard_name(HookGuardPolicy::Skip) == "skip");
        assert(hook_guard_name(HookGuardPolicy::Reject) == "reject");
        assert(stage_error_name(StageError::HookPlanFailed) == "HookPlanFailed");
        assert(stage_error_name(StageError::HookReadFailed) == "HookReadFailed");
        assert(stage_error_name(StageError::HookImageTooLarge) ==
               "HookImageTooLarge");
        assert(stage_error_name(StageError::HookIoUnavailable) ==
               "HookIoUnavailable");
        assert(stage_error_name(StageError::PluginRejected) == "PluginRejected");
        StageReport code_report{};
        code_report.error = StageError::HookPlanFailed;
        assert(stage_exit_code(code_report) == 2);

        const std::vector<std::uint8_t> image = build_hook_fixture(0x400U);
        FakeHookImage fake{image, 0U, false};
        std::array<std::uint8_t, kShellcodeMaxBytes> shell{};
        std::array<std::uint8_t, kShellcodeMaxBytes> shell_orig{};

        StagedHookRequest req{};
        req.image = image.data();
        req.image_size = image.size();
        req.hook_target = "/system/lib64/libc++.so";
        req.symbol = kLibcxxSentrySymbol;
        req.guard = HookGuardPolicy::Reject;
        req.carrier_path = "/vendor/lib64/libstagefrighthw.so";
        req.io = fake_hook_io(fake);
        req.shellcode_buf = shell.data();
        req.shellcode_cap = shell.size();
        req.shellcode_orig_buf = shell_orig.data();

        /* Positive: unguarded sentry, reject policy. */
        StagedHookPlan good{};
        assert(plan_staged_hook(req, good));
        assert(good.attempted);
        assert(good.valid);
        assert(!good.steal_unsafe);
        assert(good.error == StageError::None);
        assert(good.hook_error == HookPatchError::None);
        assert(good.hook_file_offset == kHookTextOff + 0x8U);
        assert(good.hook_vaddr == kHookTextVaddr + 0x8U);
        assert(good.displaced_instruction == kFixtureNop);
        assert(good.guard_instruction == 0U);
        assert(!good.guard_skipped);
        assert(good.shellcode_len == 480U);
        assert(good.shellcode_file_offset == kHookTextOff + kHookTextSize);
        assert(good.shellcode_vaddr == kHookTextVaddr + kHookTextSize);
        /* B5-9h-2: the executable page tail can exceed the BSS tail, but this
         * fixture's p_memsz - p_filesz (0x3c0) still dominates. */
        assert(good.payload_max == 0x3C0U);
        assert(good.trampoline_len == kHookTrampolineBytes);
        assert(good.trampoline_offset == kHookTextOff);
        assert(good.trampoline_pos == 8U);
        assert(fake.writes == 0U); /* the plan never writes */

        /* The structured record carries every required diagnostic. */
        StageReport hook_report{};
        hook_report.stage = Stage::Plan;
        hook_report.hook = good;
        const std::string hook_text = format_stage_report(
                hook_report, "/tmp/a.ko", "/system/lib64/libc++.so", 100U);
        assert(hook_text.find("run.hook hook_target=/system/lib64/libc++.so") !=
               std::string::npos);
        assert(hook_text.find("hook_symbol=" +
                              std::string(kLibcxxSentrySymbol)) !=
               std::string::npos);
        assert(hook_text.find(" hook_guard=reject") != std::string::npos);
        assert(hook_text.find(" hook_vma=0x") != std::string::npos);
        assert(hook_text.find(" hook_steal=0x") != std::string::npos);
        assert(hook_text.find(" shellcode_len=480") != std::string::npos);
        assert(hook_text.find(" payload_max=960") != std::string::npos);
        assert(hook_text.find(" trampoline_len=16") != std::string::npos);
        assert(hook_text.find(" hook_error=None") != std::string::npos);

        /* Guard present + Skip: advance +4 over PACIASP. */
        StagedHookPlan skipped{};
        req.symbol = "pac_entry";
        req.guard = HookGuardPolicy::Skip;
        assert(plan_staged_hook(req, skipped));
        assert(skipped.valid);
        assert(skipped.guard_skipped);
        assert(skipped.guard_instruction == kFixturePaciasp);
        assert(skipped.hook_vaddr == kHookTextVaddr + 0x14U);
        assert(skipped.hook_file_offset == kHookTextOff + 0x14U);
        assert(fake.writes == 0U);

        /* Guard present + Reject: fail-closed. */
        StagedHookPlan rejected{};
        req.guard = HookGuardPolicy::Reject;
        assert(!plan_staged_hook(req, rejected));
        assert(!rejected.valid);
        assert(rejected.hook_error == HookPatchError::GuardRejected);
        assert(rejected.error == StageError::HookPlanFailed);
        assert(fake.writes == 0U);

        /* Missing symbol: fail-closed. */
        StagedHookPlan missing{};
        req.symbol = "no_such_symbol";
        req.guard = HookGuardPolicy::Skip;
        assert(!plan_staged_hook(req, missing));
        assert(missing.hook_error == HookPatchError::TargetNotFound);
        assert(fake.writes == 0U);

        /* PC-relative displaced word: fail-closed before any write path. */
        const std::vector<std::uint8_t> pc_image =
                build_hook_fixture(0x400U, kFixtureBranch);
        FakeHookImage pc_fake{pc_image, 0U, false};
        StagedHookPlan pc_plan{};
        req.image = pc_image.data();
        req.image_size = pc_image.size();
        req.symbol = kLibcxxSentrySymbol;
        req.guard = HookGuardPolicy::Reject;
        req.io = fake_hook_io(pc_fake);
        assert(!plan_staged_hook(req, pc_plan));
        assert(pc_plan.steal_unsafe);
        assert(pc_plan.error == StageError::HookPlanFailed);
        assert(pc_fake.writes == 0U);

        /* Shellcode capacity below the padded template: fail-closed. */
        StagedHookPlan small{};
        req.image = image.data();
        req.image_size = image.size();
        req.io = fake_hook_io(fake);
        req.shellcode_cap = 16U;
        assert(!plan_staged_hook(req, small));
        assert(small.hook_error == HookPatchError::ShellcodeBuildFailed);
        req.shellcode_cap = shell.size();

        /* Unavailable read surface: fail-closed. */
        StagedHookPlan no_io{};
        req.io = HookPatchIo{};
        assert(!plan_staged_hook(req, no_io));
        assert(no_io.hook_error == HookPatchError::IoUnavailable);
        req.io = fake_hook_io(fake);

        /* A hook-region read failure propagates. */
        StagedHookPlan read_fail{};
        fake.fail_read = true;
        assert(!plan_staged_hook(req, read_fail));
        assert(read_fail.hook_error == HookPatchError::ReadFailed);
        fake.fail_read = false;
        assert(fake.writes == 0U);
    }

    /* ---- B5-9h-1 hook image reader (read-only, bounded). ---- */
    {
        std::vector<std::uint8_t> image{};
        StageError error = StageError::None;
        assert(!read_hook_image("/nonexistent/ghostlock-hook.so", 1024U, image,
                                error));
        assert(error == StageError::HookReadFailed);
        assert(!read_hook_image("", 1024U, image, error));
        assert(error == StageError::HookReadFailed);

        const std::string path = temp_module_path("hookimage");
        const std::vector<std::uint8_t> bytes(64U, 0x33U);
        assert(write_bytes(path, bytes));
        error = StageError::None;
        assert(!read_hook_image(path, 16U, image, error));
        assert(error == StageError::HookImageTooLarge);
        assert(image.empty());
        error = StageError::None;
        assert(read_hook_image(path, 64U, image, error));
        assert(error == StageError::None);
        assert(image == bytes);
        ::unlink(path.c_str());
    }

    std::puts("cve_2026_43284_stage_runner_test: OK");
    return 0;
}
