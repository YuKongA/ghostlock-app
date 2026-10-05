/* Host test for the CM-2 countermeasure loader (plugin).
 *
 * The positive path is a real dlopen/dlsym/dlclose of the C test plugins built
 * next to this binary; the negative paths mix real plugins (bad ABI, missing
 * symbol), real filesystem checks (parent component, symlink escape) and
 * injected LoaderOps that synthesize a module for the reserved trigger/stage/
 * capability, hook-bound and handle-leak cases.
 *
 * No hook is ever dispatched here (CM-3 owns dispatch); the valid plugin counts
 * invocations and this test proves the count stays zero. */

#include "plugin/loader.hpp"
#include "plugin/sha256.hpp"

#include "contract/countermeasure.hpp"
#include "contract/abi/glk_contract_abi.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include <dlfcn.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef GLK_TEST_PLUGIN_DIR
#define GLK_TEST_PLUGIN_DIR "."
#endif

using ghostlock::plugin::default_loader_ops;
using ghostlock::plugin::kMaxHooks;
using ghostlock::plugin::Loader;
using ghostlock::plugin::LoaderOps;
using ghostlock::plugin::LoadResult;
using ghostlock::plugin::LoadStatus;
using ghostlock::plugin::load_status_name;
using ghostlock::plugin::sha256;
using ghostlock::plugin::sha256_file;

using ghostlock::contract::Capability;
using ghostlock::contract::kHostImplementedCaps;
using ghostlock::contract::kHostImplementedTriggers;

namespace {

    constexpr char kGoodSha[] =
            "aaaaaaaaaaaaaaaa"
            "aaaaaaaaaaaaaaaa"
            "aaaaaaaaaaaaaaaa"
            "aaaaaaaaaaaaaaaa";
    constexpr char kBadSha[] =
            "0000000000000000"
            "0000000000000000"
            "0000000000000000"
            "0000000000000000";

    [[noreturn]] void fail(const char *what, const char *detail) {
        std::fprintf(stderr, "countermeasure_loader_test: FAIL %s: %s\n", what, detail);
        std::abort();
    }

    void check(bool condition, const char *what) {
        if (!condition) {
            fail(what, "condition is false");
        }
    }

    void check_status(const LoadResult &result, LoadStatus expected, const char *what) {
        if (result.status != expected) {
            char detail[320] = {};
            std::snprintf(detail, sizeof(detail), "got %s (%d): %s",
                          load_status_name(result.status),
                          static_cast<int>(result.status), result.error.c_str());
            fail(what, detail);
        }
    }

    std::string plugin_path(const char *name) {
        return std::string(GLK_TEST_PLUGIN_DIR) + "/" + name;
    }

    std::string hex_of(const std::uint8_t digest[32]) {
        static constexpr char kHex[] = "0123456789abcdef";
        std::string out;
        out.reserve(64u);
        for (std::size_t i = 0u; i < 32u; ++i) {
            out.push_back(kHex[(digest[i] >> 4u) & 0x0fu]);
            out.push_back(kHex[digest[i] & 0x0fu]);
        }
        return out;
    }

    /* ---- injected operations that synthesize a module ------------------- */

    namespace fake {

        const glk_module *g_module = nullptr;
        std::uint32_t g_mode = 0100644u;
        bool g_exists = true;
        bool g_open_ok = true;
        bool g_symbol_ok = true;
        bool g_sha_ok = true;
        int g_open = 0;
        int g_close = 0;
        char g_digest[65] = {};

        glk_hook g_hook{};
        glk_module g_module_storage{};
        int g_handle_token = 0;

        int32_t hook_fn(void *, glk_stage, const glk_contract_ops *) { return 0; }

        const glk_module *entry(uint32_t) { return g_module; }

        void *open_lib(const char *) {
            ++g_open;
            return g_open_ok ? static_cast<void *>(&g_handle_token) : nullptr;
        }

        void close_lib(void *) { ++g_close; }

        void *sym(void *, const char *name) {
            if (!g_symbol_ok || std::strcmp(name, "glk_entry") != 0) {
                return nullptr;
            }
            return reinterpret_cast<void *>(&entry);
        }

