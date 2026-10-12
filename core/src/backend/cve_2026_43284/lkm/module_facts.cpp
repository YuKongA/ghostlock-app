/** @file backend/cve_2026_43284/lkm/module_facts.cpp
 *  Reading a module's own declaration, with a bound on every step.
 */
#include "backend/cve_2026_43284/lkm/module_facts.hpp"

#include <cstring>

namespace ghostlock::backend::cve_2026_43284::lkm {
    namespace {
        /* The little-endian ELF64 header, as the format defines it. */
        constexpr std::size_t kSectionHeaderBytes = 64U;
        constexpr std::uint16_t kElfClass64 = 2U;
        constexpr std::uint16_t kMachineAarch64 = 0xB7U;
        constexpr std::uint32_t kSectionProgbits = 1U;
        /** SHF_ALLOC: the kernel's find_sec() only answers for an allocated section. */
        constexpr std::uint64_t kSectionAlloc = 0x2U;
        constexpr const char *kVersionsSection = "__versions";
        constexpr const char *kVersionsAltSection = ".versions";
        constexpr const char *kSignatureMarker = "~Module signature appended~";
        constexpr const char *kCfiMarker = "__cfi_check";

        bool in_range(std::size_t size, std::size_t offset, std::size_t width) noexcept {
            return offset <= size && width <= size - offset;
        }

        std::uint16_t read_u16(const std::uint8_t *bytes, std::size_t offset) noexcept {
            return static_cast<std::uint16_t>(bytes[offset]) |
                   static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
        }

        std::uint32_t read_u32(const std::uint8_t *bytes, std::size_t offset) noexcept {
            std::uint32_t value = 0U;
            for (std::size_t i = 0U; i < 4U; ++i) {
                value |= static_cast<std::uint32_t>(bytes[offset + i]) << (i * 8U);
            }
            return value;
        }

        std::uint64_t read_u64(const std::uint8_t *bytes, std::size_t offset) noexcept {
            std::uint64_t value = 0U;
            for (std::size_t i = 0U; i < 8U; ++i) {
                value |= static_cast<std::uint64_t>(bytes[offset + i]) << (i * 8U);
            }
            return value;
        }

        /** Copy a NUL-terminated text into a bounded field. */
        bool take_text(const std::uint8_t *bytes, std::size_t size, std::size_t offset,
                       char *out, std::size_t capacity) noexcept {
            std::size_t at = 0U;
            while (offset + at < size && bytes[offset + at] != 0U) {
                if (at + 1U >= capacity) {
                    return false;
                }
                out[at] = static_cast<char>(bytes[offset + at]);
                ++at;
            }
            if (at == 0U || offset + at >= size) {
                return false;
            }
            out[at] = '\0';
            return true;
        }

        /** Does the section name at this offset into the string table equal want? */
        bool section_name_is(const std::uint8_t *bytes, std::size_t size, std::uint64_t table_offset,
                             std::uint64_t table_size, std::uint32_t name_offset,
                             const char *want) noexcept {
            if (name_offset >= table_size) {
                return false;
            }
            const std::size_t at = static_cast<std::size_t>(table_offset + name_offset);
            if (!in_range(size, at, 1U)) {
                return false;
            }
            std::size_t i = 0U;
            while (want[i] != '\0') {
                if (!in_range(size, at + i, 1U) || bytes[at + i] != static_cast<std::uint8_t>(want[i])) {
                    return false;
                }
                ++i;
            }
            return in_range(size, at + i, 1U) && bytes[at + i] == 0U;
        }
        /** Whether the image holds this text anywhere. The loader's two markers are byte strings
         *  in the image, and neither has a section of its own. */
        bool contains_marker(const std::uint8_t *bytes, std::size_t size, const char *marker) noexcept {
            const std::size_t width = std::strlen(marker);
            if (width == 0U || width > size) {
                return false;
            }
            for (std::size_t at = 0U; at + width <= size; ++at) {
                if (std::memcmp(bytes + at, marker, width) == 0) {
                    return true;
                }
            }
            return false;
        }
    }  // namespace

