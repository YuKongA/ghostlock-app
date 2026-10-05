#ifndef GHOSTLOCK_PLUGIN_LOADER_HPP
#define GHOSTLOCK_PLUGIN_LOADER_HPP

/* CM-2: fail-closed loader for out-of-tree countermeasure shared objects.
 *
 * Authority: docs/analysis/countermeasure-plugin-plan.md sections 5 and 9. The
 * loader is the only component that touches a countermeasure .so before any
 * attack stage runs. It validates, in this exact order, and never proceeds past
 * a failed step:
 *
 *   (a) the path lives inside the caller-supplied whitelist directory, contains
 *       no ".." component and does not escape the directory through a symlink;
 *   (b) the file is a regular file that is not group/other writable;
 *   (c) its sha256 equals the caller's expected digest;
 *   (d) glk_entry exists and returns a non-NULL module;
 *   (e) the module's abi_version, size, name and version are compatible with
 *       the v1 host;
 *   (f) every declared hook uses an implemented trigger/stage/capability and
 *       carries a non-NULL fn and a bounded, non-empty name;
 *   (g) on any failure the library handle is closed, no hook is called and a
 *       status code plus a human-readable reason is returned.
 *
 * No hook is ever invoked here; dispatch is CM-3. The declaration-only contract
 * mapping (contract/countermeasure.hpp) still owns the ABI values; the
 * adjudicated implemented sets it exposes are the load-time acceptance sets.
 *
 * The loader deliberately depends only on contract/ and, through the shared
 * support/sha256.hpp, the C/C++ runtime: it must not reach backend/, pipeline/
 * or terminal/ (ADR-0004 R1; S4 R8 moved the digest there). */

#include <cstddef>
#include <cstdint>
#include <string>

#include "contract/countermeasure.hpp"
#include "contract/abi/glk_contract_abi.h"

namespace ghostlock::plugin {

    /* The single exported entry point's function-pointer type. */
    using EntryFn = const glk_module *(*)(std::uint32_t host_abi_version);

    /* Host-side bounds enforced before any module table is read. A module that
     * exceeds one is rejected as a whole. */
    inline constexpr std::uint32_t kMaxHooks = 16u;
    inline constexpr std::size_t kMaxModuleNameLength = 64u;
    inline constexpr std::size_t kMaxModuleVersionLength = 64u;
    inline constexpr std::size_t kMaxHookNameLength = 64u;

    /* Smallest glk_module the v1 host can read. A module may append fields
     * (a larger size is forward compatible) but must never be smaller than the
     * struct whose fields the host actually reads. */
    inline constexpr std::uint32_t kKnownModuleSize =
            static_cast<std::uint32_t>(sizeof(glk_module));

    [[nodiscard]] constexpr std::uint32_t stage_bit(
            contract::CountermeasureStage stage) noexcept {
        return std::uint32_t{1} << static_cast<std::uint32_t>(stage);
    }

    /* Adjudicated implemented stages. PRE_SPAWN, POST_SPAWN and PRE_TERMINAL
     * are the classic chain points; delta-4 adds POST_TERMINAL, the point inside
     * the 43284 LKM residency window where a kernel-state countermeasure runs
     * (contract-design.md 3.13). PRE_ROUTE stays reserved and must reject. */
    inline constexpr std::uint32_t kHostImplementedStages =
            stage_bit(contract::CountermeasureStage::PreSpawn) |
            stage_bit(contract::CountermeasureStage::PostSpawn) |
            stage_bit(contract::CountermeasureStage::PreTerminal) |
            stage_bit(contract::CountermeasureStage::PostTerminal);
    static_assert((kHostImplementedStages &
                   stage_bit(contract::CountermeasureStage::PreRoute)) == 0u);

    /* Injectable operations, so the host tests can exercise every validation
     * branch without a real file or a real module. Production defaults are
     * dlopen/dlsym/dlclose, stat(2), realpath(3) and the platform sha256. */
    struct LoaderOps final {
        void *(*open_lib)(const char *path) = nullptr;
        void *(*sym)(void *handle, const char *name) = nullptr;
        void (*close_lib)(void *handle) = nullptr;
        std::int32_t (*stat_mode)(const char *path, std::uint32_t *mode_out) = nullptr;
        bool (*exists)(const char *path) = nullptr;
        std::int32_t (*sha256_file)(const char *path, char *out_hex, std::size_t cap) = nullptr;
        std::int32_t (*canonicalize)(const char *path, char *out, std::size_t cap) = nullptr;
        void (*log)(std::int32_t level, const char *message) = nullptr;
    };

    /* Production ops: dlopen(RTLD_NOW|RTLD_LOCAL), dlsym, dlclose, stat,
     * realpath and support::sha256_file. */
    [[nodiscard]] LoaderOps default_loader_ops() noexcept;

