/* Host contract tests for B5-7: platform device facts and the CVE-2026-43284
 * backend -> umh_forward bridge.
 *
 * The device probe and the B5-6 chain are fakes, so nothing touches a device,
 * forks or writes a file. The production units are the same ones the device
 * build uses, so the fail-closed policy and the handoff fill are exercised end
 * to end. */

#include "backend/cve_2026_43284/backend_terminal.hpp"
#include "backend/cve_2026_43284_backend.hpp"
#include "backend/cve_2026_43284/steps/steps.hpp"
#include "platform/device_facts.hpp"
#include "session/core_session.hpp"
#include "session/stage_types.hpp"
#include "terminal/terminal_input.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

using ghostlock::backend::cve_2026_43284::BackendTerminalDeps;
using ghostlock::backend::cve_2026_43284::BackendTerminalError;
using ghostlock::backend::cve_2026_43284::BackendTerminalResult;
using ghostlock::backend::cve_2026_43284::kCarrierTokenDefault;
using ghostlock::backend::cve_2026_43284::kCarrierTokenLibbinderdebug;
using ghostlock::backend::cve_2026_43284::kCarrierTokenMax;
using ghostlock::backend::cve_2026_43284::resolve_carrier_token;
using ghostlock::backend::cve_2026_43284::run_backend_terminal;
using ghostlock::backend::Cve2026_43284Policy;
using ghostlock::backend::Cve2026_43284Profile;
using ghostlock::backend::cve_2026_43284::IpsecSaParams;
using ghostlock::backend::cve_2026_43284::lkm::kLateLoadArgPackageName;
using ghostlock::backend::cve_2026_43284::lkm::kLateLoadArgRoPartitions;
using ghostlock::backend::cve_2026_43284::lkm::kLkmPathTokenBundled;
using ghostlock::backend::cve_2026_43284::lkm::kSelinuxExecContextVendorModprobe;
using ghostlock::backend::cve_2026_43284::steps::ChainError;
using ghostlock::backend::cve_2026_43284::steps::ChainOps;
using ghostlock::backend::cve_2026_43284::steps::ChainWaitOutcome;
using ghostlock::backend::cve_2026_43284::steps::kDefaultCarriers;
using ghostlock::backend::cve_2026_43284::steps::PageCacheWriteSteps;
using ghostlock::backend::cve_2026_43284::steps::PatchPlan;
using ghostlock::backend::cve_2026_43284::steps::PatchRegion;
using ghostlock::platform::DeviceFactError;
using ghostlock::platform::DeviceProbeOps;
using ghostlock::platform::FileFact;
using ghostlock::platform::VendorCandidate;
using ghostlock::session::CoreSession;
using ghostlock::session::StageResult;
using ghostlock::terminal::RootProgram;
using ghostlock::terminal::RootProgramKind;
using ghostlock::terminal::UmhCommand;
using ghostlock::terminal::UmhForwardInput;
using ghostlock::terminal::UmhForwardOutcome;
using ghostlock::terminal::UmhLkmSource;

namespace {

    struct FakeDevice final {
        bool bind = true;
        bool release_ok = true;
        const char *release = "5.15.202-android14-8-gabcdef";
        bool proc_version_ok = true;
        const char *proc_version = "#1 SMP PREEMPT Fri Jan 1 00:00:00 UTC 2026";
        bool enforce_ok = true;
        int enforce = 1;
        bool crash_dump_exists = true;
        bool crash_dump_label = true;
        bool crash_dump_verity = false;
        std::size_t vendor_count = 2U;
        bool selinux_state = true;
        bool defex_enforce = false;
        bool defex_user_exec = false;
        bool defex_get_dpath = false;
        bool precheck_ok = true;
    };

    long dev_read_release(void *ctx, char *out, std::size_t capacity) noexcept {
        auto *f = static_cast<FakeDevice *>(ctx);
        if (!f->release_ok) {
            return -5;
        }
        const std::size_t n = std::strlen(f->release);
        if (n + 1U > capacity) {
            return -36;
        }
        std::memcpy(out, f->release, n + 1U);
        return static_cast<long>(n);
    }

    long dev_read_proc_version(void *ctx, char *out, std::size_t capacity) noexcept {
        auto *f = static_cast<FakeDevice *>(ctx);
        if (!f->proc_version_ok) {
            return -5;
        }
        const std::size_t n = std::strlen(f->proc_version);
        if (n + 1U > capacity) {
            return -36;
        }
        std::memcpy(out, f->proc_version, n + 1U);
        return static_cast<long>(n);
    }