    ModuleFacts read_module_facts(const std::uint8_t *bytes, std::size_t size) noexcept {
        ModuleFacts facts{};
        if (bytes == nullptr || size < kModuleMinBytes) {
            facts.error = ModuleReadError::TooSmall;
            return facts;
        }
        if (size > kModuleMaxBytes) {
            facts.error = ModuleReadError::TooLarge;
            return facts;
        }
        if (bytes[0U] != 0x7FU || bytes[1U] != 'E' || bytes[2U] != 'L' || bytes[3U] != 'F') {
            facts.error = ModuleReadError::NotElf;
            return facts;
        }
        if (bytes[4U] != kElfClass64 || read_u16(bytes, 18U) != kMachineAarch64) {
            facts.error = ModuleReadError::NotAarch64;
            return facts;
        }
        facts.elf_valid = true;
        const std::uint64_t sections_at = read_u64(bytes, 40U);
        const std::uint16_t section_size = read_u16(bytes, 58U);
        const std::uint16_t section_count = read_u16(bytes, 60U);
        const std::uint16_t names_index = read_u16(bytes, 62U);
        if (section_size < kSectionHeaderBytes || section_count == 0U ||
            !in_range(size, static_cast<std::size_t>(sections_at),
                      static_cast<std::size_t>(section_size) * section_count) ||
            names_index >= section_count) {
            facts.error = ModuleReadError::HeadersOutOfRange;
            return facts;
        }
        const std::size_t names_header =
            static_cast<std::size_t>(sections_at) + static_cast<std::size_t>(names_index) * section_size;
        const std::uint64_t names_offset = read_u64(bytes, names_header + 24U);
        const std::uint64_t names_size = read_u64(bytes, names_header + 32U);
        if (!in_range(size, static_cast<std::size_t>(names_offset),
                      static_cast<std::size_t>(names_size))) {
            facts.error = ModuleReadError::HeadersOutOfRange;
            return facts;
        }
        /* The sections the kernel's own check reads, found by name rather than by position. The
         *  CRC sections are looked for first: their absence or presence decides how the vermagic is
         *  compared, and a module declares one only when it was built with versioning. */
        for (std::uint16_t i = 0U; i < section_count; ++i) {
            const std::size_t header =
                static_cast<std::size_t>(sections_at) + static_cast<std::size_t>(i) * section_size;
            const std::uint64_t flags = read_u64(bytes, header + 8U);
            if ((flags & kSectionAlloc) == 0U) {
                continue;
            }
            const std::uint32_t name_offset = read_u32(bytes, header);
            if (section_name_is(bytes, size, names_offset, names_size, name_offset,
                                kVersionsSection) ||
                section_name_is(bytes, size, names_offset, names_size, name_offset,
                                kVersionsAltSection)) {
                /* The kernel's find_sec() answers for any allocated section of this name, even an
                 * empty one, and that answer is what turns the comparison into a tail comparison. */
                facts.has_crcs = true;
            }
        }
        facts.signed_module = contains_marker(bytes, size, kSignatureMarker);
        facts.cfi_present = contains_marker(bytes, size, kCfiMarker);
        /* The .modinfo section, found by name rather than by position. */
        for (std::uint16_t i = 0U; i < section_count; ++i) {
            const std::size_t header =
                static_cast<std::size_t>(sections_at) + static_cast<std::size_t>(i) * section_size;
            if (read_u32(bytes, header + 4U) != kSectionProgbits) {
                continue;
            }
            if (!section_name_is(bytes, size, names_offset, names_size, read_u32(bytes, header), ".modinfo")) {
                continue;
            }
            const std::uint64_t offset = read_u64(bytes, header + 24U);
            const std::uint64_t length = read_u64(bytes, header + 32U);
            if (!in_range(size, static_cast<std::size_t>(offset), static_cast<std::size_t>(length))) {
                facts.error = ModuleReadError::HeadersOutOfRange;
                return facts;
            }
            facts.has_modinfo = true;
            /* The entries are NUL-separated key=value texts. */
            std::size_t at = static_cast<std::size_t>(offset);
            const std::size_t end = at + static_cast<std::size_t>(length);
            while (at < end) {
                if (bytes[at] == 0U) {
                    ++at;
                    continue;
                }
                const bool is_name = std::strncmp(reinterpret_cast<const char *>(bytes + at), "name=", 5U) == 0;
                const bool is_vermagic =
                    std::strncmp(reinterpret_cast<const char *>(bytes + at), "vermagic=", 9U) == 0;
                if (is_name) {
                    facts.has_name = take_text(bytes, end, at + 5U, facts.name, sizeof(facts.name));
                } else if (is_vermagic) {
                    facts.has_vermagic =
                        take_text(bytes, end, at + 9U, facts.vermagic, sizeof(facts.vermagic));
                }
                while (at < end && bytes[at] != 0U) {
                    ++at;
                }
            }
            break;
        }
        if (!facts.has_modinfo) {
            facts.error = ModuleReadError::NoModinfo;
        } else if (!facts.has_name) {
            facts.error = ModuleReadError::NoName;
        } else if (!facts.has_vermagic) {
            facts.error = ModuleReadError::NoVermagic;
        }
        return facts;
    }

    bool vermagic_matches_kernel(const char *module_vermagic, const char *kernel_vermagic,
                                 bool module_has_crcs) noexcept {
        if (module_vermagic == nullptr || kernel_vermagic == nullptr) {
            return false;
        }
        if (!module_has_crcs) {
            return std::strcmp(module_vermagic, kernel_vermagic) == 0;
        }
        /* strcspn, not strchr: a string with no space moves the offset to its terminator, which is
         * what the kernel's own arithmetic does. */
        const char *const module_tail = module_vermagic + std::strcspn(module_vermagic, " ");
        const char *const kernel_tail = kernel_vermagic + std::strcspn(kernel_vermagic, " ");
        return std::strcmp(module_tail, kernel_tail) == 0;
    }

    bool vermagic_belongs_to(const char *vermagic, const char *release) noexcept {
        if (vermagic == nullptr || release == nullptr || release[0U] == '\0') {
            return false;
        }
        std::size_t i = 0U;
        while (release[i] != '\0') {
            if (vermagic[i] != release[i]) {
            return false;
            }
            ++i;
        }
        /* The release has to end where the next word begins: a vermagic for a
         * longer release must not match a shorter one that prefixes it. */
        return vermagic[i] == '\0' || vermagic[i] == ' ';
    }
}  // namespace ghostlock::backend::cve_2026_43284::lkm
