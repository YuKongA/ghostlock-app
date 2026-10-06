/* P1 step 2 host test: registration -> window-gated load -> stage dispatch ->
 * unload, without a device, a real .so or the filesystem. The loader's injected
 * operations synthesize the modules (the same technique as
 * countermeasure_loader_test), so every branch -- refusal counts, hook order,
 * hook failure, the per-backend stage matrix and the fail-soft paths -- is
 * driven deterministically. host.cpp, wire.cpp and loader.cpp are the real
 * production units under test.
 *
 * Acceptance (design plugin-runtime-integration-design.md sections 11.5, 12,
 * 13.2; frozen contract-design 3.14.7.8):
 *   1. open(WaiterAlive) -> false, open_rejected == 1, dlopen count == 0 (R1);
 *   2. dispatch before open() -> skipped, zero hook calls;
 *   3. dispatch after close() -> skipped, zero hook calls;
 *   4. stage order strictly increasing and at most once per plugin per stage;
 *   5. a non-zero hook disables that module's later hooks, other modules run;
 *   6. stage unavailable on the backend -> hook refused, stage_unavailable
 *      accounted, hook never called (judged by stage_available_on, no copied
 *      matrix);
 *   7. a document without a plugin section is a complete no-op;
 *   8. (extra) load failure and a wire-rejected document stay fail-soft. */

#include "plugin/host.hpp"

#include "contract/countermeasure.hpp"
#include "plugin/loader.hpp"
#include "profile/document.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using ghostlock::contract::CountermeasureStage;
using ghostlock::plugin::HostDiagnostics;
using ghostlock::plugin::HostReason;
using ghostlock::plugin::HostRecord;
using ghostlock::plugin::HostStage;
using ghostlock::plugin::host_stage_from_token;
using ghostlock::plugin::host_stage_index;
using ghostlock::plugin::host_stage_token;
using ghostlock::plugin::kNoHostStage;
using ghostlock::plugin::LoaderOps;
using ghostlock::plugin::PluginCallContext;
using ghostlock::plugin::PluginHost;
using ghostlock::plugin::RuntimeBackend;
using ghostlock::plugin::stage_available_on;
using ghostlock::plugin::WindowState;
using ghostlock::profile::Document;
using ghostlock::profile::Section;

namespace {

    [[noreturn]] void fail(const char *what) {
        std::fprintf(stderr, "plugin_host_test: FAIL %s\n", what);
        std::abort();
    }

    void expect(bool condition, const char *what) {
        if (!condition) {
            fail(what);
        }
    }

    /* 64 lowercase hex digits; the injected sha256 always returns it. */
    const std::string kHash(64u, 'a');

    /* ---- injected loader operations that synthesize modules -------------- */

    namespace fake {

        constexpr std::size_t kMaxModules = 4u;
        constexpr std::size_t kMaxHooksPerModule = 4u;

        struct HookSpec final {
            const char *label = nullptr;
            std::int32_t rc = 0;
        };

        struct ModuleSpec final {
            const char *name = nullptr;
            const char *version = nullptr;
            const glk_hook *hooks = nullptr;
            std::uint32_t hook_count = 0u;
        };

        std::array<ModuleSpec, kMaxModules> g_specs{};
        std::array<glk_module, kMaxModules> g_modules{};
        std::size_t g_module_count = 0u;
        std::size_t g_current = 0u;
        int g_open_count = 0;
        int g_close_count = 0;
        std::vector<std::size_t> g_close_order{};
        bool g_hash_mismatch = false;
        std::vector<std::string> g_calls{};
        glk_stage g_last_stage = GLK_STAGE_PRE_SPAWN;
        const glk_contract_ops *g_last_ops = nullptr;

        std::int32_t hook_fn(void *user, glk_stage stage, const glk_contract_ops *host) {
            const HookSpec *spec = static_cast<const HookSpec *>(user);
            g_calls.emplace_back(spec != nullptr && spec->label != nullptr ? spec->label
                                                                          : "?");
            g_last_stage = stage;
            g_last_ops = host;
            return spec != nullptr ? spec->rc : 0;
        }