    int dev_read_enforce(void *ctx) noexcept {
        auto *f = static_cast<FakeDevice *>(ctx);
        return f->enforce_ok ? f->enforce : -5;
    }

    bool dev_file_fact(void *ctx, const char *path, FileFact &out) noexcept {
        auto *f = static_cast<FakeDevice *>(ctx);
        out = FileFact{};
        out.path.set(path);
        if (std::string_view(path) == ghostlock::platform::kCrashDump64Path) {
            out.exists = f->crash_dump_exists;
            out.label.set("u:object_r:crash_dump_exec:s0");
            out.label_known = f->crash_dump_label;
            out.verity = f->crash_dump_verity;
        }
        return true;
    }

    std::size_t dev_list_vendor(void *ctx, VendorCandidate *out,
                                std::size_t capacity) noexcept {
        auto *f = static_cast<FakeDevice *>(ctx);
        if (out == nullptr || capacity == 0U) {
            return 0U;
        }
        const std::size_t n = f->vendor_count < capacity ? f->vendor_count : capacity;
        for (std::size_t i = 0U; i < n; ++i) {
            out[i] = VendorCandidate{};
            out[i].path.set("/vendor/lib64/libcandidate.so");
            out[i].label.set("u:object_r:vendor_file:s0");
            out[i].exists = true;
            out[i].vendor_file_label = true;
        }
        return n;
    }

    bool dev_symbol(void *ctx, const char *symbol) noexcept {
        auto *f = static_cast<FakeDevice *>(ctx);
        const std::string_view name(symbol);
        if (name == "selinux_state") {
            return f->selinux_state;
        }
        if (name == "task_defex_enforce") {
            return f->defex_enforce;
        }
        if (name == "task_defex_user_exec") {
            return f->defex_user_exec;
        }
        if (name == "get_dc_target_dpath") {
            return f->defex_get_dpath;
        }
        return false;
    }

    DeviceProbeOps make_device_ops(FakeDevice &f) noexcept {
        DeviceProbeOps ops{};
        if (!f.bind) {
            return ops;
        }
        ops.ctx = &f;
        ops.read_release = dev_read_release;
        ops.read_proc_version = dev_read_proc_version;
        ops.read_selinux_enforce = dev_read_enforce;
        ops.file_fact = dev_file_fact;
        ops.list_vendor_candidates = dev_list_vendor;
        ops.symbol_present = dev_symbol;
        return ops;
    }

    struct FakeChain final {
        std::array<std::uint8_t, 256> file{};
        std::array<std::uint8_t, 16> payload{};
        PatchRegion region{};
        ChainWaitOutcome wait = ChainWaitOutcome::LkmLoaded;
        int write_fail = 0;
        bool trigger_ok = true;
        std::uint32_t release_calls = 0U;
        std::size_t read_calls = 0U;
    };

    bool chain_build_plan(void *ctx, PatchPlan &out, ChainError &error) noexcept {
        (void)error;
        auto *c = static_cast<FakeChain *>(ctx);
        out = PatchPlan{&c->region, 1U};
        return true;
    }

    long chain_read(void *ctx, std::uint64_t offset, std::uint8_t out[16]) noexcept {
        auto *c = static_cast<FakeChain *>(ctx);
        ++c->read_calls;
        for (std::size_t i = 0U; i < 16U; ++i) {
            out[i] = c->file[static_cast<std::size_t>(offset) + i];
        }
        return 16L;
    }

    std::int32_t chain_write16(void *ctx, std::uint64_t offset,
                               const void *bytes16) noexcept {
        auto *c = static_cast<FakeChain *>(ctx);
        if (c->write_fail > 0) {
            --c->write_fail;
            return 1;
        }
        const auto *src = static_cast<const std::uint8_t *>(bytes16);
        for (std::size_t i = 0U; i < 16U; ++i) {
            c->file[static_cast<std::size_t>(offset) + i] = src[i];
        }
        return 0;
    }

    int chain_trigger(void *ctx) noexcept {
        auto *c = static_cast<FakeChain *>(ctx);
        return c->trigger_ok ? 0 : 1;
    }

    ChainWaitOutcome chain_wait(void *ctx, std::uint32_t timeout_ms) noexcept {
        (void)timeout_ms;
        return static_cast<FakeChain *>(ctx)->wait;
    }

    void chain_release(void *ctx) noexcept {
        ++static_cast<FakeChain *>(ctx)->release_calls;
    }