        std::int32_t stat_mode(const char *, std::uint32_t *mode_out) {
            *mode_out = g_mode;
            return 0;
        }

        bool exists(const char *) { return g_exists; }

        std::int32_t sha256_fn(const char *, char *out_hex, std::size_t cap) {
            if (!g_sha_ok || cap < 65u) {
                return -1;
            }
            std::memcpy(out_hex, g_digest, 65u);
            return 0;
        }

        std::int32_t canonicalize(const char *path, char *out, std::size_t cap) {
            const std::size_t len = std::strlen(path);
            if (len + 1u > cap) {
                return -1;
            }
            std::memcpy(out, path, len + 1u);
            return 0;
        }

        void log_fn(std::int32_t, const char *) {}

        LoaderOps ops() {
            LoaderOps operations{};
            operations.open_lib = &open_lib;
            operations.sym = &sym;
            operations.close_lib = &close_lib;
            operations.stat_mode = &stat_mode;
            operations.exists = &exists;
            operations.sha256_file = &sha256_fn;
            operations.canonicalize = &canonicalize;
            operations.log = &log_fn;
            return operations;
        }

        void prepare() {
            g_mode = 0100644u;
            g_exists = true;
            g_open_ok = true;
            g_symbol_ok = true;
            g_sha_ok = true;
            g_open = 0;
            g_close = 0;
            std::memcpy(g_digest, kGoodSha, sizeof(g_digest));

            g_hook = glk_hook{};
            g_hook.trigger = GLK_TRIGGER_ON_STAGE;
            g_hook.stage = GLK_STAGE_PRE_SPAWN;
            g_hook.priority = 0u;
            g_hook.period_ms = 0u;
            g_hook.fn = &hook_fn;
            g_hook.user = nullptr;
            g_hook.name = "fake.hook";

            g_module_storage = glk_module{};
            g_module_storage.abi_version = GLK_ABI_VERSION;
            g_module_storage.size = static_cast<std::uint32_t>(sizeof(glk_module));
            g_module_storage.name = "fake.module";
            g_module_storage.version = "1.0";
            g_module_storage.required_caps =
                    static_cast<std::uint32_t>(GLK_CAP_KERNEL_READ);
            g_module_storage.hook_count = 1u;
            g_module_storage.hooks = &g_hook;

            g_module = &g_module_storage;
        }

    } // namespace fake

    LoadResult fake_load(const char *sha256_hex,
                         Capability caps = kHostImplementedCaps,
                         std::uint32_t triggers = kHostImplementedTriggers) {
        const Loader loader(std::string("/fake/cm"), fake::ops());
        return loader.load("/fake/cm/mod.so", sha256_hex, caps, triggers);
    }

    /* ---- real dlopen-backed operations with open/close counters --------- */

    namespace counted {

        int g_open = 0;
        int g_close = 0;

        void *open_lib(const char *path) {
            ++g_open;
            return ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
        }

        void close_lib(void *handle) {
            ++g_close;
            if (handle != nullptr) {
                (void)::dlclose(handle);
            }
        }

        LoaderOps ops() {
            LoaderOps operations = default_loader_ops();
            operations.open_lib = &open_lib;
            operations.close_lib = &close_lib;
            return operations;
        }

        void reset() {
            g_open = 0;
            g_close = 0;
        }

    } // namespace counted

    /* ---- tests ---------------------------------------------------------- */

    void test_sha256_vectors() {
        std::uint8_t digest[32] = {};
        sha256(nullptr, 0u, digest);
        check(hex_of(digest) ==
                      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              "sha256 empty");
        const char *abc = "abc";
        sha256(reinterpret_cast<const std::uint8_t *>(abc), 3u, digest);
        check(hex_of(digest) ==
                      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "sha256 abc");
    }

