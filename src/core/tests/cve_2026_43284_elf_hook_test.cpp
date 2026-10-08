/* Host tests for B5-5: read-only ELF parsing, hook-target location, trampoline
 * planning and parameterized shellcode.
 *
 * No device, .so sample or page-cache write is involved. The ELF fixture is a
 * synthesized ELF64 AArch64 shared object with a real program-header table, a
 * real section-header table and a .dynsym/.dynstr/.rela.dyn pair, so the parser
 * exercises the exact layout the device path will read. Covered: header/range
 * rejection, section and symbol resolution, executable-section and relocation
 * fail-closed rules, the BTI/PAC reject and skip policies, branch/trampoline
 * encoding and range limits, and shellcode parameter encoding with NUL bytes
 * and slot boundary checks. */

#include "backend/cve_2026_43284/steps/elf_hook.hpp"
#include "backend/cve_2026_43284/steps/shellcode.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {
    using ghostlock::backend::cve_2026_43284::steps::ElfError;
    using ghostlock::backend::cve_2026_43284::steps::ElfImage;
    using ghostlock::backend::cve_2026_43284::steps::ElfSymbol;
    using ghostlock::backend::cve_2026_43284::steps::HookGuardPolicy;
    using ghostlock::backend::cve_2026_43284::steps::HookPlan;
    using ghostlock::backend::cve_2026_43284::steps::HookPlanError;
    using ghostlock::backend::cve_2026_43284::steps::HookTarget;
    using ghostlock::backend::cve_2026_43284::steps::ShellcodeBinding;
    using ghostlock::backend::cve_2026_43284::steps::ShellcodeError;
    using ghostlock::backend::cve_2026_43284::steps::ShellcodeSlotKind;
    using ghostlock::backend::cve_2026_43284::steps::ShellcodeSlotSpec;
    using ghostlock::backend::cve_2026_43284::steps::ShellcodeTemplate;
    using ghostlock::backend::cve_2026_43284::steps::build_hook_plan;
    using ghostlock::backend::cve_2026_43284::steps::build_shellcode;
    using ghostlock::backend::cve_2026_43284::steps::encode_branch;
    using ghostlock::backend::cve_2026_43284::steps::encode_mov_imm64;
    using ghostlock::backend::cve_2026_43284::steps::find_elf_symbol;
    using ghostlock::backend::cve_2026_43284::steps::is_prologue_guard;
    using ghostlock::backend::cve_2026_43284::steps::kAarch64BtiC;
    using ghostlock::backend::cve_2026_43284::steps::kAarch64Paciasp;
    using ghostlock::backend::cve_2026_43284::steps::locate_hook_target;
    using ghostlock::backend::cve_2026_43284::steps::locate_hook_target_at;
    using ghostlock::backend::cve_2026_43284::steps::parse_elf_image;

    constexpr std::uint64_t kTextOff = 0x100U;
    constexpr std::uint64_t kTextVaddr = 0x2000U;
    constexpr std::uint64_t kTextSize = 0x80U;
    constexpr std::uint64_t kPayloadOff = kTextOff + kTextSize;   /* 0x180 */
    constexpr std::uint64_t kPayloadVaddr = kTextVaddr + kTextSize; /* 0x2080 */
    constexpr std::uint64_t kPayloadMax = 0x100U;

    constexpr std::uint32_t kHookWord = 0xAA0103E0U;
    constexpr std::uint32_t kRetWord = 0xD65F03C0U;
    constexpr std::uint32_t kNopWord = 0xD503201FU;

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

    std::uint64_t read64(const std::uint8_t *p) {
        return static_cast<std::uint64_t>(read32(p)) |
               (static_cast<std::uint64_t>(read32(p + 4U)) << 32U);
    }

    /* Offsets the tests may mutate to exercise fail-closed branches. */
    struct Fixture final {
        std::vector<std::uint8_t> bytes;
        std::uint64_t shoff = 0U;
        std::uint64_t text_sh = 0U;
        std::uint64_t dynsym_sh = 0U;
        std::uint64_t dynstr_sh = 0U;
        std::uint64_t text_name_off = 0U;
        std::uint64_t data_name_off = 0U;
        std::uint64_t dynsym_size = 0U;
    };

    Fixture build_fixture(bool relocation_at_hook) {
        Fixture fx;
        std::vector<std::uint8_t> &out = fx.bytes;

        std::string dynstr;
        dynstr.push_back('\0');
        const auto add_name = [&dynstr](std::string_view s) {
            const std::uint32_t off = static_cast<std::uint32_t>(dynstr.size());
            dynstr.append(s);
            dynstr.push_back('\0');
            return off;
        };
        const std::uint32_t n_hook = add_name("hook_me");
        const std::uint32_t n_bti = add_name("bti_entry");
        const std::uint32_t n_pac = add_name("pac_entry");
        const std::uint32_t n_undef = add_name("undefined_sym");
        const std::uint32_t n_data_sym = add_name("data_sym");
        const std::uint32_t n_out = add_name("out_of_text");

        std::string shstr;
        shstr.push_back('\0');
        const auto add_sec = [&shstr](std::string_view s) {
            const std::uint32_t off = static_cast<std::uint32_t>(shstr.size());
            shstr.append(s);
            shstr.push_back('\0');
            return off;
        };
        const std::uint32_t s_text = add_sec(".text");
        const std::uint32_t s_data = add_sec(".data");
        const std::uint32_t s_dynstr = add_sec(".dynstr");
        const std::uint32_t s_dynsym = add_sec(".dynsym");
        const std::uint32_t s_rela = add_sec(".rela.dyn");
        const std::uint32_t s_shstr = add_sec(".shstrtab");
        fx.text_name_off = s_text;
        fx.data_name_off = s_data;

        const std::uint64_t kDynstrOff = 0x200U;
        const std::uint64_t kDynsymOff = (kDynstrOff + dynstr.size() + 7U) & ~std::uint64_t{7U};
        const std::uint64_t kDynsymSize = 7U * 24U;
        const std::uint64_t kRelaOff = kDynsymOff + kDynsymSize;
        const std::uint64_t kShstrOff = (kRelaOff + 24U + 7U) & ~std::uint64_t{7U};
        const std::uint64_t kShoff = (kShstrOff + shstr.size() + 7U) & ~std::uint64_t{7U};
        const std::uint64_t kDataOff = (kShoff + 7U * 64U + 7U) & ~std::uint64_t{7U};

        out.assign(static_cast<std::size_t>(kDataOff + 0x20U), 0U);

        out[0] = 0x7FU;
        out[1] = 'E';
        out[2] = 'L';
        out[3] = 'F';
        out[4] = 2U;
        out[5] = 1U;
        out[6] = 1U;
        put16(out, 0x10U, 3U);    /* ET_DYN */
        put16(out, 0x12U, 0xB7U); /* EM_AARCH64 */
        put32(out, 0x14U, 1U);    /* EV_CURRENT */
        put64(out, 0x20U, 0x40U); /* e_phoff */
        put64(out, 0x28U, kShoff);
        put16(out, 0x36U, 56U);
        put16(out, 0x38U, 1U);
        put16(out, 0x3AU, 64U);
        put16(out, 0x3CU, 7U);
        put16(out, 0x3EU, 6U);

        put32(out, 0x40U, 1U);          /* PT_LOAD */
        put32(out, 0x44U, 5U);          /* PF_R | PF_X */
        put64(out, 0x48U, kTextOff);    /* p_offset */
        put64(out, 0x50U, kTextVaddr);  /* p_vaddr */
        put64(out, 0x60U, kTextSize);   /* p_filesz */
        put64(out, 0x68U, 0x180U);      /* p_memsz */

        const std::uint32_t text_words[8] = {
            kNopWord, kNopWord, kHookWord, kRetWord, kAarch64BtiC, kHookWord,
            kAarch64Paciasp, kHookWord,
        };
        for (std::size_t i = 0U; i < 8U; ++i) {
            put32(out, static_cast<std::size_t>(kTextOff) + i * 4U, text_words[i]);
        }
        for (std::size_t i = 8U; i < kTextSize / 4U; ++i) {
            put32(out, static_cast<std::size_t>(kTextOff) + i * 4U, kNopWord);
        }

        std::memcpy(out.data() + kDynstrOff, dynstr.data(), dynstr.size());

        const auto put_symbol = [&out, kDynsymOff](std::size_t index, std::uint32_t name,
                                                   std::uint16_t shndx, std::uint64_t value,
                                                   std::uint64_t size) {
            const std::size_t at = static_cast<std::size_t>(kDynsymOff) + index * 24U;
            put32(out, at, name);
            out[at + 4U] = 0x12U; /* STB_GLOBAL | STT_FUNC */
            put16(out, at + 6U, shndx);
            put64(out, at + 8U, value);
            put64(out, at + 16U, size);
        };
        put_symbol(1U, n_hook, 1U, kTextVaddr + 0x8U, 8U);
        put_symbol(2U, n_bti, 1U, kTextVaddr + 0x10U, 4U);
        put_symbol(3U, n_pac, 1U, kTextVaddr + 0x18U, 4U);
        put_symbol(4U, n_undef, 0U, 0U, 0U);
        put_symbol(5U, n_data_sym, 2U, 0x3000U, 8U);
        put_symbol(6U, n_out, 1U, kTextVaddr + kTextSize, 4U);

        put64(out, static_cast<std::size_t>(kRelaOff),
              relocation_at_hook ? (kTextVaddr + 0x8U) : (kTextVaddr + 0x40U));
        put64(out, static_cast<std::size_t>(kRelaOff) + 8U, 0U);

        std::memcpy(out.data() + kShstrOff, shstr.data(), shstr.size());

        const auto put_section = [&out, kShoff](std::size_t index, std::uint32_t name,
                                                std::uint32_t type, std::uint64_t flags,
                                                std::uint64_t addr, std::uint64_t off,
                                                std::uint64_t size, std::uint32_t link) {
            const std::size_t at = static_cast<std::size_t>(kShoff) + index * 64U;
            put32(out, at, name);
            put32(out, at + 4U, type);
            put64(out, at + 8U, flags);
            put64(out, at + 0x10U, addr);
            put64(out, at + 0x18U, off);
            put64(out, at + 0x20U, size);
            put32(out, at + 0x28U, link);
            put64(out, at + 0x38U, index == 1U ? 4U : (index == 4U || index == 5U ? 24U : 1U));
        };
        put_section(1U, s_text, 1U, 0x6U, kTextVaddr, kTextOff, kTextSize, 0U);
        put_section(2U, s_data, 1U, 0x2U, 0x3000U, kDataOff, 0x20U, 0U);
        put_section(3U, s_dynstr, 3U, 0x2U, 0x4000U, kDynstrOff, dynstr.size(), 0U);
        put_section(4U, s_dynsym, 11U, 0x2U, 0x5000U, kDynsymOff, kDynsymSize, 3U);
        put_section(5U, s_rela, 4U, 0x2U, 0x6000U, kRelaOff, 24U, 4U);
        put_section(6U, s_shstr, 3U, 0U, 0x7000U, kShstrOff, shstr.size(), 0U);

        fx.shoff = kShoff;
        fx.text_sh = kShoff + 64U;
        fx.dynstr_sh = kShoff + 3U * 64U;
        fx.dynsym_sh = kShoff + 4U * 64U;
        fx.dynsym_size = kDynsymSize;
        return fx;
    }

    ElfImage parse_or_die(const Fixture &fx) {
        ElfImage image{};
        ElfError error = ElfError::None;
        const bool ok = parse_elf_image(fx.bytes.data(), fx.bytes.size(), image, error);
        assert(ok);
        assert(error == ElfError::None);
        return image;
    }

    /* B5-9h-2 payload-room fixtures: reuse the base ELF and retune the
     * executable PT_LOAD. When add_next_load is set a second, non-executable
     * PT_LOAD is written into the free program-header slot at 0x78 (the base
     * fixture declares one header). file_size may be enlarged: the parser only
     * validates referenced ranges against the image size. */
    Fixture build_room_fixture(std::uint64_t filesz, std::uint64_t memsz,
                               std::uint64_t align, std::size_t file_size,
                               bool add_next_load, std::uint64_t next_offset,
                               std::uint64_t next_vaddr, std::uint64_t next_filesz) {
        Fixture fx = build_fixture(false);
        fx.bytes.resize(file_size, 0U);
        put64(fx.bytes, 0x60U, filesz); /* exec p_filesz */
        put64(fx.bytes, 0x68U, memsz);  /* exec p_memsz */
        put64(fx.bytes, 0x70U, align);  /* exec p_align */
        if (add_next_load) {
            put16(fx.bytes, 0x38U, 2U); /* e_phnum */
            put32(fx.bytes, 0x78U, 1U); /* PT_LOAD */
            put32(fx.bytes, 0x7CU, 6U); /* PF_R | PF_W */
            put64(fx.bytes, 0x80U, next_offset);
            put64(fx.bytes, 0x88U, next_vaddr);
            put64(fx.bytes, 0x90U, next_filesz);
            put64(fx.bytes, 0x98U, next_filesz);
            put64(fx.bytes, 0xA0U, align);
        }
        return fx;
    }

    HookTarget locate_room_target(const Fixture &fx, ElfImage &image) {
        image = parse_or_die(fx);
        HookTarget target{};
        ElfError error = ElfError::None;
        assert(locate_hook_target(fx.bytes.data(), fx.bytes.size(), image, "hook_me",
                                  HookGuardPolicy::Reject, target, error));
        assert(error == ElfError::None);
        return target;
    }
} // namespace