    ChainOps make_chain_ops(FakeChain &c) noexcept {
        ChainOps ops{};
        ops.write.ctx = &c;
        ops.write.write16 = chain_write16;
        ops.read_block = chain_read;
        ops.trigger = chain_trigger;
        ops.wait_result = chain_wait;
        ops.release = chain_release;
        ops.build_plan = chain_build_plan;
        return ops;
    }

    void reset_chain(FakeChain &c) noexcept {
        c = FakeChain{};
        c.payload.fill(0xA5U);
        c.region = PatchRegion{0U, c.payload.data(), c.payload.size(), true, true, "t"};
    }

    Cve2026_43284Profile make_profile() noexcept {
        Cve2026_43284Profile profile{};
        profile.carrier_path = kCarrierTokenDefault;
        profile.lkm_path = kLkmPathTokenBundled;
        profile.kmi = 5015;
        profile.selinux_exec_context = kSelinuxExecContextVendorModprobe;
        profile.late_load_args = kLateLoadArgPackageName | kLateLoadArgRoPartitions;
        profile.defex_symbol = 0U;
        profile.steps = PageCacheWriteSteps::id;
        return profile;
    }

    bool precheck_ok(void *ctx, std::string_view, const ghostlock::backend::cve_2026_43284::lkm::KernelRelease &,
                     ghostlock::backend::cve_2026_43284::lkm::ModuleFacts &,
                     ghostlock::backend::cve_2026_43284::lkm::LkmImageError &) noexcept {
        return static_cast<FakeDevice *>(ctx)->precheck_ok;
    }

    BackendTerminalDeps make_deps(FakeDevice &dev, FakeChain &chain) noexcept {
        BackendTerminalDeps deps{};
        deps.device = make_device_ops(dev);
        deps.chain = make_chain_ops(chain);
        return deps;
    }

    BackendTerminalResult run_case(const Cve2026_43284Profile &profile, FakeDevice &dev,
                                   FakeChain &chain, UmhForwardInput &out) noexcept {
        RootProgram root{};
        root.kind = RootProgramKind::KernelSU;
        root.set_argv("/data/adb/ksud");
        IpsecSaParams sa{};
        const BackendTerminalDeps deps = make_deps(dev, chain);
        return run_backend_terminal(profile, root, sa, deps, false, out);
    }

    /* ---- positive: facts resolve, chain loads LKM, input is filled ---- */

    void test_success() {
        FakeDevice dev{};
        FakeChain chain{};
        reset_chain(chain);
        const Cve2026_43284Profile profile = make_profile();
        RootProgram root{};
        root.kind = RootProgramKind::KernelSU;
        root.set_argv("/data/adb/ksud");
        IpsecSaParams sa{};
        sa.spi = 0x11223344U;
        const BackendTerminalDeps deps = make_deps(dev, chain);
        UmhForwardInput out{};

        const BackendTerminalResult r =
                run_backend_terminal(profile, root, sa, deps, false, out);

        assert(r.error == BackendTerminalError::None);
        assert(r.ready);
        assert(r.fact_error == DeviceFactError::None);
        assert(r.chain.lkm_loaded);
        assert(r.chain.blocks_written == 1U);
        assert(r.chain.blocks_verified == 1U);
        assert(r.chain.carrier == nullptr); /* never expose the local list */
        assert(chain.release_calls == 1U);

        assert(out.lkm_loaded);
        assert(out.root_program.argv_view() == "/data/adb/ksud");
        assert(out.lkm_source == UmhLkmSource::BundledKmi);
        assert(std::string_view(out.kmi_label.data()) == "android14-5.15");
        assert(std::string_view(out.carrier_path.data()) == kDefaultCarriers[0].path);
        assert(out.command.argc == 5U);
        assert(out.command.arg(0) == "/data/adb/ksud");
        assert(out.command.arg(1) == "late-load");
        assert(out.command.arg(2) == "--package-name");
        assert(out.command.arg(3) == "me.weishu.kernelsu");
        assert(out.command.arg(4) == "--ro-partitions");
        assert(out.command.selinux_exec_context == kSelinuxExecContextVendorModprobe);
        assert(out.session_secrets == &sa);
        assert(out.session_secrets_size == sizeof(IpsecSaParams));
    }

    /* ---- fail-closed device facts ---- */

    void expect_fact_error(FakeDevice &dev, DeviceFactError expected) {
        FakeChain chain{};
        reset_chain(chain);
        UmhForwardInput out{};
        const BackendTerminalResult r = run_case(make_profile(), dev, chain, out);
        assert(r.error == BackendTerminalError::DeviceFactsIncomplete);
        assert(r.fact_error == expected);
        assert(!r.ready);
        assert(!out.lkm_loaded);
    }