        const glk_module *entry(std::uint32_t abi_version) {
            if (abi_version != GLK_ABI_VERSION) {
                return nullptr;
            }
            return &g_modules[g_current];
        }

        /* Handles are the module slots themselves, so sym() can recover which
         * module the loader is resolving without a side table. */
        void *open_lib(const char *) {
            const std::size_t index = static_cast<std::size_t>(g_open_count);
            if (index >= g_module_count) {
                return nullptr;
            }
            ++g_open_count;
            return &g_modules[index];
        }

        void *sym(void *handle, const char *name) {
            if (std::strcmp(name, "glk_entry") != 0) {
                return nullptr;
            }
            const auto *slot = static_cast<glk_module *>(handle);
            g_current = static_cast<std::size_t>(slot - g_modules.data());
            return reinterpret_cast<void *>(&entry);
        }

        /* Records which module slot was released, in release order, so the
         * teardown order (reverse registration) is observable. */
        void close_lib(void *handle) {
            ++g_close_count;
            const auto *slot = static_cast<const glk_module *>(handle);
            const std::size_t index =
                    static_cast<std::size_t>(slot - g_modules.data());
            if (index < g_module_count) {
                g_close_order.push_back(index);
            }
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
            if (g_hash_mismatch) {
                std::memset(out_hex, '0', 64u);
            } else {
                std::memcpy(out_hex, kHash.c_str(), 65u);
            }
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

        /* Registers the modules in DLOPEN order (the order of the plugins that
         * actually get loaded, which is the registration order filtered by the
         * backend stage matrix). */
        void install(const ModuleSpec *specs, std::size_t count) {
            expect(count <= kMaxModules, "fake module bound");
            g_specs = {};
            g_modules = {};
            g_module_count = count;
            g_current = 0u;
            g_open_count = 0;
            g_close_count = 0;
            g_close_order.clear();
            g_hash_mismatch = false;
            g_calls.clear();
            g_last_stage = GLK_STAGE_PRE_SPAWN;
            g_last_ops = nullptr;
            for (std::size_t i = 0u; i < count; ++i) {
                ModuleSpec &spec = g_specs[i];
                spec = specs[i];
                glk_module &module = g_modules[i];
                module = glk_module{};
                module.abi_version = GLK_ABI_VERSION;
                module.size = GLK_MODULE_SIZE_V2;
                module.name = spec.name;
                module.version = spec.version;
                module.required_caps = 0u;
                module.hook_count = spec.hook_count;
                module.hooks = spec.hooks;
            }
        }

        /* Owns one module's hook table; keep it alive for the host's lifetime. */
        struct HookSet final {
            std::array<glk_hook, kMaxHooksPerModule> hooks{};
            std::array<HookSpec, kMaxHooksPerModule> specs{};
            std::uint32_t count = 0u;

            void add(const char *label, glk_stage stage, std::uint32_t priority,
                     std::int32_t rc) {
                expect(count < kMaxHooksPerModule, "fake hook bound");
                specs[count] = HookSpec{label, rc};
                glk_hook &hook = hooks[count];
                hook = glk_hook{};
                hook.trigger = GLK_TRIGGER_ON_STAGE;
                hook.stage = stage;
                hook.priority = priority;
                hook.period_ms = 0u;
                hook.fn = &hook_fn;
                hook.user = &specs[count];
                hook.name = label;
                ++count;
            }
        };

    } // namespace fake

    /* ---- document helpers ------------------------------------------------- */

    struct PluginDoc final {
        const char *id = nullptr;
        const char *stage = nullptr;
        const char *path = nullptr;
    };