    void test_valid_plugin_real() {
        const std::string path = plugin_path("cm_test_plugin.so");
        char digest[65] = {};
        check(sha256_file(path.c_str(), digest, sizeof(digest)) == 0, "sha256_file valid");
        check(std::strlen(digest) == 64u, "digest length");

        const Loader loader(std::string(GLK_TEST_PLUGIN_DIR), default_loader_ops());
        LoadResult result =
                loader.load(path.c_str(), digest, kHostImplementedCaps,
                            kHostImplementedTriggers);
        check_status(result, LoadStatus::Ok, "valid plugin");
        check(result.module.valid(), "valid handle");
        check(result.module.module() != nullptr, "module pointer");
        check(result.module.hook_count() == 1u, "owner hook count");
        check(std::strcmp(result.module.name(), "ghostlock.test") == 0, "module name");
        check(std::strcmp(result.module.version(), "1.0.0") == 0, "module version");
        check(result.required_caps == (Capability::KernelRead | Capability::Alias),
              "required caps");
        check(result.hook_count == 1u, "borrowed hook count");
        check(result.hooks != nullptr, "borrowed hooks");
        check(result.hooks[0].trigger == GLK_TRIGGER_ON_STAGE, "hook trigger");
        check(result.hooks[0].stage == GLK_STAGE_PRE_SPAWN, "hook stage");
        check(result.hooks[0].fn != nullptr, "hook fn");
        check(std::strcmp(result.hooks[0].name, "test.pre_spawn") == 0, "hook name");

        /* The loader must not have invoked the hook. */
        void *handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        check(handle != nullptr, "independent dlopen");
        using InvocationsFn = int (*)();
        auto invocations =
                reinterpret_cast<InvocationsFn>(::dlsym(handle, "glk_test_invocations"));
        check(invocations != nullptr, "invocations symbol");
        check(invocations() == 0, "loader invoked no hook");
        (void)::dlclose(handle);
    }

    void test_hash_mismatch_real() {
        const std::string path = plugin_path("cm_test_plugin.so");
        const Loader loader(std::string(GLK_TEST_PLUGIN_DIR), default_loader_ops());
        const LoadResult result =
                loader.load(path.c_str(), kBadSha, kHostImplementedCaps,
                            kHostImplementedTriggers);
        check_status(result, LoadStatus::HashMismatch, "hash mismatch");
        check(!result.module.valid(), "hash mismatch no handle");
    }

    void test_negative_plugins_real() {
        {
            const std::string path = plugin_path("cm_test_plugin_badabi.so");
            char digest[65] = {};
            check(sha256_file(path.c_str(), digest, sizeof(digest)) == 0, "sha badabi");
            const Loader loader(std::string(GLK_TEST_PLUGIN_DIR), default_loader_ops());
            const LoadResult result =
                    loader.load(path.c_str(), digest, kHostImplementedCaps,
                                kHostImplementedTriggers);
            check_status(result, LoadStatus::AbiMismatch, "bad abi");
            check(!result.module.valid(), "bad abi no handle");
        }
        {
            const std::string path = plugin_path("cm_test_plugin_missing_entry.so");
            char digest[65] = {};
            check(sha256_file(path.c_str(), digest, sizeof(digest)) == 0, "sha missing");
            const Loader loader(std::string(GLK_TEST_PLUGIN_DIR), default_loader_ops());
            const LoadResult result =
                    loader.load(path.c_str(), digest, kHostImplementedCaps,
                                kHostImplementedTriggers);
            check_status(result, LoadStatus::EntryMissing, "missing entry");
            check(!result.module.valid(), "missing entry no handle");
        }
    }

    void test_path_rejections() {
        const Loader loader(std::string(GLK_TEST_PLUGIN_DIR), default_loader_ops());
        const std::string parent =
                std::string(GLK_TEST_PLUGIN_DIR) + "/../cm/cm_test_plugin.so";
        check_status(loader.load(parent.c_str(), kGoodSha, kHostImplementedCaps,
                                 kHostImplementedTriggers),
                     LoadStatus::PathRejected, "parent component");
        check_status(loader.load(nullptr, kGoodSha, kHostImplementedCaps,
                                 kHostImplementedTriggers),
                     LoadStatus::InvalidArgument, "null path");
        check_status(loader.load("", kGoodSha, kHostImplementedCaps,
                                 kHostImplementedTriggers),
                     LoadStatus::InvalidArgument, "empty path");
        const std::string valid = plugin_path("cm_test_plugin.so");
        check_status(loader.load(valid.c_str(), "deadbeef", kHostImplementedCaps,
                                 kHostImplementedTriggers),
                     LoadStatus::InvalidArgument, "malformed expected sha");
        const Loader no_dir(LoaderOps{});
        check_status(no_dir.load(valid.c_str(), kGoodSha, kHostImplementedCaps,
                                 kHostImplementedTriggers),
                     LoadStatus::InvalidArgument, "no whitelist directory");
    }