    void test_device_facts_fail_closed() {
        FakeDevice unbound{};
        unbound.bind = false;
        {
            FakeChain chain{};
            reset_chain(chain);
            UmhForwardInput out{};
            const BackendTerminalResult r =
                    run_case(make_profile(), unbound, chain, out);
            assert(r.error == BackendTerminalError::DeviceFactsUnavailable);
            assert(r.fact_error == DeviceFactError::Unavailable);
        }
        { FakeDevice d{}; d.release_ok = false; expect_fact_error(d, DeviceFactError::ReleaseMissing); }
        { FakeDevice d{}; d.proc_version_ok = false; expect_fact_error(d, DeviceFactError::ProcVersionMissing); }
        { FakeDevice d{}; d.enforce_ok = false; expect_fact_error(d, DeviceFactError::SelinuxMissing); }
        { FakeDevice d{}; d.crash_dump_exists = false; expect_fact_error(d, DeviceFactError::CrashDumpMissing); }
        { FakeDevice d{}; d.crash_dump_label = false; expect_fact_error(d, DeviceFactError::CrashDumpLabelUnknown); }
        { FakeDevice d{}; d.vendor_count = 0U; expect_fact_error(d, DeviceFactError::VendorCandidatesMissing); }
    }

    /* ---- symbol absence is recorded, not fatal: the remaining facts being
     * complete must still reach a ready terminal handoff ---- */

    void test_symbols_restricted_still_ready() {
        FakeDevice dev{};
        dev.selinux_state = false;
        FakeChain chain{};
        reset_chain(chain);
        UmhForwardInput out{};
        const BackendTerminalResult r = run_case(make_profile(), dev, chain, out);
        assert(r.error == BackendTerminalError::None);
        assert(r.fact_error == DeviceFactError::None);
        assert(r.ready);
        assert(out.lkm_loaded);
    }

    void test_patched_kernel() {
        FakeDevice dev{};
        dev.proc_version = "#1 SMP f4c50a4 dirty";
        FakeChain chain{};
        reset_chain(chain);
        UmhForwardInput out{};
        const BackendTerminalResult r = run_case(make_profile(), dev, chain, out);
        assert(r.error == BackendTerminalError::PatchedKernel);
        assert(!r.ready);
    }

    void test_policy_rejections() {
        FakeDevice dev{};
        {
            Cve2026_43284Profile p = make_profile();
            p.steps = 2U;
            FakeChain chain{}; reset_chain(chain); UmhForwardInput out{};
            const BackendTerminalResult r = run_case(p, dev, chain, out);
            assert(r.error == BackendTerminalError::StepsMismatch);
        }
        {
            Cve2026_43284Profile p = make_profile();
            p.selinux_exec_context.reset();
            FakeChain chain{}; reset_chain(chain); UmhForwardInput out{};
            const BackendTerminalResult r = run_case(p, dev, chain, out);
            assert(r.error == BackendTerminalError::ProfileIncomplete);
        }
        {
            Cve2026_43284Profile p = make_profile();
            p.kmi = 6001;
            FakeChain chain{}; reset_chain(chain); UmhForwardInput out{};
            const BackendTerminalResult r = run_case(p, dev, chain, out);
            assert(r.error == BackendTerminalError::LkmPolicyRejected);
            assert(r.lkm_error ==
                   ghostlock::backend::cve_2026_43284::lkm::LkmPolicyError::KmiFieldMismatch);
        }
        {
            Cve2026_43284Profile p = make_profile();
            p.lkm_path.reset();
            FakeChain chain{}; reset_chain(chain); UmhForwardInput out{};
            const BackendTerminalResult r = run_case(p, dev, chain, out);
            assert(r.error == BackendTerminalError::LkmPolicyRejected);
            assert(r.lkm_error ==
                   ghostlock::backend::cve_2026_43284::lkm::LkmPolicyError::MissingLkmPath);
        }
        {
            Cve2026_43284Profile p = make_profile();
            p.carrier_path = 99U;
            FakeChain chain{}; reset_chain(chain); UmhForwardInput out{};
            const BackendTerminalResult r = run_case(p, dev, chain, out);
            assert(r.error == BackendTerminalError::CarrierRejected);
        }
    }