    Document make_doc(const PluginDoc *plugins, std::size_t count) {
        Document document{};
        Section &section = document.append_section("plugin");
        for (std::size_t i = 0u; i < count; ++i) {
            const std::string prefix = std::string(plugins[i].id) + ".";
            section.add(prefix + "enabled", 1u, 1u);
            section.add_text(prefix + "stage", plugins[i].stage);
            section.add_text(prefix + "module_path", plugins[i].path);
            section.add_text(prefix + "module_hash", kHash);
        }
        return document;
    }

    bool has_reason(const PluginHost &host, HostReason reason) {
        for (const HostRecord &record : host.records()) {
            if (record.reason == reason) {
                return true;
            }
        }
        return false;
    }

    /* ---- 1. registration is not loading ----------------------------------- */

    void test_registration_does_not_load() {
        fake::HookSet hooks;
        hooks.add("a.0", GLK_STAGE_PRE_TERMINAL, 0u, 0);
        const fake::ModuleSpec modules[] = {
            {"mod.a", "1", hooks.hooks.data(), hooks.count}};
        fake::install(modules, 1);
        const PluginDoc plugins[] = {{"a", "pre_terminal", "a/1.0/a.so"}};
        const Document document = make_doc(plugins, 1);

        const PluginHost host = PluginHost::from_document(
                document, RuntimeBackend::Cve2026_43499, fake::ops());
        expect(host.registered() == 1u, "one plugin registered");
        expect(fake::g_open_count == 0, "from_document never dlopens");
        expect(fake::g_close_count == 0, "from_document never closes a handle");
        expect(host.diagnostics().loaded == 0u, "nothing is loaded before open");
        expect(host.records().empty(), "registration alone records nothing");
        expect(host.countermeasures_root() == "/glk-plugin-host-test/countermeasures",
               "the root is <GHOSTLOCK_HOME>/countermeasures (contract-design 3.14.7.4)");
        std::puts("plugin_host_test: registration_does_not_load ok");
    }

    /* ---- 2. R1: open(WaiterAlive) refuses without dlopen ------------------ */

    void test_open_rejected_when_waiter_alive() {
        fake::HookSet hooks;
        hooks.add("a.0", GLK_STAGE_PRE_TERMINAL, 0u, 0);
        const fake::ModuleSpec modules[] = {
            {"mod.a", "1", hooks.hooks.data(), hooks.count}};
        fake::install(modules, 1);
        const PluginDoc plugins[] = {{"a", "pre_terminal", "a/1.0/a.so"}};
        const Document document = make_doc(plugins, 1);

        PluginHost host = PluginHost::from_document(
                document, RuntimeBackend::Cve2026_43499, fake::ops());
        expect(!host.open(WindowState::WaiterAlive), "open(WaiterAlive) is refused");
        expect(host.diagnostics().open_rejected == 1u, "open_rejected == 1");
        expect(fake::g_open_count == 0, "R1 refusal never dlopens");
        expect(fake::g_close_count == 0, "R1 refusal opens no handle to close");
        expect(host.diagnostics().loaded == 0u, "R1 refusal loads nothing");
        expect(!host.is_open(), "host stays closed");
        expect(host.records().size() == 1u, "one refusal record");
        expect(host.records()[0].reason == HostReason::WindowNotClosed,
               "refusal reason is WindowNotClosed");
        expect(std::string(ghostlock::plugin::host_reason_name(host.records()[0].reason)) ==
                       "WindowNotClosed",
               "WindowNotClosed is the frozen name");
        expect(host.format_diagnostics().find("open_rejected=1") != std::string::npos,
               "diagnostics carry the refusal counter");

        /* The refusal is not terminal: the same host opens once the window is
         * closed, and dispatch then runs the hook exactly once. */
        expect(host.open(WindowState::WaiterClosed), "retry with WaiterClosed");
        expect(fake::g_open_count == 1, "one dlopen after the legal open");
        expect(host.diagnostics().loaded == 1u, "one module loaded");
        expect(host.open(WindowState::WaiterClosed), "open is idempotent");
        expect(fake::g_open_count == 1, "idempotent open does not reload");
        expect(host.diagnostics().open_rejected == 1u, "still exactly one refusal");

        const glk_contract_ops ops{};
        const PluginCallContext context{&ops};
        host.dispatch(HostStage::PreTerminal, context);
        expect(host.diagnostics().called == 1u, "the hook ran once");
        expect(fake::g_calls.size() == 1u && fake::g_calls[0] == "a.0", "hook a.0 ran");
        host.close();
        std::puts("plugin_host_test: open_rejected_when_waiter_alive ok");
    }

