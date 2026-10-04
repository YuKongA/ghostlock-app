#ifndef GHOSTLOCK_PLATFORM_DEVICE_FACTS_HPP
#define GHOSTLOCK_PLATFORM_DEVICE_FACTS_HPP

/* Device/firmware facts the CVE-2026-43284 endgame needs, expressed as an
 * injectable probe contract (B5-7). The backend never probes the device itself
 * (ADR-0004 R1): platform collects the facts and never includes a backend
 * header. The real binding is real_device_probe(), compiled only for
 * __linux__; host tests bind a fake through DeviceProbeOps.
 *
 * Facts covered (design 5.3 / 5.5 and the device-fact package):
 *   - uname -r release;
 *   - /proc/version (and the best-effort f4c50a4 "already fixed" marker);
 *   - /sys/fs/selinux/enforce;
 *   - crash_dump64 existence, ls -lZ label and fs-verity state;
 *   - /vendor/lib64 carrier candidates and their labels;
 *   - kallsyms presence of selinux_state and the Defex symbols.
 *
 * Availability is derived from the bound function pointers, never a separate
 * flag. collect_device_facts() is fail-closed: the first missing mandatory fact
 * is named and the output stays all-unknown. Kernel symbol absence is never
 * mandatory, though: a restricted /proc/kallsyms (unprivileged uid) hides every
 * symbol and SELinux may be disabled via LKM, so a missing selinux_state is
 * recorded in KernelSymbolFacts and the diagnostic instead of blocking. */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ghostlock::platform {

    inline constexpr std::size_t kDeviceReleaseMax = 256U;
    inline constexpr std::size_t kDeviceProcVersionMax = 512U;
    inline constexpr std::size_t kDevicePathMax = 128U;
    inline constexpr std::size_t kDeviceLabelMax = 64U;
    inline constexpr std::size_t kDeviceSymbolMax = 32U;
    inline constexpr std::size_t kVendorCandidateMax = 8U;

    /* /apex/com.android.runtime/bin/crash_dump64 (design 2.2 patch #1). */
    inline constexpr char kCrashDump64Path[] =
            "/apex/com.android.runtime/bin/crash_dump64";

    /* Bounded, NUL-terminated text buffer. Overflow truncates rather than
     * corrupting memory; the empty string means "unknown/absent". */
    template <std::size_t N>
    struct FactText final {
        std::array<char, N> value{};

        [[nodiscard]] bool empty() const noexcept { return value[0] == '\0'; }
        [[nodiscard]] std::string_view view() const noexcept {
            return std::string_view(value.data());
        }
        void set(std::string_view text) noexcept {
            const std::size_t n = text.size() < N - 1U ? text.size() : N - 1U;
            for (std::size_t i = 0U; i < n; ++i) value[i] = text[i];
            value[n] = '\0';
        }
    };

    /* One filesystem object plus the facts ls -lZ / fs-verity expose. */
    struct FileFact final {
        FactText<kDevicePathMax> path{};
        FactText<kDeviceLabelMax> label{}; /* security.selinux, empty when unknown */
        bool exists = false;
        bool label_known = false;
        bool verity = false;
    };

    /* One /vendor/lib64 carrier candidate. vendor_file_label mirrors the
     * "u:object_r:vendor_file:s0" context the endgame requires. */
    struct VendorCandidate final {
        FactText<kDevicePathMax> path{};
        FactText<kDeviceLabelMax> label{};
        bool exists = false;
        bool vendor_file_label = false;
    };

    /* kallsyms presence; absence is a fact, not an error. Booleans default to
     * false so an unbound/empty probe reads as "no symbol". */
    struct KernelSymbolFacts final {
        bool selinux_state = false;
        /* /proc/kallsyms gave no selinux_state symbol. Unprivileged callers see
         * an all-zero table (kptr_restrict), and a kernel with SELinux disabled
         * via LKM legitimately lacks the symbol; the two are indistinguishable
         * here, and neither is fatal. Recorded so the diagnostic can show it. */
        bool kallsyms_restricted = false;
        bool defex_task_defex_enforce = false;
        bool defex_task_defex_user_exec = false;
        bool defex_get_dc_target_dpath = false;

        [[nodiscard]] bool any_defex() const noexcept {
            return defex_task_defex_enforce || defex_task_defex_user_exec ||
                   defex_get_dc_target_dpath;
        }
    };

    struct DeviceFacts final {
        bool release_present = false;
        FactText<kDeviceReleaseMax> release{};
        bool proc_version_present = false;
        FactText<kDeviceProcVersionMax> proc_version{};
        /* true when /proc/version carries the fix marker (best effort; a vendor
         * backport without the marker cannot be ruled out). */
        bool has_f4c50a4 = false;
        bool selinux_enforce_readable = false;
        int selinux_enforce = 0; /* 0 permissive, 1 enforcing */

        FileFact crash_dump{};
        std::array<VendorCandidate, kVendorCandidateMax> vendor_candidates{};
        std::size_t vendor_candidate_count = 0U;

        KernelSymbolFacts symbols{};
    };

    /* Injectable probe surface. Every entry point returns a non-negative value
     * on success (byte count / 0|1 / count) or a negative -errno; the collector
     * treats any negative as "unavailable fact". */
    struct DeviceProbeOps final {
        void *ctx = nullptr;

        /* uname -r; returns the byte count written (excluding NUL), or -errno. */
        long (*read_release)(void *ctx, char *out, std::size_t capacity) noexcept = nullptr;

        /* /proc/version content; returns the byte count written, or -errno. */
        long (*read_proc_version)(void *ctx, char *out,
                                  std::size_t capacity) noexcept = nullptr;

        /* /sys/fs/selinux/enforce value (0/1), or -errno. */
        int (*read_selinux_enforce)(void *ctx) noexcept = nullptr;

        /* stat + ls -lZ label + fs-verity for one absolute path. */
        bool (*file_fact)(void *ctx, const char *path, FileFact &out) noexcept = nullptr;

        /* Enumerates /vendor/lib64 carriers; returns the number written. */
        std::size_t (*list_vendor_candidates)(void *ctx, VendorCandidate *out,
                                              std::size_t capacity) noexcept = nullptr;

        /* /proc/kallsyms symbol existence. */
        bool (*symbol_present)(void *ctx, const char *symbol) noexcept = nullptr;

        [[nodiscard]] bool available() const noexcept {
            return read_release != nullptr && read_proc_version != nullptr &&
                   read_selinux_enforce != nullptr && file_fact != nullptr &&
                   list_vendor_candidates != nullptr && symbol_present != nullptr;
        }
    };

    enum class DeviceFactError : std::uint8_t {
        None = 0,
        Unavailable,             /* probe surface not fully bound */
        ReleaseMissing,          /* uname -r unreadable/empty */
        ProcVersionMissing,      /* /proc/version unreadable/empty */
        SelinuxMissing,          /* enforce unreadable */
        CrashDumpMissing,        /* crash_dump64 not present */
        CrashDumpLabelUnknown,   /* no ls -lZ label */
        VendorCandidatesMissing, /* no /vendor/lib64 candidate found */
        /* Retained for ABI/name stability, but no longer produced: a missing
         * selinux_state symbol is recorded (KernelSymbolFacts::
         * kallsyms_restricted), never fatal. */
        SelinuxStateMissing,
    };

    /* True when the proc version string carries the f4c50a4 fix marker. Kept a
     * pure function so the collector and the tests agree and the heuristic can
     * be replaced without changing the fact shape. */
    [[nodiscard]] bool proc_version_indicates_fixed(std::string_view proc_version) noexcept;

    /* Fail-closed collection. On an error the output is reset to all-unknown
     * and error names the first missing mandatory fact. Kernel symbol absence
     * is always recorded, never fatal: Defex is vendor-specific, and
     * selinux_state can be hidden by a restricted /proc/kallsyms or absent when
     * SELinux is disabled via LKM. */
    [[nodiscard]] DeviceFactError collect_device_facts(const DeviceProbeOps &ops,
                                                       DeviceFacts &out) noexcept;

    /* Real binding for Android/Linux. On any other host every entry point is
     * null, so available() is false instead of a link error; host tests bind
     * their own fake. */
    [[nodiscard]] DeviceProbeOps real_device_probe() noexcept;

} // namespace ghostlock::platform

#endif