    void test_precheck_rejected() {
        FakeDevice dev{};
        dev.precheck_ok = false;
        FakeChain chain{};
        reset_chain(chain);
        RootProgram root{};
        root.kind = RootProgramKind::KernelSU;
        root.set_argv("/data/adb/ksud");
        IpsecSaParams sa{};
        BackendTerminalDeps deps = make_deps(dev, chain);
        deps.precheck_lkm = precheck_ok;
        deps.precheck_ctx = &dev;
        deps.lkm_image_path = "/data/local/tmp/dirtyfrag.ko";
        UmhForwardInput out{};
        const BackendTerminalResult r =
                run_backend_terminal(make_profile(), root, sa, deps, false, out);
        assert(r.error == BackendTerminalError::LkmPrecheckRejected);
        assert(!r.ready);
    }

    void test_chain_rejected() {
        FakeDevice dev{};
        FakeChain chain{};
        reset_chain(chain);
        chain.wait = ChainWaitOutcome::Failed;
        UmhForwardInput out{};
        const BackendTerminalResult r = run_case(make_profile(), dev, chain, out);
        assert(r.error == BackendTerminalError::ChainRejected);
        assert(r.chain.error == ChainError::LkmFailed);
        assert(!r.ready);
        assert(!out.lkm_loaded);
        assert(out.session_secrets == nullptr);
        assert(chain.release_calls == 1U);
    }

    void test_carrier_tokens() {
        const ghostlock::backend::cve_2026_43284::steps::CarrierTarget *primary = nullptr;
        std::size_t count = 7U;
        assert(resolve_carrier_token(kCarrierTokenDefault, primary, count));
        assert(primary == nullptr);
        assert(count == 0U);
        assert(resolve_carrier_token(kCarrierTokenLibbinderdebug, primary, count));
        assert(primary == &kDefaultCarriers[0]);
        assert(count == 1U);
        assert(resolve_carrier_token(kCarrierTokenMax, primary, count));
        assert(primary == &kDefaultCarriers[3]);
        assert(!resolve_carrier_token(99U, primary, count));
    }

    void test_proc_version_marker() {
        assert(ghostlock::platform::proc_version_indicates_fixed("foo f4c50a4 bar"));
        assert(!ghostlock::platform::proc_version_indicates_fixed("Linux 5.15.202"));
    }

    /* ---- B5-8: the injected UMH channel handle reaches the terminal input ---- */

    UmhForwardOutcome dummy_forward(void *, const RootProgram &, const UmhCommand &,
                                    std::uint32_t) noexcept {
        return UmhForwardOutcome::Ready;
    }

    void test_umh_channel_handoff() {
        FakeDevice dev{};
        FakeChain chain{};
        reset_chain(chain);
        RootProgram root{};
        root.kind = RootProgramKind::KernelSU;
        root.set_argv("/data/adb/ksud");
        IpsecSaParams sa{};
        BackendTerminalDeps deps = make_deps(dev, chain);
        deps.umh_channel.ctx = &dev;
        deps.umh_channel.forward = dummy_forward;
        deps.umh_channel.wait_timeout_ms = 777U;
        UmhForwardInput out{};

        const BackendTerminalResult r =
                run_backend_terminal(make_profile(), root, sa, deps, false, out);
        assert(r.ready);
        assert(out.channel.ctx == &dev);
        assert(out.channel.forward == dummy_forward);
        assert(out.channel.wait_timeout_ms == 777U);
        assert(out.channel.valid());
    }

    /* ---- the policy's CoreSession state seam ---- */

    void test_policy_state_seam() {
        FakeDevice dev{};
        FakeChain chain{};
        reset_chain(chain);
        CoreSession session;
        Cve2026_43284Policy::state_construct(session);
        ghostlock::backend::cve_2026_43284::Cve2026_43284State &state =
                ghostlock::backend::cve_2026_43284::cve_2026_43284_state(session);
        state.profile = make_profile();
        state.root_program.kind = RootProgramKind::KernelSU;
        state.root_program.set_argv("/data/adb/ksud");
        state.deps = make_deps(dev, chain);
        UmhForwardInput out{};
        const StageResult result = Cve2026_43284Policy::run(
                session, ghostlock::profile::kernel_offsets{}, nullptr, false, out);
        assert(result == StageResult::Continue);
        assert(out.lkm_loaded);
        assert(out.session_secrets == &state.sa);
        Cve2026_43284Policy::state_destroy(session);
        assert(!session.backend_state_ready);
    }

} // namespace

int main() {
    test_success();
    test_device_facts_fail_closed();
    test_symbols_restricted_still_ready();
    test_patched_kernel();
    test_policy_rejections();
    test_precheck_rejected();
    test_chain_rejected();
    test_carrier_tokens();
    test_proc_version_marker();
    test_umh_channel_handoff();
    test_policy_state_seam();
    std::puts("cve_2026_43284_backend_terminal_test: OK");
    return 0;
}