    /* ---- 3. dispatch before open() ---------------------------------------- */

    void test_dispatch_before_open_is_skipped() {
        fake::HookSet hooks;
        hooks.add("a.0", GLK_STAGE_PRE_TERMINAL, 0u, 0);
        const fake::ModuleSpec modules[] = {
            {"mod.a", "1", hooks.hooks.data(), hooks.count}};
        fake::install(modules, 1);
        const PluginDoc plugins[] = {{"a", "pre_terminal", "a/1.0/a.so"}};
        const Document document = make_doc(plugins, 1);

        PluginHost host = PluginHost::from_document(
                document, RuntimeBackend::Cve2026_43499, fake::ops());
        host.dispatch(HostStage::PreTerminal, PluginCallContext{});
        expect(host.diagnostics().skipped == 1u, "dispatch before open is skipped");
        expect(host.diagnostics().called == 0u, "no hook is called");
        expect(fake::g_calls.empty(), "no hook ran");
        expect(fake::g_open_count == 0, "dispatch never loads implicitly");
        expect(host.diagnostics().loaded == 0u, "nothing loaded");
        std::puts("plugin_host_test: dispatch_before_open_is_skipped ok");
    }

    /* ---- 4. dispatch after close() ---------------------------------------- */

    void test_dispatch_after_close_is_skipped() {
        fake::HookSet hooks;
        hooks.add("a.0", GLK_STAGE_PRE_TERMINAL, 0u, 0);
        const fake::ModuleSpec modules[] = {
            {"mod.a", "1", hooks.hooks.data(), hooks.count}};
        fake::install(modules, 1);
        const PluginDoc plugins[] = {{"a", "pre_terminal", "a/1.0/a.so"}};
        const Document document = make_doc(plugins, 1);

        PluginHost host = PluginHost::from_document(
                document, RuntimeBackend::Cve2026_43499, fake::ops());
        expect(host.open(WindowState::WaiterClosed), "legal open");
        host.dispatch(HostStage::PreTerminal, PluginCallContext{});
        expect(host.diagnostics().called == 1u, "one call while open");
        host.close();
        expect(host.is_closed(), "host closed");
        expect(fake::g_close_count == 1, "the module was unloaded once");
        host.close();
        expect(fake::g_close_count == 1, "close is idempotent");

        host.dispatch(HostStage::PreTerminal, PluginCallContext{});
        expect(host.diagnostics().called == 1u, "no call after close");
        expect(host.diagnostics().skipped == 1u, "the post-close dispatch is skipped");
        expect(fake::g_calls.size() == 1u, "no hook ran after close");
        expect(!host.open(WindowState::WaiterClosed), "a closed host never reopens");
        expect(fake::g_open_count == 1, "no reload attempt");
        std::puts("plugin_host_test: dispatch_after_close_is_skipped ok");
    }

    /* ---- 4b. teardown order: reverse registration ------------------------- */