int main() {
    const Fixture fixture = build_fixture(false);

    /* ---- parse_elf_image: valid image and range facts. ---- */
    {
        ElfImage image{};
        ElfError error = ElfError::None;
        assert(parse_elf_image(fixture.bytes.data(), fixture.bytes.size(), image, error));
        assert(error == ElfError::None);
        assert(image.type == 3U);
        assert(image.machine == 0xB7U);
        assert(image.shnum == 7U);
        assert(image.has_text && image.has_dynsym && image.has_dynstr && image.has_rela_dyn);
        assert(!image.has_rela_plt);
        assert(image.text.file_offset == kTextOff);
        assert(image.text.size == kTextSize);
        assert(image.dynsym.size == fixture.dynsym_size);
        assert(image.exec_segment.file_offset == kTextOff);
        assert(image.exec_segment.vaddr == kTextVaddr);
        assert(image.exec_segment.size == kTextSize);
        assert(image.exec_memsz == 0x180U);
    }

    /* ---- parse_elf_image: rejection branches. ---- */
    {
        ElfImage image{};
        ElfError error = ElfError::None;
        assert(!parse_elf_image(nullptr, 0U, image, error));
        assert(error == ElfError::NullImage);

        const std::vector<std::uint8_t> tiny(10U, 0U);
        assert(!parse_elf_image(tiny.data(), tiny.size(), image, error));
        assert(error == ElfError::TooSmall);

        const std::vector<std::uint8_t> not_elf(64U, 0U);
        assert(!parse_elf_image(not_elf.data(), not_elf.size(), image, error));
        assert(error == ElfError::NotElf);

        std::vector<std::uint8_t> bad = fixture.bytes;
        bad[4] = 1U;
        assert(!parse_elf_image(bad.data(), bad.size(), image, error));
        assert(error == ElfError::Not64Bit);

        bad = fixture.bytes;
        bad[5] = 2U;
        assert(!parse_elf_image(bad.data(), bad.size(), image, error));
        assert(error == ElfError::NotLittleEndian);

        bad = fixture.bytes;
        put16(bad, 0x12U, 62U);
        assert(!parse_elf_image(bad.data(), bad.size(), image, error));
        assert(error == ElfError::NotAarch64);

        bad = fixture.bytes;
        put16(bad, 0x10U, 1U);
        assert(!parse_elf_image(bad.data(), bad.size(), image, error));
        assert(error == ElfError::NotExecutableImage);

        bad = fixture.bytes;
        put16(bad, 0x36U, 32U);
        assert(!parse_elf_image(bad.data(), bad.size(), image, error));
        assert(error == ElfError::BadProgramHeaders);

        bad = fixture.bytes;
        put16(bad, 0x3AU, 32U);
        assert(!parse_elf_image(bad.data(), bad.size(), image, error));
        assert(error == ElfError::BadSectionHeaders);

        bad = fixture.bytes;
        put32(bad, 0x44U, 4U); /* drop PF_X */
        assert(!parse_elf_image(bad.data(), bad.size(), image, error));
        assert(error == ElfError::NoExecutableSegment);

        bad = fixture.bytes;
        put64(bad, static_cast<std::size_t>(fixture.text_sh) + 0x18U, 0xFFFFFF00U);
        assert(!parse_elf_image(bad.data(), bad.size(), image, error));
        assert(error == ElfError::OutOfBounds);

        /* Rename .text away: the parser must not invent a text section. */
        bad = fixture.bytes;
        put32(bad, static_cast<std::size_t>(fixture.text_sh),
              static_cast<std::uint32_t>(fixture.data_name_off));
        assert(!parse_elf_image(bad.data(), bad.size(), image, error));
        assert(error == ElfError::MissingText);
    }

    /* ---- find_elf_symbol: resolution and fail-closed rules. ---- */
    {
        const ElfImage image = parse_or_die(fixture);
        ElfSymbol symbol{};
        ElfError error = ElfError::None;
        assert(find_elf_symbol(fixture.bytes.data(), fixture.bytes.size(), image, "hook_me",
                               symbol, error));
        assert(error == ElfError::None);
        assert(symbol.value == kTextVaddr + 0x8U);
        assert(symbol.size == 8U);
        assert(symbol.section_index == 1U);
        assert(symbol.file_offset == kTextOff + 0x8U);
        assert(symbol.section.file_offset == kTextOff);
        assert(symbol.name == "hook_me");

        assert(!find_elf_symbol(fixture.bytes.data(), fixture.bytes.size(), image, "absent",
                                symbol, error));
        assert(error == ElfError::SymbolNotFound);

        assert(!find_elf_symbol(fixture.bytes.data(), fixture.bytes.size(), image,
                                "undefined_sym", symbol, error));
        assert(error == ElfError::SymbolUndefined);

        assert(!find_elf_symbol(fixture.bytes.data(), fixture.bytes.size(), image, "data_sym",
                                symbol, error));
        assert(error == ElfError::SymbolBadSection);

        assert(!find_elf_symbol(fixture.bytes.data(), fixture.bytes.size(), image,
                                "out_of_text", symbol, error));
        assert(error == ElfError::SymbolBadSection);

        /* A .dynsym size that is not a whole number of entries fails closed. */
        std::vector<std::uint8_t> bad = fixture.bytes;
        put64(bad, static_cast<std::size_t>(fixture.dynsym_sh) + 0x20U,
              fixture.dynsym_size + 1U);
        ElfImage bad_image{};
        assert(parse_elf_image(bad.data(), bad.size(), bad_image, error));
        assert(!find_elf_symbol(bad.data(), bad.size(), bad_image, "hook_me", symbol, error));
        assert(error == ElfError::OutOfBounds);

        /* A renamed .dynsym is reported as missing, not guessed. */
        std::vector<std::uint8_t> no_dynsym = fixture.bytes;
        put32(no_dynsym, static_cast<std::size_t>(fixture.dynsym_sh),
              static_cast<std::uint32_t>(fixture.data_name_off));
        ElfImage no_dynsym_image{};
        assert(parse_elf_image(no_dynsym.data(), no_dynsym.size(), no_dynsym_image, error));
        assert(!no_dynsym_image.has_dynsym);
        assert(!find_elf_symbol(no_dynsym.data(), no_dynsym.size(), no_dynsym_image, "hook_me",
                                symbol, error));
        assert(error == ElfError::MissingDynsym);

        /* A renamed .dynstr is reported as missing. */
        std::vector<std::uint8_t> no_dynstr = fixture.bytes;
        put32(no_dynstr, static_cast<std::size_t>(fixture.dynstr_sh),
              static_cast<std::uint32_t>(fixture.data_name_off));
        ElfImage no_dynstr_image{};
        assert(parse_elf_image(no_dynstr.data(), no_dynstr.size(), no_dynstr_image, error));
        assert(!no_dynstr_image.has_dynstr);
        assert(!find_elf_symbol(no_dynstr.data(), no_dynstr.size(), no_dynstr_image, "hook_me",
                                symbol, error));
        assert(error == ElfError::MissingDynstr);
    }

    /* ---- locate_hook_target: default and guard policies. ---- */
    {
        const ElfImage image = parse_or_die(fixture);
        HookTarget target{};
        ElfError error = ElfError::None;

        assert(locate_hook_target(fixture.bytes.data(), fixture.bytes.size(), image, "hook_me",
                                  HookGuardPolicy::Reject, target, error));
        assert(error == ElfError::None);
        assert(target.valid);
        assert(target.hook_file_offset == kTextOff + 0x8U);
        assert(target.hook_vaddr == kTextVaddr + 0x8U);
        assert(target.symbol_vaddr == kTextVaddr + 0x8U);
        assert(target.displaced_instruction == kHookWord);
        assert(!target.guard_skipped);
        assert(target.payload_file_offset == kPayloadOff);
        assert(target.payload_vaddr == kPayloadVaddr);
        /* Fixture A keeps the historical BSS tail (p_memsz - p_filesz = 0x100)
         * but the page-aligned tail of the segment's last file page reaches EOF
         * first, so the reported room is the larger page-tail value. */
        assert(target.payload_max_bytes ==
               static_cast<std::uint64_t>(fixture.bytes.size()) - kPayloadOff);
        assert(target.payload_max_bytes >= kPayloadMax);

        /* BTI/PAC at the entry: Reject fails, Skip advances one word. */
        assert(!locate_hook_target(fixture.bytes.data(), fixture.bytes.size(), image,
                                   "bti_entry", HookGuardPolicy::Reject, target, error));
        assert(error == ElfError::PrologueGuard);
        assert(locate_hook_target(fixture.bytes.data(), fixture.bytes.size(), image,
                                  "bti_entry", HookGuardPolicy::Skip, target, error));
        assert(target.guard_skipped);
        assert(target.guard_instruction == kAarch64BtiC);
        assert(target.hook_vaddr == kTextVaddr + 0x14U);
        assert(target.displaced_instruction == kHookWord);

        assert(!locate_hook_target(fixture.bytes.data(), fixture.bytes.size(), image,
                                   "pac_entry", HookGuardPolicy::Reject, target, error));
        assert(error == ElfError::PrologueGuard);
        assert(locate_hook_target(fixture.bytes.data(), fixture.bytes.size(), image,
                                  "pac_entry", HookGuardPolicy::Skip, target, error));
        assert(target.guard_skipped);
        assert(target.guard_instruction == kAarch64Paciasp);
        assert(target.hook_vaddr == kTextVaddr + 0x1CU);
        assert(target.displaced_instruction == kHookWord);

        assert(is_prologue_guard(kAarch64BtiC));
        assert(is_prologue_guard(kAarch64Paciasp));
        assert(!is_prologue_guard(kNopWord));
        assert(!is_prologue_guard(kHookWord));
    }

    /* ---- locate_hook_target_at: offset validation. ---- */
    {
        const ElfImage image = parse_or_die(fixture);
        HookTarget target{};
        ElfError error = ElfError::None;

        assert(locate_hook_target_at(fixture.bytes.data(), fixture.bytes.size(), image,
                                     kTextOff + 0x8U, HookGuardPolicy::Reject, target, error));
        assert(target.valid);
        assert(target.hook_vaddr == kTextVaddr + 0x8U);
        assert(target.symbol_size == 0U);

        assert(!locate_hook_target_at(fixture.bytes.data(), fixture.bytes.size(), image,
                                      kTextOff + 0x9U, HookGuardPolicy::Reject, target, error));
        assert(error == ElfError::HookNotPatchable);

        assert(!locate_hook_target_at(fixture.bytes.data(), fixture.bytes.size(), image,
                                      kTextOff + kTextSize, HookGuardPolicy::Reject, target,
                                      error));
        assert(error == ElfError::HookNotPatchable);

        assert(!locate_hook_target_at(fixture.bytes.data(), fixture.bytes.size(), image, 0x80U,
                                      HookGuardPolicy::Reject, target, error));
        assert(error == ElfError::HookNotPatchable);
    }

    /* ---- Relocation overlap fails closed. ---- */
    {
        const Fixture relocated = build_fixture(true);
        const ElfImage image = parse_or_die(relocated);
        HookTarget target{};
        ElfError error = ElfError::None;
        assert(!locate_hook_target(relocated.bytes.data(), relocated.bytes.size(), image,
                                   "hook_me", HookGuardPolicy::Reject, target, error));
        assert(error == ElfError::RelocationOverlap);
    }

    /* ---- Shellcode parameterization. ---- */
    {
        std::uint8_t tmpl[16] = {};
        const ShellcodeSlotSpec slots[3] = {
            {0U, 8U, ShellcodeSlotKind::Immediate64},
            {8U, 4U, ShellcodeSlotKind::Immediate32},
            {12U, 4U, ShellcodeSlotKind::Instruction},
        };
        const ShellcodeTemplate shell_template{tmpl, sizeof(tmpl), slots, 3U};
        const ShellcodeBinding bindings[3] = {
            {0U, 0x1122334455667788ULL, {}},
            {1U, 0x00000000ULL, {}},
            {2U, kRetWord, {}},
        };
        std::uint8_t out[32] = {};
        std::size_t out_size = 0U;
        ShellcodeError error = ShellcodeError::None;
        assert(build_shellcode(shell_template, bindings, 3U, out, sizeof(out), out_size, error));
        assert(error == ShellcodeError::None);
        assert(out_size == 16U);
        assert(read64(out) == 0x1122334455667788ULL);
        assert(read32(out + 8U) == 0U); /* NUL bytes are valid, not rejected. */
        assert(read32(out + 12U) == kRetWord);

        /* ByteString slot: NUL-terminated, NUL-padded, length-bounded. */
        std::uint8_t str_tmpl[16] = {};
        const ShellcodeSlotSpec str_slots[1] = {{4U, 8U, ShellcodeSlotKind::ByteString}};
        const ShellcodeTemplate str_template{str_tmpl, sizeof(str_tmpl), str_slots, 1U};
        const ShellcodeBinding str_binding[1] = {{0U, 0U, "/x/y"}};
        assert(build_shellcode(str_template, str_binding, 1U, out, sizeof(out), out_size, error));
        assert(std::string_view(reinterpret_cast<const char *>(out + 4U)) == "/x/y");
        assert(out[8U] == 0U);

        /* A string that exactly fills the slot has no room for the NUL. */
        const ShellcodeBinding too_long[1] = {{0U, 0U, "12345678"}};
        assert(!build_shellcode(str_template, too_long, 1U, out, sizeof(out), out_size, error));
        assert(error == ShellcodeError::ValueTooLong);

        /* Template-shape and binding failures. */
        const ShellcodeTemplate null_template{nullptr, 16U, nullptr, 0U};
        assert(!build_shellcode(null_template, nullptr, 0U, out, sizeof(out), out_size, error));
        assert(error == ShellcodeError::NullTemplate);

        std::uint8_t odd[6] = {};
        const ShellcodeTemplate odd_template{odd, sizeof(odd), nullptr, 0U};
        assert(!build_shellcode(odd_template, nullptr, 0U, out, sizeof(out), out_size, error));
        assert(error == ShellcodeError::InvalidTemplateSize);

        const ShellcodeSlotSpec oob_slots[1] = {{16U, 8U, ShellcodeSlotKind::Immediate64}};
        const ShellcodeTemplate oob_template{tmpl, sizeof(tmpl), oob_slots, 1U};
        assert(!build_shellcode(oob_template, nullptr, 0U, out, sizeof(out), out_size, error));
        assert(error == ShellcodeError::SlotOutOfBounds);

        const ShellcodeSlotSpec overlap_slots[2] = {
            {0U, 8U, ShellcodeSlotKind::Immediate64},
            {4U, 4U, ShellcodeSlotKind::Immediate32},
        };
        const ShellcodeTemplate overlap_template{tmpl, sizeof(tmpl), overlap_slots, 2U};
        assert(!build_shellcode(overlap_template, nullptr, 0U, out, sizeof(out), out_size, error));
        assert(error == ShellcodeError::SlotOverlap);

        const ShellcodeSlotSpec misaligned_slots[1] = {{2U, 4U, ShellcodeSlotKind::Immediate32}};
        const ShellcodeTemplate misaligned_template{tmpl, sizeof(tmpl), misaligned_slots, 1U};
        assert(!build_shellcode(misaligned_template, nullptr, 0U, out, sizeof(out), out_size,
                                error));
        assert(error == ShellcodeError::SlotMisaligned);

        const ShellcodeSlotSpec bad_size_slots[1] = {{0U, 3U, ShellcodeSlotKind::Immediate32}};
        const ShellcodeTemplate bad_size_template{tmpl, sizeof(tmpl), bad_size_slots, 1U};
        assert(!build_shellcode(bad_size_template, nullptr, 0U, out, sizeof(out), out_size,
                                error));
        assert(error == ShellcodeError::SlotBadSize);

        assert(!build_shellcode(shell_template, bindings, 3U, out, 8U, out_size, error));
        assert(error == ShellcodeError::OutputTooSmall);

        const ShellcodeBinding unknown[1] = {{3U, 0U, {}}};
        assert(!build_shellcode(shell_template, unknown, 1U, out, sizeof(out), out_size, error));
        assert(error == ShellcodeError::UnknownSlot);

        const ShellcodeBinding duplicate[2] = {{0U, 1U, {}}, {0U, 2U, {}}};
        assert(!build_shellcode(shell_template, duplicate, 2U, out, sizeof(out), out_size,
                                error));
        assert(error == ShellcodeError::DuplicateSlot);

        const ShellcodeBinding too_wide[1] = {{1U, 0x100000000ULL, {}}};
        assert(!build_shellcode(shell_template, too_wide, 1U, out, sizeof(out), out_size, error));
        assert(error == ShellcodeError::ValueTooLong);

        assert(!build_shellcode(shell_template, bindings, 3U, nullptr, sizeof(out), out_size,
                                error));
        assert(error == ShellcodeError::NullOutput);
    }

    /* ---- encode_mov_imm64 round-trip. ---- */
    {
        std::uint32_t words[4] = {};
        assert(encode_mov_imm64(0x123456789ABCDEF0ULL, 0U, words));
        assert((words[0] & 0xFFE0001FU) == 0xD2800000U);
        assert((words[1] & 0xFFE0001FU) == 0xF2A00000U);
        assert((words[2] & 0xFFE0001FU) == 0xF2C00000U);
        assert((words[3] & 0xFFE0001FU) == 0xF2E00000U);
        assert(((words[0] >> 5U) & 0xFFFFU) == 0xDEF0U);
        assert(((words[1] >> 5U) & 0xFFFFU) == 0x9ABCU);
        assert(((words[2] >> 5U) & 0xFFFFU) == 0x5678U);
        assert(((words[3] >> 5U) & 0xFFFFU) == 0x1234U);
        assert(encode_mov_imm64(0U, 5U, words));
        assert((words[0] & 0x1FU) == 5U);
        assert(!encode_mov_imm64(0U, 32U, words));
    }

    /* ---- encode_branch: forward, backward, alignment and range. ---- */
    {
        std::uint32_t branch = 0U;
        assert(encode_branch(0x2000U, 0x2080U, branch));
        assert(branch == 0x14000020U);
        assert(encode_branch(0x2080U, 0x2000U, branch));
        assert(branch == (0x14000000U | (0x3FFFFE0U & 0x03FFFFFFU)));
        assert(!encode_branch(0x2001U, 0x2000U, branch));
        assert(encode_branch(0U, (1ULL << 27U) - 4U, branch));
        assert(!encode_branch(0U, 1ULL << 27U, branch));
    }

    /* ---- build_hook_plan: trampoline description and limits. ---- */
    {
        const ElfImage image = parse_or_die(fixture);
        HookTarget target{};
        ElfError error = ElfError::None;
        assert(locate_hook_target(fixture.bytes.data(), fixture.bytes.size(), image, "hook_me",
                                  HookGuardPolicy::Reject, target, error));

        HookPlan plan{};
        HookPlanError plan_error = HookPlanError::None;
        assert(build_hook_plan(target, 16U, plan, plan_error));
        assert(plan_error == HookPlanError::None);
        assert(plan.valid);
        assert(plan.hook_file_offset == kTextOff + 0x8U);
        assert(plan.shellcode_file_offset == kPayloadOff);
        assert(plan.shellcode_vaddr == kPayloadVaddr);
        assert(plan.shellcode_bytes == 16U);
        assert(plan.displaced_instruction == kHookWord);
        std::uint32_t expected = 0U;
        assert(encode_branch(target.hook_vaddr, target.payload_vaddr, expected));
        assert(plan.branch_instruction == expected);
        assert(encode_branch(target.payload_vaddr + 12U, target.hook_vaddr + 4U, expected));
        assert(plan.jump_back_instruction == expected);

        HookTarget crafted = target;
        assert(!build_hook_plan(crafted, 4U, plan, plan_error));
        assert(plan_error == HookPlanError::ShellcodeTooSmall);
        assert(!build_hook_plan(crafted, 10U, plan, plan_error));
        assert(plan_error == HookPlanError::ShellcodeMisaligned);

        crafted.payload_max_bytes = 8U;
        assert(!build_hook_plan(crafted, 16U, plan, plan_error));
        assert(plan_error == HookPlanError::PayloadNotMapped);

        crafted.payload_max_bytes = kPayloadMax;
        crafted.payload_vaddr = target.hook_vaddr + (1ULL << 27U);
        assert(!build_hook_plan(crafted, 16U, plan, plan_error));
        assert(plan_error == HookPlanError::BranchOutOfRange);

        HookTarget invalid{};
        assert(!build_hook_plan(invalid, 16U, plan, plan_error));
        assert(plan_error == HookPlanError::InvalidTarget);
    }

    /* ---- B5-9h-2 payload room: BSS tail vs executable page tail. ---- */
    {
        /* B: filesz == memsz (no BSS tail) but the segment end is mid-page, so
         * the page-aligned tail supplies the room -- the real libc++
         * executable-LOAD shape. p_align mirrors the device's 0x4000, which must
         * not inflate the 4 KiB mapping. */
        const Fixture roomy = build_room_fixture(0xC00U, 0xC00U, 0x4000U, 0x2000U,
                                                 false, 0U, 0U, 0U);
        ElfImage image{};
        const HookTarget target = locate_room_target(roomy, image);
        assert(image.exec_align == 0x4000U);
        assert(target.payload_file_offset == 0xD00U); /* 0x100 + 0xC00 */
        assert(target.payload_vaddr == 0x2C00U);       /* 0x2000 + 0xC00 */
        assert(target.payload_max_bytes == 0x300U);    /* 0xd00 -> 0x1000 */
        HookPlan plan{};
        HookPlanError plan_error = HookPlanError::None;
        assert(build_hook_plan(target, 480U, plan, plan_error));
        assert(plan_error == HookPlanError::None);
        assert(plan.payload_max_bytes == 0x300U);
    }
    {
        /* C: a following non-executable PT_LOAD's page-aligned mapping covers
         * the segment end, so the page tail is rejected. */
        const Fixture covered = build_room_fixture(0xC00U, 0xC00U, 0x1000U, 0x2000U,
                                                   true, 0xE00U, 0x2E00U, 0x300U);
        ElfImage image{};
        const HookTarget target = locate_room_target(covered, image);
        assert(image.phnum == 2U);
        assert(target.payload_max_bytes == 0U);
        HookPlan plan{};
        HookPlanError plan_error = HookPlanError::None;
        assert(!build_hook_plan(target, 480U, plan, plan_error));
        assert(plan_error == HookPlanError::PayloadNotMapped);
    }
    {
        /* D: the page tail would cross EOF, so it is clipped to the file end. */
        const Fixture eof = build_room_fixture(0x1D00U, 0x1D00U, 0x1000U, 0x1F00U,
                                               false, 0U, 0U, 0U);
        ElfImage image{};
        const HookTarget target = locate_room_target(eof, image);
        assert(target.payload_file_offset == 0x1E00U);
        assert(target.payload_max_bytes == 0x100U); /* 0x1e00 -> 0x1f00 EOF */
    }
    {
        /* E: a page tail below the 480-byte shellcode stays fail-closed. */
        const Fixture tight = build_room_fixture(0xE00U, 0xE00U, 0x1000U, 0x2000U,
                                                 false, 0U, 0U, 0U);
        ElfImage image{};
        const HookTarget target = locate_room_target(tight, image);
        assert(target.payload_max_bytes == 0x100U); /* 0xf00 -> 0x1000 */
        HookPlan plan{};
        HookPlanError plan_error = HookPlanError::None;
        assert(!build_hook_plan(target, 480U, plan, plan_error));
        assert(plan_error == HookPlanError::PayloadNotMapped);
    }
    std::puts("cve_2026_43284_elf_hook_test: OK");
    return 0;
}