    void test_symlink_escape_real() {
        const std::string valid = plugin_path("cm_test_plugin.so");
        char tmpl[] = "/tmp/glk_plugin_link_XXXXXX";
        char *dir = ::mkdtemp(tmpl);
        check(dir != nullptr, "mkdtemp");
        const std::string link = std::string(dir) + "/escape.so";
        check(::symlink(valid.c_str(), link.c_str()) == 0, "symlink");

        const Loader loader(std::string(dir), default_loader_ops());
        const LoadResult result =
                loader.load(link.c_str(), kGoodSha, kHostImplementedCaps,
                            kHostImplementedTriggers);
        check_status(result, LoadStatus::PathRejected, "symlink escape");
        check(!result.module.valid(), "symlink escape no handle");

        (void)::unlink(link.c_str());
        (void)::rmdir(dir);
    }

    void test_counted_lifecycle() {
        const std::string valid = plugin_path("cm_test_plugin.so");
        const std::string badabi = plugin_path("cm_test_plugin_badabi.so");
        char good_digest[65] = {};
        char bad_digest[65] = {};
        check(sha256_file(valid.c_str(), good_digest, sizeof(good_digest)) == 0, "sha valid");
        check(sha256_file(badabi.c_str(), bad_digest, sizeof(bad_digest)) == 0, "sha badabi");

        counted::reset();
        {
            const Loader loader(std::string(GLK_TEST_PLUGIN_DIR), counted::ops());
            LoadResult result =
                    loader.load(valid.c_str(), good_digest, kHostImplementedCaps,
                                kHostImplementedTriggers);
            check_status(result, LoadStatus::Ok, "counted valid");
            check(counted::g_open == 1 && counted::g_close == 0, "handle open");
            LoadResult moved = std::move(result);
            check(!result.module.valid(), "moved-from invalid");
            check(moved.module.valid(), "moved-to valid");
        }
        check(counted::g_open == 1 && counted::g_close == 1, "released exactly once");

        counted::reset();
        const Loader loader(std::string(GLK_TEST_PLUGIN_DIR), counted::ops());
        for (int i = 0; i < 3; ++i) {
            const LoadResult result =
                    loader.load(badabi.c_str(), bad_digest, kHostImplementedCaps,
                                kHostImplementedTriggers);
            check_status(result, LoadStatus::AbiMismatch, "counted badabi");
            check(!result.module.valid(), "counted badabi invalid");
        }
        check(counted::g_open == 3 && counted::g_close == 3,
              "every failed load closed its handle");
    }