    void test_close_unloads_in_reverse_order() {
        fake::HookSet a;
        a.add("a.0", GLK_STAGE_PRE_TERMINAL, 0u, 0);
        fake::HookSet b;
        b.add("b.0", GLK_STAGE_PRE_TERMINAL, 0u, 0);
        const fake::ModuleSpec modules[] = {{"mod.a", "1", a.hooks.data(), a.count},
                                            {"mod.b", "1", b.hooks.data(), b.count}};
        fake::install(modules, 2);
        const PluginDoc plugins[] = {{"a", "pre_terminal", "a/a.so"},
                                     {"b", "pre_terminal", "b/b.so"}};
        const Document document = make_doc(plugins, 2);

        PluginHost host = PluginHost::from_document(
                document, RuntimeBackend::Cve2026_43499, fake::ops());
        expect(host.open(WindowState::WaiterClosed), "legal open");
        expect(host.diagnostics().loaded == 2u, "both modules loaded");
        expect(fake::g_close_order.empty(), "nothing is unloaded while open");
        host.close();
        expect(fake::g_close_order.size() == 2u, "both modules unloaded");
        expect(fake::g_close_order[0] == 1u && fake::g_close_order[1] == 0u,
               "modules unload in reverse registration order");
        std::puts("plugin_host_test: close_unloads_in_reverse_order ok");
    }

    /* ---- 5. stage order, at most once per stage, context threading -------- */

    void test_stage_order_and_once_per_stage() {
        fake::HookSet a;
        a.add("a.0", GLK_STAGE_PRE_TERMINAL, 0u, 0);
        fake::HookSet b;
        b.add("b.0", GLK_STAGE_PRE_TERMINAL, 0u, 0); /* same priority, later module */
        b.add("b.1", GLK_STAGE_PRE_TERMINAL, 1u, 0);
        const fake::ModuleSpec modules[] = {{"mod.a", "1", a.hooks.data(), a.count},
                                            {"mod.b", "1", b.hooks.data(), b.count}};
        fake::install(modules, 2);
        const PluginDoc plugins[] = {{"a", "pre_terminal", "a/a.so"},
                                     {"b", "pre_terminal", "b/b.so"}};
        const Document document = make_doc(plugins, 2);

        PluginHost host = PluginHost::from_document(
                document, RuntimeBackend::Cve2026_43499, fake::ops());
        expect(host.open(WindowState::WaiterClosed), "legal open");
        expect(host.diagnostics().loaded == 2u, "both modules loaded");

        glk_contract_ops ops{};
        const PluginCallContext context{&ops};
        host.dispatch(HostStage::PreTerminal, context);
        expect(fake::g_calls.size() == 3u, "three hooks ran");
        expect(fake::g_calls[0] == "a.0" && fake::g_calls[1] == "b.0" &&
                       fake::g_calls[2] == "b.1",
               "hooks run in (priority, registration) order");
        expect(fake::g_last_ops == &ops, "the call context reaches the hook unchanged");
        expect(fake::g_last_stage == GLK_STAGE_PRE_TERMINAL,
               "HostStage maps to the ABI stage");

        /* At most once per plugin per stage: a repeated dispatch is skipped. */
        host.dispatch(HostStage::PreTerminal, context);
        expect(host.diagnostics().called == 3u, "no hook runs twice");
        expect(host.diagnostics().skipped == 2u, "both plugins report a skipped repeat");

        /* A stage no plugin is registered for is not a skip: nothing is armed. */
        host.dispatch(HostStage::PreSpawn, context);
        host.dispatch(HostStage::PostSpawn, context);
        host.dispatch(HostStage::PostTerminal, context);
        expect(host.diagnostics().skipped == 2u, "unarmed stages stay silent");
        expect(fake::g_calls.size() == 3u, "no further hook ran");

        host.close();
        std::puts("plugin_host_test: stage_order_and_once_per_stage ok");
    }

    /* ---- 6. a failing hook disables its module, not the others ------------ */

