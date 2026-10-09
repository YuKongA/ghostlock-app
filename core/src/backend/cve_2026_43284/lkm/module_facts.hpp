#pragma once
/** @file backend/cve_2026_43284/lkm/module_facts.hpp
 *  What a kernel module says about itself, read without loading it.
 *
 *  A module carries its own declaration in a .modinfo section: its name, the
 *  kernel release it was built for, and the features that must agree. Reading it
 *  is the cheapest way to refuse a module that would not load -- or worse, would
 *  load into a kernel it does not belong to -- and it touches nothing: no file is
 *  written, nothing is loaded, no module is registered.
 *
 *  The reader is bounded on every step and reports what it found rather than
 *  what it hoped for: a file that is not an arm64 ELF, one with no .modinfo, or
 *  one whose entries are cut off are each named. A vermagic that does not name
 *  the running release is not rewritten here; there is no rewriting in this tree.
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43284::lkm {
    /** Bounds the reader keeps, so a hostile file cannot make it work forever. */
    inline constexpr std::size_t kModuleMinBytes = 64U;
    inline constexpr std::size_t kModuleMaxBytes = std::size_t{64U} * 1024U * 1024U;
    inline constexpr std::size_t kVermagicMaxBytes = 192U;
    inline constexpr std::size_t kModuleNameMaxBytes = 64U;

    /** Why a module could not be read. */
    enum class ModuleReadError : unsigned char {
        None = 0,
        TooSmall = 1,
        TooLarge = 2,
        NotElf = 3,
        NotAarch64 = 4,
        HeadersOutOfRange = 5,
        NoModinfo = 6,
        NoVermagic = 7,
        NoName = 8,
    };

    [[nodiscard]] constexpr const char *module_read_error_name(ModuleReadError error) noexcept {
        switch (error) {
            case ModuleReadError::None: return "ok";
            case ModuleReadError::TooSmall: return "param-invalid";
            case ModuleReadError::TooLarge: return "budget-exceeded";
            case ModuleReadError::NotElf: return "target-kind-invalid";
            case ModuleReadError::NotAarch64: return "target-kind-invalid";
            case ModuleReadError::HeadersOutOfRange: return "param-invalid";
            case ModuleReadError::NoModinfo: return "tuning-missing";
            case ModuleReadError::NoVermagic: return "tuning-missing";
            case ModuleReadError::NoName: return "tuning-missing";
        }
        return "unknown";
    }

    /** What the module declares. */
    struct ModuleFacts final {
        bool elf_valid = false;
        bool has_modinfo = false;
        bool has_name = false;
        bool has_vermagic = false;
        char name[kModuleNameMaxBytes] = {};
        char vermagic[kVermagicMaxBytes] = {};
        ModuleReadError error = ModuleReadError::None;
    };

    /** Read the facts out of the bytes of a module. Nothing is loaded. */
    [[nodiscard]] ModuleFacts read_module_facts(const std::uint8_t *bytes,

                                               std::size_t size) noexcept;

    /** True when the vermagic names this release. The vermagic carries the
     *  release as its first word, followed by the features the kernel was built
     *  with, so the comparison is on the first word and on a boundary. */
    [[nodiscard]] bool vermagic_belongs_to(const char *vermagic, const char *release) noexcept;
}  // namespace ghostlock::backend::cve_2026_43284::lkm
