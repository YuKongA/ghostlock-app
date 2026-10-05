/* CM-2 loader implementation: whitelist -> mode -> sha256 -> dlopen -> ABI
 * checks -> hook checks, all fail-closed. See loader.hpp for the ordered rules
 * and the plan sections 5/9 for the authority. */

#include "platform/countermeasure/loader.hpp"

#include "platform/countermeasure/sha256.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ghostlock::platform::countermeasure {
    namespace {

        constexpr std::size_t kCanonicalBufferBytes = 4096u;

        /* True when the path contains a ".." component. Rejected outright even
         * when it would resolve back inside the whitelist. */
        [[nodiscard]] bool has_parent_component(const std::string &path) {
            std::size_t i = 0u;
            while (i <= path.size()) {
                const std::size_t j = path.find('/', i);
                const std::string part =
                        path.substr(i, j == std::string::npos ? std::string::npos : j - i);
                if (part == "..") {
                    return true;
                }
                if (j == std::string::npos) {
                    break;
                }
                i = j + 1u;
            }
            return false;
        }

        [[nodiscard]] bool is_hex_digit(char c) noexcept {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                   (c >= 'A' && c <= 'F');
        }

        [[nodiscard]] bool is_valid_sha256(const char *hex) noexcept {
            if (hex == nullptr) {
                return false;
            }
            for (std::size_t i = 0u; i < kSha256HexLength; ++i) {
                if (!is_hex_digit(hex[i])) {
                    return false;
                }
            }
            return hex[kSha256HexLength] == '\0';
        }

        [[nodiscard]] char lower_hex(char c) noexcept {
            if (c >= 'A' && c <= 'F') {
                return static_cast<char>(c - 'A' + 'a');
            }
            return c;
        }

        [[nodiscard]] bool sha256_equal(const char *lhs, const char *rhs) noexcept {
            for (std::size_t i = 0u; i < kSha256HexLength; ++i) {
                if (lower_hex(lhs[i]) != lower_hex(rhs[i])) {
                    return false;
                }
            }
            return true;
        }

        /* Length of a NUL-terminated string capped at max. Returns max + 1 when
         * no NUL is found within max bytes, which the caller treats as invalid. */
        [[nodiscard]] std::size_t bounded_length(const char *text,
                                                 std::size_t max) noexcept {
            if (text == nullptr) {
                return 0u;
            }
            std::size_t len = 0u;
            while (len <= max && text[len] != '\0') {
                ++len;
            }
            return len;
        }

        /* Serializes the "size" gate before reading any hook table. */
        [[nodiscard]] bool size_is_compatible(std::uint32_t size) noexcept {
            return size >= kKnownModuleSize;
        }

        /* ---- production operations ---------------------------------------- */

        void *default_open_lib(const char *path) {
            return ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
        }

        void *default_sym(void *handle, const char *name) {
            return ::dlsym(handle, name);
        }

        void default_close_lib(void *handle) {
            if (handle != nullptr) {
                (void)::dlclose(handle);
            }
        }

        std::int32_t default_stat_mode(const char *path, std::uint32_t *mode_out) {
            struct stat info {};
            if (::stat(path, &info) != 0) {
                return -1;
            }
            *mode_out = static_cast<std::uint32_t>(info.st_mode);
            return 0;
        }

        bool default_exists(const char *path) {
            return ::access(path, F_OK) == 0;
        }

        std::int32_t default_sha256_file(const char *path, char *out_hex,
                                         std::size_t cap) {
            return sha256_file(path, out_hex, cap);
        }

        std::int32_t default_canonicalize(const char *path, char *out,
                                          std::size_t cap) {
            char *resolved = ::realpath(path, nullptr);
            if (resolved == nullptr) {
                return -1;
            }
            const std::size_t len = std::strlen(resolved);
            std::int32_t rc = 0;
            if (len + 1u > cap) {
                rc = -1;
            } else {
                std::memcpy(out, resolved, len + 1u);
            }
            ::free(resolved);
            return rc;
        }

        void default_log(std::int32_t level, const char *message) {
            std::fprintf(stderr, "[countermeasure] log(%d): %s\n", level,
                         message != nullptr ? message : "(null)");
        }

    } // namespace

    LoaderOps default_loader_ops() noexcept {
        LoaderOps ops{};
        ops.open_lib = &default_open_lib;
        ops.sym = &default_sym;
        ops.close_lib = &default_close_lib;
        ops.stat_mode = &default_stat_mode;
        ops.exists = &default_exists;
        ops.sha256_file = &default_sha256_file;
        ops.canonicalize = &default_canonicalize;
        ops.log = &default_log;
        return ops;
    }

    std::string default_countermeasure_dir(const char *ghostlock_home) {
        if (ghostlock_home == nullptr || ghostlock_home[0] == '\0') {
            return {};
        }
        return std::string(ghostlock_home) + "/countermeasures";
    }

    const char *load_status_name(LoadStatus status) noexcept {
        switch (status) {
        case LoadStatus::Ok: return "Ok";
        case LoadStatus::NotLoaded: return "NotLoaded";
        case LoadStatus::InvalidArgument: return "InvalidArgument";
        case LoadStatus::PathRejected: return "PathRejected";
        case LoadStatus::PermissionRejected: return "PermissionRejected";
        case LoadStatus::FileMissing: return "FileMissing";
        case LoadStatus::HashRejected: return "HashRejected";
        case LoadStatus::HashMismatch: return "HashMismatch";
        case LoadStatus::OpenFailed: return "OpenFailed";
        case LoadStatus::EntryMissing: return "EntryMissing";
        case LoadStatus::EntryRejected: return "EntryRejected";
        case LoadStatus::AbiMismatch: return "AbiMismatch";
        case LoadStatus::SizeIncompatible: return "SizeIncompatible";
        case LoadStatus::NameInvalid: return "NameInvalid";
        case LoadStatus::VersionInvalid: return "VersionInvalid";
        case LoadStatus::CapsRejected: return "CapsRejected";
        case LoadStatus::HooksTooMany: return "HooksTooMany";
        case LoadStatus::HooksMissing: return "HooksMissing";
        case LoadStatus::TriggerRejected: return "TriggerRejected";
        case LoadStatus::StageRejected: return "StageRejected";
        case LoadStatus::HookInvalid: return "HookInvalid";
        }
        return "Unknown";
    }

    /* ---- LoadedModule ----------------------------------------------------- */

    LoadedModule::LoadedModule(LoaderOps ops, void *handle,
                               const glk_cm_module *module,
                               const glk_cm_hook *hooks,
                               std::uint32_t hook_count) noexcept
        : ops_(ops), handle_(handle), module_(module), hooks_(hooks),
          hook_count_(hook_count) {}

    LoadedModule::~LoadedModule() { close(); }

    LoadedModule::LoadedModule(LoadedModule &&other) noexcept
        : ops_(other.ops_), handle_(other.handle_), module_(other.module_),
          hooks_(other.hooks_), hook_count_(other.hook_count_) {
        other.handle_ = nullptr;
        other.module_ = nullptr;
        other.hooks_ = nullptr;
        other.hook_count_ = 0u;
    }

    LoadedModule &LoadedModule::operator=(LoadedModule &&other) noexcept {
        if (this != &other) {
            close();
            ops_ = other.ops_;
            handle_ = other.handle_;
            module_ = other.module_;
            hooks_ = other.hooks_;
            hook_count_ = other.hook_count_;
            other.handle_ = nullptr;
            other.module_ = nullptr;
            other.hooks_ = nullptr;
            other.hook_count_ = 0u;
        }
        return *this;
    }

    void LoadedModule::close() noexcept {
        if (handle_ != nullptr) {
            if (ops_.close_lib != nullptr) {
                ops_.close_lib(handle_);
            }
            handle_ = nullptr;
        }
        module_ = nullptr;
        hooks_ = nullptr;
        hook_count_ = 0u;
    }

    const char *LoadedModule::name() const noexcept {
        return module_ != nullptr ? module_->name : nullptr;
    }

    const char *LoadedModule::version() const noexcept {
        return module_ != nullptr ? module_->version : nullptr;
    }

    /* ---- Loader ----------------------------------------------------------- */

    Loader::Loader(LoaderOps ops) noexcept : ops_(ops) {}

    Loader::Loader(std::string whitelist_dir, LoaderOps ops)
        : whitelist_dir_(std::move(whitelist_dir)), ops_(ops) {}

    LoadResult Loader::load(const char *path, const char *expected_sha256,
                            contract::Capability host_caps,
                            std::uint32_t host_triggers) const {
        LoadResult result;
        const auto fail = [&result, this](LoadStatus status,
                                          const char *reason) -> LoadResult {
            result.status = status;
            result.error = reason;
            if (ops_.log != nullptr) {
                ops_.log(-1, reason);
            }
            return std::move(result);
        };

        /* (argument gate) */
        if (path == nullptr || path[0] == '\0') {
            return fail(LoadStatus::InvalidArgument, "countermeasure path is empty");
        }
        if (!is_valid_sha256(expected_sha256)) {
            return fail(LoadStatus::InvalidArgument,
                        "expected sha256 is not 64 hex digits");
        }
        if (whitelist_dir_.empty()) {
            return fail(LoadStatus::InvalidArgument,
                        "no countermeasure whitelist directory configured");
        }

        /* (a) whitelist containment and symlink escape. */
        if (has_parent_component(std::string(path))) {
            return fail(LoadStatus::PathRejected, "path contains a parent (..) component");
        }
        std::string target = path;
        if (target[0] != '/') {
            target = whitelist_dir_ + "/" + target;
        }
        if (has_parent_component(target)) {
            return fail(LoadStatus::PathRejected,
                        "resolved path contains a parent (..) component");
        }
        if (ops_.exists != nullptr && !ops_.exists(target.c_str())) {
            return fail(LoadStatus::FileMissing, "countermeasure file does not exist");
        }
        if (ops_.canonicalize == nullptr) {
            return fail(LoadStatus::PathRejected, "no path resolver configured");
        }
        std::array<char, kCanonicalBufferBytes> dir_buf{};
        std::array<char, kCanonicalBufferBytes> path_buf{};
        if (ops_.canonicalize(whitelist_dir_.c_str(), dir_buf.data(), dir_buf.size()) != 0) {
            return fail(LoadStatus::PathRejected,
                        "countermeasure whitelist directory cannot be resolved");
        }
        if (ops_.canonicalize(target.c_str(), path_buf.data(), path_buf.size()) != 0) {
            return fail(LoadStatus::PathRejected, "countermeasure path cannot be resolved");
        }
        std::string canonical_dir(dir_buf.data());
        const std::string canonical_path(path_buf.data());
        if (!canonical_dir.empty() && canonical_dir.back() != '/') {
            canonical_dir.push_back('/');
        }
        if (canonical_path.size() <= canonical_dir.size() ||
            canonical_path.compare(0u, canonical_dir.size(), canonical_dir) != 0) {
            return fail(LoadStatus::PathRejected,
                        "countermeasure path escapes the whitelist directory");
        }

        /* (b) regular file, not group/other writable. */
        if (ops_.stat_mode == nullptr) {
            return fail(LoadStatus::PermissionRejected, "no file mode probe configured");
        }
        std::uint32_t mode = 0u;
        if (ops_.stat_mode(canonical_path.c_str(), &mode) != 0) {
            return fail(LoadStatus::FileMissing, "cannot stat countermeasure file");
        }
        if (!S_ISREG(static_cast<mode_t>(mode))) {
            return fail(LoadStatus::PermissionRejected,
                        "countermeasure is not a regular file");
        }
        if ((mode & 0022u) != 0u) {
            return fail(LoadStatus::PermissionRejected,
                        "countermeasure file is group/other writable");
        }

        /* (c) sha256 must equal the expected digest. */
        if (ops_.sha256_file == nullptr) {
            return fail(LoadStatus::HashRejected, "no sha256 implementation configured");
        }
        char actual[kSha256HexLength + 1u] = {};
        if (ops_.sha256_file(canonical_path.c_str(), actual, sizeof(actual)) != 0) {
            return fail(LoadStatus::HashRejected, "countermeasure sha256 could not be computed");
        }
        if (!sha256_equal(expected_sha256, actual)) {
            return fail(LoadStatus::HashMismatch, "countermeasure sha256 mismatch");
        }

        /* (d) open and resolve the single exported entry point. The handle is
         * owned from here on, so every later rejection closes it. */
        if (ops_.open_lib == nullptr || ops_.sym == nullptr || ops_.close_lib == nullptr) {
            return fail(LoadStatus::OpenFailed, "no library operations configured");
        }
        void *handle = ops_.open_lib(canonical_path.c_str());
        if (handle == nullptr) {
            return fail(LoadStatus::OpenFailed, "countermeasure dlopen failed");
        }
        LoadedModule owner(ops_, handle, nullptr, nullptr, 0u);

        void *entry_symbol = ops_.sym(handle, "glk_cm_entry");
        if (entry_symbol == nullptr) {
            return fail(LoadStatus::EntryMissing, "glk_cm_entry symbol not found");
        }
        const auto entry = reinterpret_cast<EntryFn>(entry_symbol);
        const glk_cm_module *module = entry(GLK_CM_ABI_VERSION);
        if (module == nullptr) {
            return fail(LoadStatus::EntryRejected, "glk_cm_entry returned NULL");
        }

        /* (e) ABI version, size, identity strings. */
        if (module->abi_version != GLK_CM_ABI_VERSION) {
            return fail(LoadStatus::AbiMismatch, "module abi_version mismatch");
        }
        if (!size_is_compatible(module->size)) {
            return fail(LoadStatus::SizeIncompatible,
                        "module size is smaller than ABI v1");
        }
        const std::size_t name_len = bounded_length(module->name, kMaxModuleNameLength);
        if (name_len == 0u || name_len > kMaxModuleNameLength) {
            return fail(LoadStatus::NameInvalid,
                        "module name missing, empty or too long");
        }
        const std::size_t version_len =
                bounded_length(module->version, kMaxModuleVersionLength);
        if (version_len == 0u || version_len > kMaxModuleVersionLength) {
            return fail(LoadStatus::VersionInvalid,
                        "module version missing, empty or too long");
        }

        /* (f) capabilities and every hook. Reserved bits reject the whole
         * module; nothing is silently cleared or skipped. */
        const std::uint32_t required_raw =
                static_cast<std::uint32_t>(module->required_caps);
        const std::uint32_t all_caps =
                static_cast<std::uint32_t>(contract::kAllCapabilities);
        if ((required_raw & ~all_caps) != 0u) {
            return fail(LoadStatus::CapsRejected,
                        "module requires an unknown capability bit");
        }
        if ((required_raw & ~static_cast<std::uint32_t>(host_caps)) != 0u) {
            return fail(LoadStatus::CapsRejected,
                        "module requires an unimplemented capability");
        }
        if (module->hook_count > kMaxHooks) {
            return fail(LoadStatus::HooksTooMany, "module hook_count exceeds the limit");
        }
        if (module->hook_count > 0u && module->hooks == nullptr) {
            return fail(LoadStatus::HooksMissing, "module hook table is NULL");
        }
        for (std::uint32_t i = 0u; i < module->hook_count; ++i) {
            const glk_cm_hook &hook = module->hooks[i];
            if (hook.fn == nullptr) {
                return fail(LoadStatus::HookInvalid, "hook callback is NULL");
            }
            const std::size_t hook_name_len =
                    bounded_length(hook.name, kMaxHookNameLength);
            if (hook_name_len == 0u || hook_name_len > kMaxHookNameLength) {
                return fail(LoadStatus::HookInvalid,
                            "hook name missing, empty or too long");
            }
            const std::uint32_t trigger =
                    static_cast<std::uint32_t>(hook.trigger);
            const std::uint32_t trigger_bit =
                    trigger < 32u ? (std::uint32_t{1} << trigger) : 0u;
            if (trigger_bit == 0u || (host_triggers & trigger_bit) == 0u) {
                return fail(LoadStatus::TriggerRejected,
                            "hook trigger is not implemented by the host");
            }
            if (hook.trigger == GLK_CM_TRIGGER_ON_STAGE) {
                const std::uint32_t stage = static_cast<std::uint32_t>(hook.stage);
                const std::uint32_t stage_bit_value =
                        stage < 32u ? (std::uint32_t{1} << stage) : 0u;
                if (stage_bit_value == 0u ||
                    (kHostImplementedStages & stage_bit_value) == 0u) {
                    return fail(LoadStatus::StageRejected,
                                "hook stage is not implemented by the host");
                }
            }
        }

        /* Success: transfer the handle and publish the borrowed metadata. */
        owner.module_ = module;
        owner.hooks_ = module->hooks;
        owner.hook_count_ = module->hook_count;

        result.status = LoadStatus::Ok;
        result.module = std::move(owner);
        result.module_name = module->name;
        result.module_version = module->version;
        result.required_caps = static_cast<contract::Capability>(required_raw);
        result.hooks = module->hooks;
        result.hook_count = module->hook_count;
        return result;
    }

} // namespace ghostlock::platform::countermeasure