    void test_hook_failure_isolates_the_module() {
        fake::HookSet a;
        a.add("a.first", GLK_STAGE_PRE_TERMINAL, 0u, 7);
        a.add("a.second", GLK_STAGE_PRE_TERMINAL, 1u, 0);
        fake::HookSet b;
        b.add("b.0", GLK_STAGE_PRE_TERMINAL, 0u, 0);
        const fake::ModuleSpec modules[] = {{"mod.a", "1", a.hooks.data(), a.count},
                                            {"mod.b", "1", b.hooks.data(), b.count}};
        fake::install(modules, 2);
        const PluginDoc plugins[] = {{"a", "pre_terminal", "a/a.so"},
                                     {"b", "pre_terminal", "b/b.so"}};
        const Document document = make_doc(plugins, 2);

        PluginHost host = PluginHost::from_document(
                document, RuntimeBackend::Cve2026_43499, fake::ops());
        expect(host.open(WindowState::WaiterClosed), "legal open");
        host.dispatch(HostStage::PreTerminal, PluginCallContext{});
        expect(host.diagnostics().called == 2u, "a.first and b.0 ran");
        expect(host.diagnostics().hook_failed == 1u, "a.first failed");
        expect(fake::g_calls.size() == 2u, "a.second did not run");
        expect(fake::g_calls[0] == "a.first" && fake::g_calls[1] == "b.0",
               "the other module is unaffected");
        expect(has_reason(host, HostReason::HookFailed), "the failure is recorded");
        const std::string text = host.format_diagnostics();
        expect(text.find("reason=HookFailed") != std::string::npos, "named HookFailed");
        expect(text.find("stage=pre_terminal") != std::string::npos,
               "the record names the stage token");
        host.close();
        std::puts("plugin_host_test: hook_failure_isolates_the_module ok");
    }

    /* ---- 7. the per-backend stage matrix ---------------------------------- */