    void test_fake_rejections() {
        {
            fake::prepare();
            fake::g_module_storage.required_caps =
                    static_cast<std::uint32_t>(GLK_CAP_FILE_CACHE_WRITE);
            const LoadResult result = fake_load(kGoodSha);
            check_status(result, LoadStatus::CapsRejected, "reserved capability");
            check(fake::g_close == 1, "reserved capability closed handle");
        }
        {
            fake::prepare();
            fake::g_module_storage.required_caps = 1u << 20;
            check_status(fake_load(kGoodSha), LoadStatus::CapsRejected, "unknown capability");
        }
        {
            fake::prepare();
            fake::g_hook.trigger = GLK_TRIGGER_PERIODIC;
            check_status(fake_load(kGoodSha), LoadStatus::TriggerRejected, "reserved trigger");
        }
        {
            fake::prepare();
            fake::g_hook.trigger = GLK_TRIGGER_ON_LOAD;
            check_status(fake_load(kGoodSha), LoadStatus::TriggerRejected, "on-load trigger");
        }
        {
            fake::prepare();
            fake::g_hook.stage = GLK_STAGE_PRE_ROUTE;
            check_status(fake_load(kGoodSha), LoadStatus::StageRejected, "reserved stage");
        }
        {
            /* delta-4: POST_TERMINAL is now implemented by the 43284 LKM
             * residency window (contract-design.md 3.13), so it loads. PRE_ROUTE
             * stays reserved (asserted above). */
            fake::prepare();
            fake::g_hook.stage = GLK_STAGE_POST_TERMINAL;
            const LoadResult result = fake_load(kGoodSha);
            check_status(result, LoadStatus::Ok, "post-terminal stage");
            check(result.module.valid(), "post-terminal module valid");
        }
        {
            fake::prepare();
            fake::g_module_storage.hook_count = kMaxHooks + 1u;
            check_status(fake_load(kGoodSha), LoadStatus::HooksTooMany, "hooks too many");
        }
        {
            fake::prepare();
            fake::g_module_storage.hooks = nullptr;
            check_status(fake_load(kGoodSha), LoadStatus::HooksMissing, "hooks missing");
        }
        {
            fake::prepare();
            fake::g_hook.fn = nullptr;
            check_status(fake_load(kGoodSha), LoadStatus::HookInvalid, "null fn");
        }
        {
            fake::prepare();
            fake::g_hook.name = nullptr;
            check_status(fake_load(kGoodSha), LoadStatus::HookInvalid, "null hook name");
        }
        {
            fake::prepare();
            fake::g_module_storage.name = "";
            check_status(fake_load(kGoodSha), LoadStatus::NameInvalid, "empty module name");
        }
        {
            fake::prepare();
            fake::g_module_storage.version = "";
            check_status(fake_load(kGoodSha), LoadStatus::VersionInvalid,
                         "empty module version");
        }
        {
            fake::prepare();
            fake::g_module_storage.size = 4u;
            check_status(fake_load(kGoodSha), LoadStatus::SizeIncompatible, "small size");
        }
        {
            fake::prepare();
            fake::g_module_storage.abi_version = GLK_ABI_VERSION + 1u;
            check_status(fake_load(kGoodSha), LoadStatus::AbiMismatch, "fake bad abi");
        }
        {
            fake::prepare();
            fake::g_module = nullptr;
            check_status(fake_load(kGoodSha), LoadStatus::EntryRejected, "entry null");
        }
        {
            fake::prepare();
            fake::g_open_ok = false;
            const LoadResult result = fake_load(kGoodSha);
            check_status(result, LoadStatus::OpenFailed, "open failure");
            check(fake::g_close == 0, "open failure closed nothing");
        }
        {
            fake::prepare();
            fake::g_symbol_ok = false;
            const LoadResult result = fake_load(kGoodSha);
            check_status(result, LoadStatus::EntryMissing, "symbol missing");
            check(fake::g_close == 1, "symbol missing closed handle");
        }
        {
            fake::prepare();
            fake::g_sha_ok = false;
            check_status(fake_load(kGoodSha), LoadStatus::HashRejected, "sha failure");
        }
        {
            fake::prepare();
            fake::g_mode = 0100666u;
            const LoadResult result = fake_load(kGoodSha);
            check_status(result, LoadStatus::PermissionRejected, "group writable");
            check(fake::g_open == 0, "mode checked before open");
        }
        {
            fake::prepare();
            fake::g_exists = false;
            check_status(fake_load(kGoodSha), LoadStatus::FileMissing, "file missing");
        }
        {
            fake::prepare();
            const LoadResult result = fake_load(kGoodSha);
            check_status(result, LoadStatus::Ok, "fake valid");
            check(fake::g_open == 1 && fake::g_close == 0, "fake handle open");
        }
        check(fake::g_close == 1, "fake handle closed at scope exit");
    }

} // namespace

int32_t main() {
    test_sha256_vectors();
    test_valid_plugin_real();
    test_hash_mismatch_real();
    test_negative_plugins_real();
    test_path_rejections();
    test_symlink_escape_real();
    test_counted_lifecycle();
    test_fake_rejections();
    std::puts("countermeasure_loader_test: ok");
    return 0;
}
