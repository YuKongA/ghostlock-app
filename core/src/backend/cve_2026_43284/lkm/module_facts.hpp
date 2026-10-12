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
        /** True when the module carries an allocatable __versions (or .versions) section. This is
         *  the answer the kernel's own check derives from find_sec("__versions"), and it is what
         *  decides how much of the vermagic is compared -- see vermagic_matches_kernel. */
        bool has_crcs = false;
        /** True when a module signature is appended: the loader refuses one without the matching
         *  key, so a caller that cannot check the key refuses it here instead. */
        bool signed_module = false;
        /** True when the module was built with CFI checks. Informational: it says which toolchain
         *  produced it, and nothing on this path branches on it. */
        bool cfi_present = false;
        ModuleReadError error = ModuleReadError::None;
    };

    /** Read the facts out of the bytes of a module. Nothing is loaded. */
    [[nodiscard]] ModuleFacts read_module_facts(const std::uint8_t *bytes,

                                               std::size_t size) noexcept;

    /** True when the vermagic names this release. The vermagic carries the
     *  release as its first word, followed by the features the kernel was built
     *  with, so the comparison is on the first word and on a boundary. This is the strict rule: a
     *  module it accepts matches the running release exactly, which is the strongest case and not
     *  the only one the kernel accepts. */
    [[nodiscard]] bool vermagic_belongs_to(const char *vermagic, const char *release) noexcept;

    /**
     *  The kernel's own comparison, check_modinfo() -> same_magic(), written out because the device
     *  answers differently from the strict rule above.
     *
     *  With CRCs the kernel compares only the part of each string after its first space, so the
     *  release word is not compared at all and a module built for another release of the same KMI
     *  loads; without CRCs the whole string has to match. That is why the same device both refuses a
     *  module by this tree's strict rule and loads it in practice: the release differs, the feature
     *  tail does not.
     *
     *  @param module_vermagic The module's own .modinfo vermagic.
     *  @param kernel_vermagic The running kernel's VERMAGIC_STRING, composed from the release and the
     *                         build features (include/linux/vermagic.h).
     *  @param module_has_crcs ModuleFacts::has_crcs for the same module.
     *  @return True when the kernel would accept the module's vermagic.
     *  @note   Mirrors same_magic() exactly, including its behaviour when a string has no space: the
     *          offset then lands on the terminator and the comparison is of empty tails.
     */
    [[nodiscard]] bool vermagic_matches_kernel(const char *module_vermagic,
                                               const char *kernel_vermagic,
                                               bool module_has_crcs) noexcept;
}  // namespace ghostlock::backend::cve_2026_43284::lkm