    void test_stage_matrix_rejects_hooks_and_plugins() {
        /* The judge is stage_available_on(); the test pins its two usable cells
         * so a matrix change cannot pass silently. */
        expect(stage_available_on(RuntimeBackend::Cve2026_43499,
                                  CountermeasureStage::PreTerminal),
               "43499 exposes pre_terminal");
        expect(!stage_available_on(RuntimeBackend::Cve2026_43499,
                                   CountermeasureStage::PostTerminal),
               "43499 does not expose post_terminal");
        expect(stage_available_on(RuntimeBackend::Cve2026_43284,
                                  CountermeasureStage::PostTerminal),
               "43284 exposes post_terminal");

        /* (a) A plugin declared for a stage this backend does not expose is
         * refused as a plugin, visibly, without a dlopen. */
        {
            fake::HookSet unused;
            unused.add("p.0", GLK_STAGE_POST_TERMINAL, 0u, 0);
            const fake::ModuleSpec modules[] = {
                {"mod.p", "1", unused.hooks.data(), unused.count}};
            fake::install(modules, 1);
            const PluginDoc plugins[] = {{"p", "post_terminal", "p/p.so"}};
            const Document document = make_doc(plugins, 1);

            PluginHost host = PluginHost::from_document(
                    document, RuntimeBackend::Cve2026_43499, fake::ops());
            expect(host.open(WindowState::WaiterClosed), "open still succeeds");
            expect(fake::g_open_count == 0, "an unusable stage never dlopens");
            expect(host.diagnostics().loaded == 0u, "nothing loaded");
            expect(host.diagnostics().rejected == 1u, "the plugin is rejected");
            expect(has_reason(host, HostReason::StageUnavailableOnBackend),
                   "StageUnavailableOnBackend is recorded");
            const std::string text = host.format_diagnostics();
            expect(text.find("stage=post_terminal") != std::string::npos,
                   "the record names the unavailable stage (ABI slot != enum value)");
            host.dispatch(HostStage::PostTerminal, PluginCallContext{});
            expect(host.diagnostics().called == 0u, "no hook of a rejected plugin runs");
            expect(fake::g_calls.empty(), "no hook ran");
            host.close();
        }

        /* (b) A loaded module may declare hooks for both backends: on 43499 the
         * post_terminal hook is refused as a HOOK (not as a plugin) and
         * accounted, while the pre_terminal hook still runs. */
        {
            fake::HookSet mixed;
            mixed.add("h.post", GLK_STAGE_POST_TERMINAL, 0u, 0);
            mixed.add("h.pre", GLK_STAGE_PRE_TERMINAL, 0u, 0);
            const fake::ModuleSpec modules[] = {
                {"mod.h", "1", mixed.hooks.data(), mixed.count}};
            fake::install(modules, 1);
            const PluginDoc plugins[] = {{"h", "pre_terminal", "h/h.so"}};
            const Document document = make_doc(plugins, 1);

            PluginHost host = PluginHost::from_document(
                    document, RuntimeBackend::Cve2026_43499, fake::ops());
            expect(host.open(WindowState::WaiterClosed), "legal open");
            expect(host.diagnostics().loaded == 1u, "the plugin survives its hook");
            expect(host.diagnostics().stage_unavailable == 1u,
                   "one hook refused by the matrix");
            expect(host.diagnostics().rejected == 0u, "the plugin itself is not rejected");
            expect(has_reason(host, HostReason::StageUnavailableOnBackend),
                   "refusal reason recorded");
            host.dispatch(HostStage::PreTerminal, PluginCallContext{});
            expect(host.diagnostics().called == 1u, "the usable hook ran");
            expect(fake::g_calls.size() == 1u && fake::g_calls[0] == "h.pre",
                   "the refused hook never ran");
            host.close();
        }

        /* (c) Mirror image on 43284: pre_terminal hooks are refused, the
         * post_terminal hook runs. */
        {
            fake::HookSet mixed;
            mixed.add("m.pre", GLK_STAGE_PRE_TERMINAL, 0u, 0);
            mixed.add("m.post", GLK_STAGE_POST_TERMINAL, 0u, 0);
            const fake::ModuleSpec modules[] = {
                {"mod.m", "1", mixed.hooks.data(), mixed.count}};
            fake::install(modules, 1);
            const PluginDoc plugins[] = {{"m", "post_terminal", "m/m.so"}};
            const Document document = make_doc(plugins, 1);

            PluginHost host = PluginHost::from_document(
                    document, RuntimeBackend::Cve2026_43284, fake::ops());
            expect(host.open(WindowState::WaiterClosed), "legal open");
            expect(host.diagnostics().loaded == 1u, "loaded on 43284");
            expect(host.diagnostics().stage_unavailable == 1u, "pre_terminal refused");
            host.dispatch(HostStage::PostTerminal, PluginCallContext{});
            expect(host.diagnostics().called == 1u, "the post_terminal hook ran");
            expect(fake::g_calls.size() == 1u && fake::g_calls[0] == "m.post",
                   "only the usable hook ran");
            host.close();
        }

        /* The token helpers round-trip the four vocabulary entries. */
        {
            HostStage stage = HostStage::PreSpawn;
            expect(host_stage_from_token("post_terminal", stage) &&
                           stage == HostStage::PostTerminal,
                   "post_terminal parses");
            expect(host_stage_from_token("pre_terminal", stage) &&
                           stage == HostStage::PreTerminal,
                   "pre_terminal parses");
            expect(!host_stage_from_token("pre_route", stage), "pre_route is not a host stage");
            expect(host_stage_token(HostStage::PostTerminal) == "post_terminal",
                   "token spelling");
            expect(host_stage_index(HostStage::PostTerminal) == 3u &&
                           kNoHostStage == 0xFFu,
                   "stage slots are pinned");
        }
        std::puts("plugin_host_test: stage_matrix_rejects_hooks_and_plugins ok");
    }

    /* ---- 8. fail-soft: tampered artifact and a rejected document ---------- */

