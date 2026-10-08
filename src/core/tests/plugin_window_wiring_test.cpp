/* S4 P1 step 3a host test: the 43284 LKM residency window drives the plugin
 * host (design plugin-runtime-integration-design.md sections 4, 5, 12.2;
 * contract-design 3.14.7.8).
 *
 * No device and no real .so: the window runs on an injected LkmTransport (the
 * same seam cve_2026_43284_lkm_window_test uses) and the host's loader
 * operations synthesize the module, so the whole chain
 *   PluginHost -> LkmWindowRuntime::run -> hook -> glk_contract_ops -> channel
 * is exercised in one process. The assertions are:
 *   - the host is only dispatched while the residency window is open;
 *   - the hook receives the window's own glk_contract_ops and can use it;
 *   - a failing hook is counted by the host and NEVER fails the window body
 *     (fail-soft: the attack chain must survive any plugin fault);
 *   - a document without a plugin section leaves the window exactly as it was. */

#include "backend/cve_2026_43284/lkm_window.hpp"
#include "plugin/host.hpp"

#include "contract/countermeasure.hpp"
#include "plugin/loader.hpp"
#include "plugin/kernel_channel.hpp"
#include "profile/document.hpp"

#include <array>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

    using ghostlock::backend::cve_2026_43284::LkmWindowRuntime;
    using ghostlock::plugin::HostStage;
    using ghostlock::plugin::LoaderOps;
    using ghostlock::plugin::LkmTransport;
    using ghostlock::plugin::PluginHost;
    using ghostlock::plugin::RuntimeBackend;
    using ghostlock::plugin::WindowState;
    using ghostlock::profile::Document;
    using ghostlock::profile::Section;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::fprintf(stderr, "plugin_window_wiring_test: FAIL %s\n", what);
            std::abort();
        }
    }

    /* ---- fake /dev/glk transport (PING + READ + UNLOAD) ------------------ */

    struct FakeChannel final {
        std::array<std::uint8_t, 4096> kernel_mem{};
        std::uint64_t direct_base = 0xFFFF000000000000ULL;
        bool opened = false;
        bool closed = false;
        bool unloaded = false;
        std::uint32_t read_ops = 0U;
    };

    FakeChannel g_channel{};

    int fake_open(void *ctx) noexcept {
        (void)ctx;
        g_channel.opened = true;
        g_channel.closed = false;
        return 0;
    }

    void fake_close(void *ctx) noexcept {
        (void)ctx;
        g_channel.closed = true;
    }

    int fake_call(void *ctx, glk_lkm_req &req) noexcept {
        (void)ctx;
        if (req.abi_version != GLK_LKM_ABI_VERSION) {
            req.status = static_cast<std::uint32_t>(-EPROTO);
            return 0;
        }
        req.status = 0U;
        switch (req.op) {
        case GLK_LKM_PING:
            break;
        case GLK_LKM_READ: {
            const std::uint64_t offset = req.addr - g_channel.direct_base;
            if (offset + req.len > g_channel.kernel_mem.size()) {
                req.status = static_cast<std::uint32_t>(-EFAULT);
                break;
            }
            ++g_channel.read_ops;
            std::memcpy(reinterpret_cast<void *>(req.value),
                        g_channel.kernel_mem.data() + offset,
                        static_cast<std::size_t>(req.len));
            break;
        }
        case GLK_LKM_UNLOAD:
            g_channel.unloaded = true;
            break;
        default:
            req.status = static_cast<std::uint32_t>(-EOPNOTSUPP);
            break;
        }
        return 0;
    }

    LkmTransport transport() noexcept {
        LkmTransport t{};
        t.ctx = &g_channel;
        t.open = &fake_open;
        t.close = &fake_close;
        t.call = &fake_call;
        return t;
    }

    /* ---- fake loader operations that synthesize one module --------------- */

    const std::string kHash(64u, 'a');

    struct HookSpec final {
        std::int32_t rc = 0;
    };

    HookSpec g_hook_spec{};
    glk_hook g_hook{};
    glk_module g_module{};
    int g_open_count = 0;
    int g_close_count = 0;
    int g_hook_calls = 0;
    const glk_contract_ops *g_last_ops = nullptr;
    std::uint64_t g_last_read_value = 0U;
    std::int32_t g_last_read_rc = -1;

    std::int32_t post_terminal_hook(void *user, glk_stage stage,
                                    const glk_contract_ops *host) {
        (void)user;
        expect(stage == GLK_STAGE_POST_TERMINAL, "the hook fires at POST_TERMINAL");
        expect(host != nullptr, "the hook receives a contract ops table");
        expect(host->read_u64 != nullptr && host->log != nullptr,
               "the window publishes the channel primitives");
        ++g_hook_calls;
        g_last_ops = host;
        g_last_read_rc = host->read_u64(host->ctx, g_channel.direct_base,
                                        &g_last_read_value);
        return g_hook_spec.rc;
    }

    const glk_module *entry(std::uint32_t abi_version) {
        return abi_version == GLK_ABI_VERSION ? &g_module : nullptr;
    }

    void *open_lib(const char *) {
        ++g_open_count;
        return &g_module;
    }

    void close_lib(void *) { ++g_close_count; }

    void *sym(void *, const char *name) {
        if (std::strcmp(name, "glk_entry") != 0) {
            return nullptr;
        }
        return reinterpret_cast<void *>(&entry);
    }

    std::int32_t stat_mode(const char *, std::uint32_t *mode_out) {
        *mode_out = 0100644u;
        return 0;
    }

    bool exists(const char *) { return true; }

    std::int32_t sha256_fn(const char *, char *out_hex, std::size_t cap) {
        if (cap < 65u) {
            return -1;
        }
        std::memcpy(out_hex, kHash.c_str(), 65u);
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

    LoaderOps loader_ops() {
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

    /* One 43284 module whose single hook fires at POST_TERMINAL. */
    void install_module() {
        g_hook_spec = HookSpec{};
        g_hook = glk_hook{};
        g_hook.trigger = GLK_TRIGGER_ON_STAGE;
        g_hook.stage = GLK_STAGE_POST_TERMINAL;
        g_hook.priority = 0u;
        g_hook.fn = &post_terminal_hook;
        g_hook.user = &g_hook_spec;
        g_hook.name = "window.hook";

        g_module = glk_module{};
        g_module.abi_version = GLK_ABI_VERSION;
        g_module.size = GLK_MODULE_SIZE_V2;
        g_module.name = "mod.window";
        g_module.version = "1";
        g_module.required_caps = static_cast<std::uint32_t>(GLK_CAP_KERNEL_READ);
        g_module.hook_count = 1u;
        g_module.hooks = &g_hook;

        g_channel = FakeChannel{};
        g_open_count = 0;
        g_close_count = 0;
        g_hook_calls = 0;
        g_last_ops = nullptr;
        g_last_read_value = 0U;
        g_last_read_rc = -1;
    }

    /* The composition seam's POST_TERMINAL consumer, mirroring
     * execution_binding.cpp: the window only sees this neutral thunk. */
    void dispatch_post_terminal(void *ctx, const glk_contract_ops *ops) noexcept {
        auto *host = static_cast<PluginHost *>(ctx);
        expect(host != nullptr, "the sink context is the host");
        host->dispatch(HostStage::PostTerminal,
                       ghostlock::plugin::PluginCallContext{ops});
    }

    LkmWindowRuntime::PluginStageSink host_sink(PluginHost &host) noexcept {
        return LkmWindowRuntime::PluginStageSink{&dispatch_post_terminal, &host};
    }

    Document document_with_plugin(const char *stage) {
        Document document{};
        Section &section = document.append_section("plugin");
        section.add("glk.probe.enabled", 1u, 1u);
        section.add_text("glk.probe.stage", stage);
        section.add_text("glk.probe.module_path", "glk.probe/1.0/glk_probe.so");
        section.add_text("glk.probe.module_hash", kHash);
        return document;
    }

    /* ---- tests ---------------------------------------------------------- */

    void test_host_dispatches_inside_the_window() {
        install_module();
        const Document document = document_with_plugin("post_terminal");
        PluginHost host = PluginHost::from_document(
                document, RuntimeBackend::Cve2026_43284, loader_ops());
        expect(host.registered() == 1u, "one plugin registered");
        expect(host.loaded() == 0u, "registration does not load");
        expect(g_open_count == 0, "no dlopen before open()");

        LkmWindowRuntime window{};
        window.set_test_transport(transport());
        window.attach_plugin_stage(host_sink(host));
        expect(window.open(), "the window opens on the fake channel");
        expect(host.loaded() == 0u, "opening the window does not load plugins");
        expect(host.open(WindowState::WaiterClosed), "the host opens after the window");
        expect(host.loaded() == 1u, "one module loaded");
        expect(g_open_count == 1, "exactly one dlopen");

        expect(window.run(), "the window body succeeds");
        expect(g_hook_calls == 1, "the hook ran once inside the window");
        expect(host.diagnostics().called == 1u, "the host counted the call");
        expect(host.diagnostics().hook_failed == 0u, "no failure");
        expect(g_last_ops != nullptr, "the hook saw the window ops");
        expect(g_last_read_rc == 0 && g_channel.read_ops == 1u,
               "the hook really read through /dev/glk");
        expect(window.hook_calls() == 0u,
               "the legacy registry counter is untouched by the host path");
        expect(window.is_open(), "the window stays open");

        window.close();
        expect(g_channel.unloaded, "the window unloaded the module");
        host.close();
        expect(g_close_count == 1, "the plugin module was unloaded once");
        expect(host.is_closed(), "the host is terminal");
        std::puts("plugin_window_wiring_test: host_dispatches_inside_the_window ok");
    }

    void test_hook_failure_is_fail_soft() {
        install_module();
        g_hook_spec.rc = -7;
        const Document document = document_with_plugin("post_terminal");
        PluginHost host = PluginHost::from_document(
                document, RuntimeBackend::Cve2026_43284, loader_ops());

        LkmWindowRuntime window{};
        window.set_test_transport(transport());
        window.attach_plugin_stage(host_sink(host));
        expect(window.open(), "window open");
        expect(host.open(WindowState::WaiterClosed), "host open");

        /* The plugin fault must never become a window-body failure: the chain
         * terminus only closes the window, and lkm_window_failed is diagnostic. */
        expect(window.run(), "a failing hook does not fail the window body");
        expect(host.diagnostics().called == 1u, "the hook was called");
        expect(host.diagnostics().hook_failed == 1u, "the failure is counted");
        expect(window.hook_calls() == 0u, "the legacy counter stays untouched");
        expect(window.is_open(), "the residency window is unaffected");
        host.close();
        window.close();
        std::puts("plugin_window_wiring_test: hook_failure_is_fail_soft ok");
    }

    void test_no_dispatch_outside_the_window() {
        install_module();
        const Document document = document_with_plugin("post_terminal");
        PluginHost host = PluginHost::from_document(
                document, RuntimeBackend::Cve2026_43284, loader_ops());
        expect(host.open(WindowState::WaiterClosed), "host open");

        /* Never-opened window: run() is closed and must not dispatch. */
        LkmWindowRuntime unopened{};
        unopened.attach_plugin_stage(host_sink(host));
        expect(!unopened.run(), "a never-opened window is not a body call");
        expect(g_hook_calls == 0, "no hook without an open window");

        /* Opened then closed: terminal, no further dispatch. */
        LkmWindowRuntime window{};
        window.set_test_transport(transport());
        window.attach_plugin_stage(host_sink(host));
        expect(window.open(), "window open");
        expect(window.run(), "first body call");
        expect(g_hook_calls == 1, "one call while open");
        window.close();
        expect(!window.run(), "a closed window is terminal");
        expect(g_hook_calls == 1, "no dispatch after close");
        host.close();
        std::puts("plugin_window_wiring_test: no_dispatch_outside_the_window ok");
    }

    void test_no_plugin_section_leaves_the_window_unchanged() {
        install_module();
        const Document empty{};
        PluginHost host = PluginHost::from_document(
                empty, RuntimeBackend::Cve2026_43284, loader_ops());
        expect(host.registered() == 0u, "an empty document registers nothing");

        LkmWindowRuntime window{};
        window.set_test_transport(transport());
        window.attach_plugin_stage(host_sink(host));
        expect(window.open(), "window open");
        expect(host.open(WindowState::WaiterClosed), "host open is a no-op");
        expect(window.run(), "the window body still runs");
        expect(g_hook_calls == 0, "no hook");
        expect(g_open_count == 0 && g_close_count == 0, "no loader activity");
        expect(host.records().empty(), "no records");
        host.close();
        window.close();
        std::puts("plugin_window_wiring_test: no_plugin_section_leaves_the_window_unchanged ok");
    }

} // namespace

int main() {
    /* The loader resolves module_path against <GHOSTLOCK_HOME>/countermeasures;
     * the injected operations never touch the filesystem. */
    expect(setenv("GHOSTLOCK_HOME", "/glk-plugin-window-test", 1) == 0,
           "GHOSTLOCK_HOME is set");

    test_host_dispatches_inside_the_window();
    test_hook_failure_is_fail_soft();
    test_no_dispatch_outside_the_window();
    test_no_plugin_section_leaves_the_window_unchanged();

    /* RUNTIME DISABLE (user directive 2026-10-05): the mechanism asserted above
     * is retained library behaviour, but the host is NOT wired into a run. This
     * assert pins the disabled state so the intent stays auditable next to the
     * tests that would otherwise imply a live dispatch path; restoring the
     * feature flips the switch in plugin/host.hpp and runs the 43284 device gate
     * (see the comment there). */
    expect(!ghostlock::plugin::kPluginRuntimeEnabled,
           "the plugin runtime is disabled by the composition-root switch");

    std::puts("plugin_window_wiring_test: ok");
    return 0;
}
