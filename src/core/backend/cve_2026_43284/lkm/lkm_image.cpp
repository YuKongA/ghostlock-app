/* CVE-2026-43284 LKM image precheck, vermagic rewrite and UMH command (B5-4,
 * B5-9h-3) -- implementation.
 *
 * Everything here is host-testable and side-effect free apart from reading the
 * .ko path the caller names; the module is never mmap'd, insmod'd or executed. */

#include "backend/cve_2026_43284/lkm/lkm_image.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace ghostlock::backend::cve_2026_43284::lkm {
    namespace {
        constexpr std::size_t kElfHeaderBytes = 64U;
        constexpr std::size_t kElfSectionHeaderBytes = 64U;
        constexpr std::uint16_t kElfTypeRelocatable = 1U;
        constexpr std::uint16_t kElfMachineAarch64 = 0xB7U;
        constexpr std::string_view kModinfoSection = ".modinfo";
        constexpr std::string_view kVersionsSection = "__versions";
        constexpr std::string_view kVersionsAltSection = ".versions";
        constexpr std::string_view kSignatureMarker = "~Module signature appended~";
        constexpr std::string_view kCfiMarker = "__cfi_check";
        constexpr std::string_view kNamePrefix = "name=";
        constexpr std::string_view kVermagicPrefix = "vermagic=";
        constexpr std::string_view kSmpToken = "SMP";
        constexpr std::string_view kPreemptToken = "preempt";
        constexpr std::string_view kModuleUnloadToken = "mod_unload";
        constexpr std::string_view kModversionsToken = "modversions";
        constexpr std::string_view kArchToken = "aarch64";
        constexpr std::string_view kLinuxVersionPrefix = "Linux version ";

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

        bool contains_bytes(const std::uint8_t *data, std::size_t size,
                            std::string_view needle) noexcept {
            if (needle.empty() || needle.size() > size) {
                return false;
            }
            const std::size_t last = size - needle.size();
            for (std::size_t i = 0U; i <= last; ++i) {
                if (std::memcmp(data + i, needle.data(), needle.size()) == 0) {
                    return true;
                }
            }
            return false;
        }

        struct SectionView final {
            std::uint64_t offset = 0U;
            std::uint64_t size = 0U;
        };

        /* Bounded NUL-terminated view at data[begin..end). */
        std::string_view bounded_cstr(const std::uint8_t *data, std::size_t begin,
                                      std::size_t end) noexcept {
            std::size_t pos = begin;
            while (pos < end && data[pos] != 0U) {
                ++pos;
            }
            return std::string_view(reinterpret_cast<const char *>(data + begin), pos - begin);
        }

        bool find_section(const std::uint8_t *data, std::size_t size, std::string_view name,
                          SectionView &out) noexcept {
            if (size < kElfHeaderBytes) {
                return false;
            }
            const std::uint64_t shoff = read_u64(data + 0x28U);
            const std::uint16_t shentsize = read_u16(data + 0x3AU);
            const std::uint16_t shnum = read_u16(data + 0x3CU);
            const std::uint16_t shstrndx = read_u16(data + 0x3EU);
            if (shentsize != kElfSectionHeaderBytes || shnum == 0U || shstrndx >= shnum) {
                return false;
            }
            if (shoff > size ||
                static_cast<std::uint64_t>(shnum) * kElfSectionHeaderBytes >
                        static_cast<std::uint64_t>(size) - shoff) {
                return false;
            }
            const std::uint8_t *shstr = data + shoff +
                                        static_cast<std::size_t>(shstrndx) *
                                                kElfSectionHeaderBytes;
            const std::uint64_t str_off = read_u64(shstr + 0x18U);
            const std::uint64_t str_size = read_u64(shstr + 0x20U);
            if (str_off > size || str_size > static_cast<std::uint64_t>(size) - str_off) {
                return false;
            }
            const std::size_t str_begin = static_cast<std::size_t>(str_off);
            const std::size_t str_end = static_cast<std::size_t>(str_off + str_size);
            for (std::uint16_t i = 0U; i < shnum; ++i) {
                const std::uint8_t *sh =
                        data + shoff + static_cast<std::size_t>(i) * kElfSectionHeaderBytes;
                const std::uint32_t name_off = read_u32(sh);
                if (static_cast<std::uint64_t>(name_off) >= str_size) {
                    continue;
                }
                const std::string_view section_name = bounded_cstr(
                        data, str_begin + name_off, str_end);
                if (section_name != name) {
                    continue;
                }
                const std::uint64_t sec_off = read_u64(sh + 0x18U);
                const std::uint64_t sec_size = read_u64(sh + 0x20U);
                if (sec_off > size ||
                    sec_size > static_cast<std::uint64_t>(size) - sec_off) {
                    return false;
                }
                out.offset = sec_off;
                out.size = sec_size;
                return true;
            }
            return false;
        }

        /* Common ELF64 REL AArch64 envelope every entry point re-validates
         * before touching section data. */
        bool validate_elf_envelope(const std::uint8_t *data, std::size_t size,
                                   LkmImageError &error) noexcept {
            if (data == nullptr) {
                error = LkmImageError::ReadFailed;
                return false;
            }
            if (size < kModuleMinBytes) {
                error = LkmImageError::TooSmall;
                return false;
            }
            if (size > kModuleMaxBytes) {
                error = LkmImageError::TooLarge;
                return false;
            }
            if (data[0] != 0x7FU || data[1] != 'E' || data[2] != 'L' || data[3] != 'F') {
                error = LkmImageError::NotElf;
                return false;
            }
            if (data[4] != 2U || data[5] != 1U ||
                read_u16(data + 0x10U) != kElfTypeRelocatable) {
                error = LkmImageError::NotElf;
                return false;
            }
            if (read_u16(data + 0x12U) != kElfMachineAarch64) {
                error = LkmImageError::NotAarch64;
                return false;
            }
            return true;
        }

        /* One .modinfo entry. slot counts the bytes from the entry start
         * through and including its terminating NUL; an unterminated final
         * entry has slot one past the section end. */
        struct ModinfoEntry final {
            std::size_t start = 0U;
            std::string_view text{};
            std::size_t slot = 0U;
        };

        bool next_modinfo_entry(const std::uint8_t *mod, std::size_t mod_size,
                                std::size_t &pos, ModinfoEntry &out) noexcept {
            if (pos >= mod_size) {
                return false;
            }
            const std::size_t start = pos;
            const std::string_view text = bounded_cstr(mod, pos, mod_size);
            out.start = start;
            out.text = text;
            out.slot = text.size() + 1U;
            pos = start + out.slot;
            return true;
        }

        template <std::size_t N>
        void copy_bounded(char (&dst)[N], std::string_view src) noexcept {
            const std::size_t n = src.size() < N - 1U ? src.size() : N - 1U;
            for (std::size_t i = 0U; i < n; ++i) {
                dst[i] = src[i];
            }
            dst[n] = '\0';
        }

        std::string_view first_token(std::string_view text) noexcept {
            const std::size_t space = text.find(' ');
            return space == std::string_view::npos ? text : text.substr(0U, space);
        }

        /* The kernel compares the full string when the module has no CRCs, so
         * the classification only has to make the failure readable. */
        VermagicDiffReason classify_vermagic_diff(std::string_view module,
                                                  std::string_view required) noexcept {
            if (module.empty() || required.empty()) {
                return VermagicDiffReason::Unparsable;
            }
            return first_token(module) != first_token(required)
                           ? VermagicDiffReason::Release
                           : VermagicDiffReason::Options;
        }

        bool append_vermagic_token(char *out, std::size_t capacity, std::size_t &pos,
                                   bool &first, std::string_view token) noexcept {
            if (token.empty()) {
                return true;
            }
            /* pos < capacity is an invariant; leave room for the NUL. */
            const std::size_t need = (first ? 0U : 1U) + token.size() + 1U;
            if (need > capacity - pos) {
                return false;
            }
            if (!first) {
                out[pos] = ' ';
                ++pos;
            }
            for (std::size_t i = 0U; i < token.size(); ++i) {
                out[pos + i] = token[i];
            }
            pos += token.size();
            first = false;
            return true;
        }

        bool push_arg(UmhCommand &out, std::string_view value,
                      UmhCommandError &error) noexcept {
            /* Reserve one slot for the kernel's NULL terminator. */
            if (out.argc + 1U >= kUmhMaxArgc) {
                error = UmhCommandError::TooManyArgs;
                return false;
            }
            if (value.empty()) {
                error = UmhCommandError::ArgInvalid;
                return false;
            }
            if (value.size() + 1U > kUmhArgBytes) {
                error = UmhCommandError::ArgTooLong;
                return false;
            }
            for (const char c : value) {
                const unsigned char byte = static_cast<unsigned char>(c);
                if (byte < 0x20U || byte == 0x7FU) {
                    error = UmhCommandError::ArgInvalid;
                    return false;
                }
            }
            std::array<char, kUmhArgBytes> &slot = out.argv[out.argc];
            for (std::size_t i = 0U; i < value.size(); ++i) {
                slot[i] = value[i];
            }
            slot[value.size()] = '\0';
            ++out.argc;
            return true;
        }

        bool read_file(std::string_view path, std::vector<std::uint8_t> &bytes,
                       LkmImageError &error) noexcept {
            if (path.empty()) {
                error = LkmImageError::ReadFailed;
                return false;
            }
            const std::string path_str(path);
            const int fd = ::open(path_str.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd < 0) {
                error = LkmImageError::ReadFailed;
                return false;
            }
            struct stat st{};
            if (::fstat(fd, &st) != 0) {
                ::close(fd);
                error = LkmImageError::ReadFailed;
                return false;
            }
            if (!S_ISREG(st.st_mode)) {
                ::close(fd);
                error = LkmImageError::NotRegular;
                return false;
            }
            if (st.st_size < 0 ||
                static_cast<std::uint64_t>(st.st_size) < kModuleMinBytes) {
                ::close(fd);
                error = LkmImageError::TooSmall;
                return false;
            }
            if (static_cast<std::uint64_t>(st.st_size) > kModuleMaxBytes) {
                ::close(fd);
                error = LkmImageError::TooLarge;
                return false;
            }
            bytes.resize(static_cast<std::size_t>(st.st_size));
            std::size_t offset = 0U;
            while (offset < bytes.size()) {
                const ssize_t got =
                        ::read(fd, bytes.data() + offset, bytes.size() - offset);
                if (got < 0 && errno == EINTR) {
                    continue;
                }
                if (got <= 0) {
                    ::close(fd);
                    error = LkmImageError::ReadFailed;
                    return false;
                }
                offset += static_cast<std::size_t>(got);
            }
            ::close(fd);
            return true;
        }
    } // namespace

    std::string_view default_root_package(terminal::RootProgramKind kind) noexcept {
        switch (kind) {
        case terminal::RootProgramKind::KernelSU:
            return "me.weishu.kernelsu";
        case terminal::RootProgramKind::FolkPatch:
        case terminal::RootProgramKind::Custom:
        default:
            return {};
        }
    }

    std::string_view vermagic_diff_reason_name(VermagicDiffReason reason) noexcept {
        switch (reason) {
        case VermagicDiffReason::None: return "None";
        case VermagicDiffReason::Release: return "Release";
        case VermagicDiffReason::Options: return "Options";
        case VermagicDiffReason::Unparsable: return "Unparsable";
        }
        return "Unknown";
    }

    std::string_view vermagic_outcome_name(VermagicOutcome outcome) noexcept {
        switch (outcome) {
        case VermagicOutcome::Unchecked: return "unchecked";
        case VermagicOutcome::Original: return "original";
        case VermagicOutcome::Required: return "required";
        case VermagicOutcome::Rewritten: return "rewritten";
        }
        return "unknown";
    }

    std::string_view uts_release_token(std::string_view version_text) noexcept {
        if (version_text.starts_with(kLinuxVersionPrefix)) {
            version_text.remove_prefix(kLinuxVersionPrefix.size());
        }
        while (!version_text.empty() &&
               (version_text.front() == ' ' || version_text.front() == '\t' ||
                version_text.front() == '\n' || version_text.front() == '\r')) {
            version_text.remove_prefix(1U);
        }
        std::size_t end = 0U;
        while (end < version_text.size() && version_text[end] != ' ' &&
               version_text[end] != '\t' && version_text[end] != '\n' &&
               version_text[end] != '\r') {
            ++end;
        }
        return version_text.substr(0U, end);
    }

    bool required_vermagic(const DeviceKernelFacts &facts, char *out,
                           std::size_t capacity, std::size_t &length) noexcept {
        length = 0U;
        if (out == nullptr || capacity == 0U) {
            return false;
        }
        out[0] = '\0';
        const std::string_view release = uts_release_token(facts.release);
        if (release.empty()) {
            return false;
        }
        std::size_t pos = 0U;
        bool first = true;
        if (!append_vermagic_token(out, capacity, pos, first, release) ||
            !append_vermagic_token(out, capacity, pos, first, kSmpToken) ||
            (facts.preempt &&
             !append_vermagic_token(out, capacity, pos, first, kPreemptToken)) ||
            (!facts.module_force_unload &&
             !append_vermagic_token(out, capacity, pos, first, kModuleUnloadToken)) ||
            (facts.modversions &&
             !append_vermagic_token(out, capacity, pos, first, kModversionsToken)) ||
            !append_vermagic_token(out, capacity, pos, first, kArchToken)) {
            out[0] = '\0';
            return false;
        }
        out[pos] = '\0';
        length = pos;
        return true;
    }

    bool build_late_load_command(const terminal::RootProgram &root_program,
                                 std::string_view package_name,
                                 std::uint32_t late_load_args,
                                 std::uint32_t selinux_exec_context, UmhCommand &out,
                                 UmhCommandError &error) noexcept {
        out = UmhCommand{};
        error = UmhCommandError::None;
        const std::string_view program = root_program.argv_view();
        if (program.empty()) {
            error = UmhCommandError::MissingRootProgram;
            return false;
        }
        if ((late_load_args & ~kLateLoadArgsKnown) != 0U) {
            error = UmhCommandError::UnknownLateLoadArgs;
            return false;
        }
        if (selinux_exec_context > kSelinuxExecContextMax) {
            error = UmhCommandError::UnknownSelinuxContext;
            return false;
        }
        const bool want_package = (late_load_args & kLateLoadArgPackageName) != 0U;
        if (want_package && package_name.empty()) {
            error = UmhCommandError::MissingPackageName;
            return false;
        }
        if (!push_arg(out, program, error) || !push_arg(out, "late-load", error)) {
            return false;
        }
        if (want_package) {
            if (!push_arg(out, "--package-name", error) ||
                !push_arg(out, package_name, error)) {
                return false;
            }
        }
        if ((late_load_args & kLateLoadArgRoPartitions) != 0U &&
            !push_arg(out, "--ro-partitions", error)) {
            return false;
        }
        if ((late_load_args & kLateLoadArgSoftReboot) != 0U &&
            !push_arg(out, "--soft-reboot", error)) {
            return false;
        }
        out.selinux_exec_context = selinux_exec_context;
        return true;
    }

    bool precheck_module_bytes(const std::uint8_t *data, std::size_t size,
                               const DeviceKernelFacts &required, ModuleFacts &facts,
                               LkmImageError &error) noexcept {
        facts = ModuleFacts{};
        error = LkmImageError::None;
        if (!validate_elf_envelope(data, size, error)) {
            return false;
        }
        facts.elf_valid = true;

        SectionView modinfo{};
        if (!find_section(data, size, kModinfoSection, modinfo)) {
            error = LkmImageError::MissingModinfo;
            return false;
        }
        facts.has_modinfo = true;
        const std::uint8_t *mod = data + static_cast<std::size_t>(modinfo.offset);
        const std::size_t mod_size = static_cast<std::size_t>(modinfo.size);
        std::string_view vermagic{};
        std::size_t pos = 0U;
        ModinfoEntry entry{};
        while (next_modinfo_entry(mod, mod_size, pos, entry)) {
            if (entry.text.starts_with(kNamePrefix)) {
                facts.has_name = entry.text.size() > kNamePrefix.size();
            } else if (entry.text.starts_with(kVermagicPrefix)) {
                facts.has_vermagic = true;
                vermagic = entry.text.substr(kVermagicPrefix.size());
            }
        }
        if (!facts.has_name) {
            error = LkmImageError::MissingName;
            return false;
        }
        if (!facts.has_vermagic) {
            error = LkmImageError::VermagicMissing;
            return false;
        }
        char required_buf[kVermagicMaxBytes] = {};
        std::size_t required_len = 0U;
        if (!required_vermagic(required, required_buf, sizeof(required_buf),
                               required_len)) {
            error = LkmImageError::VermagicMissing;
            return false;
        }
        const std::string_view required_view(required_buf, required_len);
        copy_bounded(facts.module_vermagic, vermagic);
        copy_bounded(facts.required_vermagic, required_view);
        /* Kernel 5.15 check_modinfo() -> same_magic(): a module with no CRCs is
         * compared in full, so this is strcmp, not a release-prefix test. */
        if (vermagic != required_view) {
            facts.vermagic_diff = classify_vermagic_diff(vermagic, required_view);
            error = LkmImageError::VermagicMismatch;
            return false;
        }
        facts.vermagic_matches = true;

        SectionView versions{};
        if ((find_section(data, size, kVersionsSection, versions) && versions.size != 0U) ||
            (find_section(data, size, kVersionsAltSection, versions) && versions.size != 0U)) {
            error = LkmImageError::NonEmptyVersions;
            return false;
        }
        facts.versions_empty = true;
        facts.kcfi_present = contains_bytes(data, size, kCfiMarker);
        if (contains_bytes(data, size, kSignatureMarker)) {
            facts.signed_module = true;
            error = LkmImageError::SignedModule;
            return false;
        }
        return true;
    }

    bool precheck_module_file(std::string_view path, const DeviceKernelFacts &required,
                              ModuleFacts &facts, LkmImageError &error) noexcept {
        std::vector<std::uint8_t> bytes;
        LkmImageError read_error = LkmImageError::None;
        if (!read_file(path, bytes, read_error)) {
            facts = ModuleFacts{};
            error = read_error;
            return false;
        }
        return precheck_module_bytes(bytes.data(), bytes.size(), required, facts, error);
    }

    bool rewrite_vermagic(std::uint8_t *image, std::size_t size,
                          std::string_view required, LkmImageError &error) noexcept {
        error = LkmImageError::None;
        if (required.empty()) {
            error = LkmImageError::VermagicMissing;
            return false;
        }
        if (!validate_elf_envelope(image, size, error)) {
            return false;
        }
        SectionView modinfo{};
        if (!find_section(image, size, kModinfoSection, modinfo)) {
            error = LkmImageError::MissingModinfo;
            return false;
        }
        const std::size_t mod_off = static_cast<std::size_t>(modinfo.offset);
        const std::size_t mod_size = static_cast<std::size_t>(modinfo.size);
        const std::uint8_t *mod = image + mod_off;
        std::size_t pos = 0U;
        ModinfoEntry entry{};
        while (next_modinfo_entry(mod, mod_size, pos, entry)) {
            if (!entry.text.starts_with(kVermagicPrefix)) {
                continue;
            }
            /* Everything that can fail is decided before the first store, so a
             * rejected rewrite leaves the image byte-identical. */
            const std::size_t slot = entry.slot;
            if (entry.start + slot > mod_size || required.size() + 1U > slot) {
                error = LkmImageError::VermagicSlotTooSmall;
                return false;
            }
            std::uint8_t *slot_ptr = image + mod_off + entry.start;
            for (std::size_t i = 0U; i < kVermagicPrefix.size(); ++i) {
                slot_ptr[i] = static_cast<std::uint8_t>(kVermagicPrefix[i]);
            }
            for (std::size_t i = 0U; i < required.size(); ++i) {
                slot_ptr[kVermagicPrefix.size() + i] =
                        static_cast<std::uint8_t>(required[i]);
            }
            for (std::size_t i = kVermagicPrefix.size() + required.size(); i < slot;
                 ++i) {
                slot_ptr[i] = 0U;
            }
            return true;
        }
        error = LkmImageError::VermagicMissing;
        return false;
    }
} // namespace ghostlock::backend::cve_2026_43284::lkm