    void test_load_failure_is_fail_soft() {
        fake::HookSet hooks;
        hooks.add("t.0", GLK_STAGE_PRE_TERMINAL, 0u, 0);
        const fake::ModuleSpec modules[] = {
            {"mod.t", "1", hooks.hooks.data(), hooks.count}};
        fake::install(modules, 1);
        fake::g_hash_mismatch = true; /* the artifact no longer matches the document */
        const PluginDoc plugins[] = {{"t", "pre_terminal", "t/t.so"}};
        const Document document = make_doc(plugins, 1);

        PluginHost host = PluginHost::from_document(
                document, RuntimeBackend::Cve2026_43499, fake::ops());
        expect(host.open(WindowState::WaiterClosed), "the chain is never failed by a plugin");
        expect(fake::g_open_count == 0, "a hash mismatch never dlopens");
        expect(host.diagnostics().loaded == 0u, "nothing loaded");
        expect(host.diagnostics().load_failed == 1u, "counted as a load failure");
        expect(has_reason(host, HostReason::LoadFailed), "LoadFailed recorded");
        expect(host.format_diagnostics().find("status=HashMismatch") != std::string::npos,
               "the load status is named");
        host.dispatch(HostStage::PreTerminal, PluginCallContext{});
        expect(host.diagnostics().called == 0u, "an unloaded plugin never runs");
        expect(host.diagnostics().skipped == 1u, "it is skipped, not silently ignored");
        host.close();

        /* A document whose plugin section fails the P1 shape check registers
         * nothing and records the named wire error. */
        Document rejected{};
        Section &section = rejected.append_section("plugin");
        section.add_text("t.stage", "pre_terminal"); /* no enabled=true */
        const PluginHost empty = PluginHost::from_document(
                rejected, RuntimeBackend::Cve2026_43499, fake::ops());
        expect(empty.registered() == 0u, "nothing registered");
        expect(empty.diagnostics().rejected == 1u, "the document is rejected once");
        expect(has_reason(empty, HostReason::WireRejected), "WireRejected recorded");
        expect(empty.format_diagnostics().find("error=EnabledMissing") != std::string::npos,
               "the wire error is named");
        std::puts("plugin_host_test: load_failure_is_fail_soft ok");
    }

    /* ---- 9. no plugin section: today's run, byte for byte ---------------- */

    void test_no_plugin_section_is_a_no_op() {
        const Document empty{};
        PluginHost host = PluginHost::from_document(
                empty, RuntimeBackend::Cve2026_43499, fake::ops());
        expect(host.registered() == 0u, "an empty host has no entries");
        expect(!host.is_open() && !host.is_closed(), "an empty host starts inert");
        expect(host.open(WindowState::WaiterClosed), "open is a no-op success");
        host.dispatch(HostStage::PreSpawn, PluginCallContext{});
        host.dispatch(HostStage::PostSpawn, PluginCallContext{});
        host.dispatch(HostStage::PreTerminal, PluginCallContext{});
        host.dispatch(HostStage::PostTerminal, PluginCallContext{});
        host.close();
        const HostDiagnostics &diagnostics = host.diagnostics();
        expect(diagnostics.loaded == 0u && diagnostics.load_failed == 0u &&
                       diagnostics.rejected == 0u && diagnostics.called == 0u &&
                       diagnostics.hook_failed == 0u && diagnostics.skipped == 0u &&
                       diagnostics.open_rejected == 0u &&
                       diagnostics.stage_unavailable == 0u,
               "every counter stays zero");
        expect(host.records().empty(), "no records");
        expect(fake::g_open_count == 0 && fake::g_close_count == 0,
               "no loader or unloader activity");
        std::puts("plugin_host_test: no_plugin_section_is_a_no_op ok");
    }

} // namespace

int main() {
    /* The loader resolves relative module_path values against
     * <GHOSTLOCK_HOME>/countermeasures; the injected operations never touch the
     * filesystem, so the value only has to be non-empty. */
    expect(setenv("GHOSTLOCK_HOME", "/glk-plugin-host-test", 1) == 0,
           "GHOSTLOCK_HOME is set for the test");

    test_registration_does_not_load();
    test_open_rejected_when_waiter_alive();
    test_dispatch_before_open_is_skipped();
    test_dispatch_after_close_is_skipped();
    test_close_unloads_in_reverse_order();
    test_stage_order_and_once_per_stage();
    test_hook_failure_isolates_the_module();
    test_stage_matrix_rejects_hooks_and_plugins();
    test_load_failure_is_fail_soft();
    test_no_plugin_section_is_a_no_op();

    std::puts("plugin_host_test: ok");
    return 0;
}