    /* Relative name of the countermeasure root inside GHOSTLOCK_HOME. It is the
     * ONE authority for the literal: the loader builds <home>/<name> with it and
     * the P1 probe reports it verbatim as the countermeasures_root header value
     * (a relative name, so the App can compare it without sharing the probe's
     * environment; contract-design 3.14.7.4). */
    inline constexpr std::string_view kCountermeasuresDirName = "countermeasures";

    /* The plan's default root is "<dir>/countermeasures/". CM-5 will supply the
     * real app-private home; CM-2 lets the caller name the whitelist root and
     * exposes this helper so the convention lives in one place. Empty when
     * ghostlock_home is null/empty (which makes the loader reject everything). */
    [[nodiscard]] std::string default_countermeasure_dir(const char *ghostlock_home);

    /* Outcome of a load attempt. Negative codes mirror the native fail-closed
     * Status convention (0 ok, negative failure). */
    enum class LoadStatus : std::int32_t {
        Ok = 0,
        NotLoaded = -1,
        InvalidArgument = -2,
        PathRejected = -3,
        PermissionRejected = -4,
        FileMissing = -5,
        HashRejected = -6,
        HashMismatch = -7,
        OpenFailed = -8,
        EntryMissing = -9,
        EntryRejected = -10,
        AbiMismatch = -11,
        SizeIncompatible = -12,
        NameInvalid = -13,
        VersionInvalid = -14,
        CapsRejected = -15,
        HooksTooMany = -16,
        HooksMissing = -17,
        TriggerRejected = -18,
        StageRejected = -19,
        HookInvalid = -20,
    };

    [[nodiscard]] const char *load_status_name(LoadStatus status) noexcept;

    /* RAII owner of a loaded countermeasure library. Move-only; the destructor
     * (or an explicit close) calls the loader's close operation exactly once.
     * The module and hook pointers borrow the module's static storage and are
     * valid only while this object owns the handle. No hook is invoked. */
    class LoadedModule final {
    public:
        LoadedModule() noexcept = default;
        ~LoadedModule();
        LoadedModule(const LoadedModule &) = delete;
        LoadedModule &operator=(const LoadedModule &) = delete;
        LoadedModule(LoadedModule &&other) noexcept;
        LoadedModule &operator=(LoadedModule &&other) noexcept;

        void close() noexcept;
        [[nodiscard]] bool valid() const noexcept { return handle_ != nullptr; }
        [[nodiscard]] const glk_module *module() const noexcept { return module_; }
        [[nodiscard]] const glk_hook *hooks() const noexcept { return hooks_; }
        [[nodiscard]] std::uint32_t hook_count() const noexcept { return hook_count_; }
        [[nodiscard]] const char *name() const noexcept;
        [[nodiscard]] const char *version() const noexcept;

    private:
        friend class Loader;
        LoadedModule(LoaderOps ops, void *handle, const glk_module *module,
                     const glk_hook *hooks, std::uint32_t hook_count) noexcept;

        LoaderOps ops_{};
        void *handle_ = nullptr;
        const glk_module *module_ = nullptr;
        const glk_hook *hooks_ = nullptr;
        std::uint32_t hook_count_ = 0u;
    };

    /* Result of Loader::load. On success the module member owns the handle and
     * hooks borrows the module's static table; on failure the module is invalid,
     * the handle has been released and error names the rejected invariant. */
    struct LoadResult final {
        LoadStatus status = LoadStatus::NotLoaded;
        LoadedModule module;
        std::string module_name;
        std::string module_version;
        contract::Capability required_caps = contract::Capability::None;
        const glk_hook *hooks = nullptr;
        std::uint32_t hook_count = 0u;
        std::string error;
    };

    class Loader final {
    public:
        explicit Loader(LoaderOps ops = default_loader_ops()) noexcept;
        Loader(std::string whitelist_dir, LoaderOps ops = default_loader_ops());
        Loader(const Loader &) = delete;
        Loader &operator=(const Loader &) = delete;

        [[nodiscard]] const std::string &whitelist_dir() const noexcept {
            return whitelist_dir_;
        }

        /* Validate and open path, in the fail-closed order documented above.
         * host_caps/host_triggers are the adjudicated implemented sets the host
         * accepts; production passes contract::kHostImplementedCaps and
         * contract::kHostImplementedTriggers, and the test can pass a reduced
         * set to prove reserved entries reject. */
        [[nodiscard]] LoadResult load(const char *path, const char *expected_sha256,
                                      contract::Capability host_caps,
                                      std::uint32_t host_triggers) const;

    private:
        std::string whitelist_dir_;
        LoaderOps ops_{};
    };

} // namespace ghostlock::plugin

#endif
