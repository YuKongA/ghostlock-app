/** @file tests/module_facts_test.cpp
 *  Reading a kernel module's own declaration without loading it.
 *
 *  The module here is built in memory -- a real arm64 ELF header, a .modinfo
 *  section and a string table -- so the reader is measured against the format
 *  rather than against a file that happens to be on this machine.
 *
 *  Sections: 1. the facts come out; 2. a file that is not a module, or is one the
 *  reader cannot trust, is refused by name; 3. a vermagic names a release or it
 *  does not, including when it belongs to a longer release.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "backend/cve_2026_43284/lkm/module_facts.hpp"

namespace {
    using ghostlock::backend::cve_2026_43284::lkm::ModuleFacts;
    using ghostlock::backend::cve_2026_43284::lkm::ModuleReadError;
    using ghostlock::backend::cve_2026_43284::lkm::module_read_error_name;
    using ghostlock::backend::cve_2026_43284::lkm::read_module_facts;
    using ghostlock::backend::cve_2026_43284::lkm::vermagic_belongs_to;

    constexpr std::size_t kHeader = 64U;
    constexpr std::size_t kSectionHeader = 64U;
    constexpr std::size_t kSectionsAt = kHeader;
    constexpr std::size_t kSections = 3U;
    constexpr std::size_t kModinfoAt = kSectionsAt + kSections * kSectionHeader;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void put16(std::uint8_t *bytes, std::size_t offset, std::uint16_t value) {
        bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
        bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    }

    void put32(std::uint8_t *bytes, std::size_t offset, std::uint32_t value) {
        for (std::size_t i = 0U; i < 4U; ++i) {
            bytes[offset + i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
        }
    }

    void put64(std::uint8_t *bytes, std::size_t offset, std::uint64_t value) {
        for (std::size_t i = 0U; i < 8U; ++i) {
            bytes[offset + i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
        }
    }

    /** Lay out one section header. */
    void section(std::uint8_t *bytes, std::size_t index, std::uint32_t name, std::uint32_t type,
                 std::uint64_t offset, std::uint64_t length) {
        const std::size_t at = kSectionsAt + index * kSectionHeader;
        put32(bytes, at, name);
        put32(bytes, at + 4U, type);
        put64(bytes, at + 24U, offset);
        put64(bytes, at + 32U, length);
    }

    /** A module whose .modinfo carries the given entries, NUL-separated. */
    std::size_t build_module(std::uint8_t *bytes, std::size_t capacity, const char *entries,
                             std::size_t entry_bytes) {
        std::memset(bytes, 0, capacity);
        bytes[0U] = 0x7FU;
        bytes[1U] = 'E';
        bytes[2U] = 'L';
        bytes[3U] = 'F';
        bytes[4U] = 2U; /* 64-bit */
        bytes[5U] = 1U; /* little-endian */
        put16(bytes, 18U, 0xB7U); /* aarch64 */
        put64(bytes, 40U, kSectionsAt);
        put16(bytes, 58U, static_cast<std::uint16_t>(kSectionHeader));
        put16(bytes, 60U, static_cast<std::uint16_t>(kSections));
        put16(bytes, 62U, 2U); /* the string table is section two */
        /* The entry text holds embedded NULs by construction, so its length is
         * given rather than measured: strlen would stop at the first one. */
        const std::size_t modinfo_length = entry_bytes;
        const std::size_t modinfo_end = kModinfoAt + modinfo_length;
        std::memcpy(bytes + kModinfoAt, entries, modinfo_length);
        /* The string table: empty, then the two names. */
        const char table[] = "\0.modinfo\0.shstrtab\0";
        const std::size_t table_at = modinfo_end + 8U;
        std::memcpy(bytes + table_at, table, sizeof(table));
        section(bytes, 1U, 1U, 1U, kModinfoAt, modinfo_length);
        section(bytes, 2U, 10U, 3U, table_at, sizeof(table));
        return table_at + sizeof(table);
    }

    /* An array, not a pointer: the entries carry embedded NULs and the size that
     *  matters is the array's, which a pointer would hide. */
    const char kGoodEntries[] =
        "name=ghostlock\0vermagic=5.15.189-android13-8-00016-g51bba4309aac-ab14546557 SMP preempt "
        "mod_unload modversions aarch64\0";

    /* ---- 1: the facts come out ---- */
    void the_facts_come_out() {
        static std::uint8_t module[4096U];
        const std::size_t size =
            build_module(module, sizeof(module), kGoodEntries, sizeof(kGoodEntries) - 1U);
        const ModuleFacts facts = read_module_facts(module, size);
        expect(facts.elf_valid, "a real arm64 ELF is accepted");
        expect(facts.error == ModuleReadError::None, "with nothing to report");
        expect(facts.has_modinfo && facts.has_name && facts.has_vermagic,
               "and the section is found and read");
        expect(std::strcmp(facts.name, "ghostlock") == 0, "the module names itself");
        expect(std::strncmp(facts.vermagic, "5.15.189-android13", 18U) == 0,
               "and declares the release it was built for");
        expect(vermagic_belongs_to(facts.vermagic,
                                   "5.15.189-android13-8-00016-g51bba4309aac-ab14546557"),
               "which is the device's own release");
    }

    /* ---- 2: what cannot be read ---- */
    void what_cannot_be_read_is_named() {
        static std::uint8_t module[4096U];
        std::size_t size =
            build_module(module, sizeof(module), kGoodEntries, sizeof(kGoodEntries) - 1U);
        expect(read_module_facts(module, 8U).error == ModuleReadError::TooSmall,
               "a file far too small is refused");
        static std::uint8_t not_elf[256U];
        std::memset(not_elf, 0x41, sizeof(not_elf));
        expect(read_module_facts(not_elf, sizeof(not_elf)).error == ModuleReadError::NotElf,
               "a file that is not an ELF is refused");
        expect(std::string_view(module_read_error_name(ModuleReadError::NotElf)) ==
                   std::string_view("target-kind-invalid"),
               "and named as the wrong kind of target");
        std::uint8_t wrong_machine[4096U];
        std::memcpy(wrong_machine, module, size);
        put16(wrong_machine, 18U, 0x3EU); /* x86-64 */
        expect(read_module_facts(wrong_machine, size).error == ModuleReadError::NotAarch64,
               "a module for another machine is refused");
        /* Cut the section headers off the end. */
        expect(read_module_facts(module, kModinfoAt - 1U).error == ModuleReadError::HeadersOutOfRange,
               "headers that run past the file are refused, not followed");
        static std::uint8_t no_vermagic[4096U];
        const char only_name[] = "name=ghostlock\0";
        const std::size_t bare =
            build_module(no_vermagic, sizeof(no_vermagic), only_name, sizeof(only_name) - 1U);
        expect(read_module_facts(no_vermagic, bare).error == ModuleReadError::NoVermagic,
               "a module that declares no release is refused");
        static std::uint8_t no_name[4096U];
        const char only_vermagic[] = "vermagic=5.15.189-android13 aarch64\0";
        const std::size_t unnamed = build_module(no_name, sizeof(no_name), only_vermagic,
                                                 sizeof(only_vermagic) - 1U);
        expect(read_module_facts(no_name, unnamed).error == ModuleReadError::NoName,
               "so is one that does not name itself");
        expect(read_module_facts(nullptr, 4096U).error == ModuleReadError::TooSmall,
               "and no bytes at all");
    }

    /* ---- 3: a vermagic names a release, or does not ---- */
    void vermagic_matching() {
        const char *release = "5.15.189-android13-8-00016-g51bba4309aac-ab14546557";
        expect(vermagic_belongs_to("5.15.189-android13-8-00016-g51bba4309aac-ab14546557 aarch64",
                                   release),
               "the release followed by the features matches");
        expect(vermagic_belongs_to("5.15.189-android13-8-00016-g51bba4309aac-ab14546557", release),
               "and so does the release on its own");
        expect(!vermagic_belongs_to("5.15.189-android13-8-00004-g1c3825f8ac0a ab14110541", release),
               "another build of the same kernel does not match");
        expect(!vermagic_belongs_to("5.15.189-android13-8-00016-g51bba4309aac-ab14546557x", release),
               "nor does a longer release that merely starts the same");
        expect(!vermagic_belongs_to("", release), "nor an empty declaration");
        expect(!vermagic_belongs_to(nullptr, release), "nor a missing one");
        expect(!vermagic_belongs_to("6.6.118-android15 SMP aarch64", ""),
               "and an empty release matches nothing");
    }
}  // namespace

int main() {
    std::printf("module facts: reading them\n");
    the_facts_come_out();
    std::printf("module facts: what cannot be read\n");
    what_cannot_be_read_is_named();
    std::printf("module facts: the vermagic\n");
    vermagic_matching();
    if (g_failures != 0U) {
        std::printf("module facts: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("module facts: PASS (facts read + refusals named + vermagic bounded)\n");
    return 0;
}
