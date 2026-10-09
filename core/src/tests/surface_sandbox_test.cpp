/** @file tests/surface_sandbox_test.cpp
 *  A6-1 evidence for the Lua sandbox. Four named assertions, each falsifiable:
 *
 *   lua_globals_are_the_whitelist()      set equality between the runtime globals
 *                                        table and kept(15) + budgeted(1)
 *   forbidden_globals_are_absent()       load/loadfile/dofile/print/warn/
 *                                        setmetatable/getmetatable/io/os/package/
 *                                        debug/coroutine/require are all nil
 *   surface_registration_follows_the_table()  the runtime surface table's keys
 *                                        equal contract::kSurface[] (it is built
 *                                        by iterating that table)
 *   budgets_are_enforced()               instruction, memory, stack-depth and
 *                                        size breaches each give budget-exceeded
 *
 *  Plus the cfg read-only guarantee and the surface-unknown-name refusal.
 *
 *  Build and run: make -C src check
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include "contract/script_surface.hpp"
#include "script/lua_whitelist.hpp"
#include "script/sandbox.hpp"

namespace {
    int failures = 0;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++failures;
        }
    }

    /* The expectations: kept + budgeted, and the names that must not exist. */
    constexpr const char *kExpectedWrite[] = {"load",        "loadfile",      "dofile",     "print",
                                              "warn",        "setmetatable",  "getmetatable", "io",
                                              "os",          "package",       "debug",      "coroutine",
                                              "require"};
    constexpr std::size_t kExpectedWriteCount = sizeof(kExpectedWrite) / sizeof(kExpectedWrite[0]);

    /* Recording sink: function-pointer table, no vtable. */
    struct Recorder final {
        std::size_t calls = 0;
        std::size_t errors = 0;
        std::size_t last_surface_index = 0;
        std::size_t last_arg_count = 0;
        std::uint64_t last_u64 = 0;
        char last_error[64] = {};
    };

    void sink_on_op(void *context, const ghostlock::script::CallView *view) {
        auto *recorder = static_cast<Recorder *>(context);
        ++recorder->calls;
        recorder->last_surface_index = view->surface_index;
        recorder->last_arg_count = view->arg_count;
        for (std::size_t i = 0; i < view->arg_count; ++i) {
            if (view->args[i].kind == ghostlock::script::ArgKind::U64) {
                recorder->last_u64 = view->args[i].u64;
            }
        }
    }

    void sink_on_error(void *context, const char *named_code) {
        auto *recorder = static_cast<Recorder *>(context);
        ++recorder->errors;
        std::snprintf(recorder->last_error, sizeof(recorder->last_error), "%s", named_code);
    }

    struct Fixture final {
        Recorder recorder{};
        ghostlock::script::Sandbox *sandbox = nullptr;

        bool open() {
            ghostlock::script::ScriptBudget budget{};
            budget.max_instructions = 200000U;
            budget.max_bytes = 262144U;
            budget.max_stack_depth = 40U;
            budget.max_script_bytes = 32768U;
            ghostlock::script::CallSink sink{};
            sink.context = &recorder;
            sink.on_op = sink_on_op;
            sink.on_error = sink_on_error;
            return sandbox->open(budget, sink);
        }
    };

    /* ---- assertion 1: the globals table is exactly the whitelist ---- */
    void lua_globals_are_the_whitelist(ghostlock::script::Sandbox &sandbox) {
        /* The runtime globals = the Lua whitelist classes + the globals the host
         * injects (surface, cfg). Both halves are named by their own authority: no
         * name is hand-listed here. */
        constexpr std::size_t kKept = ghostlock::script::kKeptGlobalCount;
        constexpr std::size_t kBudgeted = ghostlock::script::kBudgetedGlobalCount;
        constexpr std::size_t kHost = ghostlock::script::kHostGlobalCount;
        const char *expected[kKept + kBudgeted + kHost];
        std::size_t count = 0;
        for (std::size_t i = 0; i < kKept; ++i) {
            expected[count++] = ghostlock::script::kKeptGlobalNames[i];
        }
        for (std::size_t i = 0; i < kBudgeted; ++i) {
            expected[count++] = ghostlock::script::kBudgetedGlobalNames[i];
        }
        for (std::size_t i = 0; i < kHost; ++i) {
            expected[count++] = ghostlock::script::kHostGlobalNames[i];
        }
        expect(sandbox.registered_global_count() == kKept + kBudgeted,
               "the Lua registrations are exactly the two whitelist classes");
        expect(sandbox.table_keys_match(nullptr, expected, count),
               "the runtime globals table is EXACTLY the whitelist (set equality)");
        std::printf("sandbox globals: %zu kept + %zu budgeted + %zu host-injected, set equality: %s\n",
                    kKept, kBudgeted, kHost, sandbox.table_keys_match(nullptr, expected, count) ? "yes" : "no");
    }

    /* ---- assertion 2: the forbidden names do not exist ---- */
    void forbidden_globals_are_absent(ghostlock::script::Sandbox &sandbox) {
        for (std::size_t i = 0; i < kExpectedWriteCount; ++i) {
            const bool absent = sandbox.global_absent(kExpectedWrite[i]);
            if (!absent) {
                std::printf("FAIL: forbidden global present: %s\n", kExpectedWrite[i]);
                ++failures;
            }
        }
        std::printf("sandbox forbidden globals: %zu names checked, all absent\n", kExpectedWriteCount);
    }

    /* ---- assertion 3: surface registration is driven by kSurface ---- */
    void surface_registration_follows_the_table(ghostlock::script::Sandbox &sandbox) {
        const char *names[ghostlock::contract::kSurfaceCount];
        for (std::size_t i = 0; i < ghostlock::contract::kSurfaceCount; ++i) {
            names[i] = ghostlock::contract::kSurface[i].name;
        }
        expect(sandbox.registered_surface_count() == ghostlock::contract::kSurfaceCount,
               "the surface registration count equals the table");
        expect(sandbox.table_keys_match("surface", names, ghostlock::contract::kSurfaceCount),
               "the runtime surface table equals kSurface[] (set equality, table-driven)");
        std::printf("sandbox surface: %zu/%zu rows registered from kSurface[]\n",
                    sandbox.registered_surface_count(), ghostlock::contract::kSurfaceCount);
    }

    /* ---- assertion 4: every budget refuses by name ---- */
    void budgets_are_enforced(ghostlock::script::Sandbox &sandbox) {
        using ghostlock::script::ScriptRejection;
        const ScriptRejection spin = sandbox.run("local n = 0 while true do n = n + 1 end");
        expect(spin == ScriptRejection::BudgetExceeded, "an endless loop must exceed the instruction budget");
        const ScriptRejection memory = sandbox.run(
            "local t = {} local i = 0 while i < 100000 do t[i] = 'aaaaaaaaaaaaaaaa' i = i + 1 end");
        expect(memory == ScriptRejection::BudgetExceeded, "a memory hog must exceed the memory budget");
        const ScriptRejection depth = sandbox.run(
            "local function f(n) return f(n + 1) end local ok = pcall(f, 1)");
        expect(depth == ScriptRejection::BudgetExceeded, "deep recursion must exceed the stack-depth budget");
        const std::string oversized(70000U, 'x');
        const ScriptRejection size = sandbox.run(std::string_view(oversized));
        expect(size == ScriptRejection::BudgetExceeded, "a script over the size ceiling must be refused");
        const ScriptRejection syntax = sandbox.run("local = = =");
        expect(syntax == ScriptRejection::PlanSyntax, "a syntax error is refused as plan-syntax");
        std::printf("sandbox budgets: instruction=%s memory=%s depth=%s size=%s syntax=%s\n",
                    ghostlock::script::script_rejection_name(spin),
                    ghostlock::script::script_rejection_name(memory),
                    ghostlock::script::script_rejection_name(depth),
                    ghostlock::script::script_rejection_name(size),
                    ghostlock::script::script_rejection_name(syntax));
    }

    /* ---- cfg is readable but not writable; surface names outside the table refuse ---- */
    void cfg_and_unknown_names(Fixture &fixture, ghostlock::script::Sandbox &sandbox) {
        using ghostlock::script::CfgField;
        using ghostlock::script::ScriptRejection;
        const CfgField fields[] = {{"probe", 4242U}};
        expect(sandbox.set_cfg_snapshot(fields, 1), "the cfg snapshot installs");
        const ScriptRejection write = sandbox.run("cfg.probe = 1");
        expect(write == ScriptRejection::CfgReadOnly, "writing cfg must be refused as cfg-read-only");
        const std::size_t before = fixture.recorder.calls;
        const ScriptRejection read = sandbox.run("surface.backend(cfg.probe)");
        expect(read == ScriptRejection::None, "reading cfg and calling a surface row must work");
        expect(fixture.recorder.calls == before + 1, "the call reached the sink exactly once");
        expect(fixture.recorder.last_u64 == 4242U, "the forwarded argument carries the cfg value");
        const ScriptRejection unknown = sandbox.run("surface.not_a_row()");
        expect(unknown == ScriptRejection::SurfaceUnknownName, "a name outside the table refuses by name");
        expect(std::strcmp(fixture.recorder.last_error, "surface-unknown-name") == 0,
               "the sink sees surface-unknown-name");
        std::printf("sandbox cfg: read-only enforced (%s), value forwarded=%llu, unknown name=%s\n",
                    ghostlock::script::script_rejection_name(write),
                    static_cast<unsigned long long>(fixture.recorder.last_u64),
                    ghostlock::script::script_rejection_name(unknown));
    }
}  // namespace

int main() {
    ghostlock::script::Sandbox sandbox;
    Fixture fixture{};
    fixture.sandbox = &sandbox;
    if (!fixture.open()) {
        std::printf("FAIL: the sandbox did not open\n");
        return 1;
    }
    expect(ghostlock::script::kSandboxPhase == ghostlock::contract::SurfacePhase::Stage,
           "the sandbox is stage-only by construction");
    /* Inject the cfg snapshot BEFORE the globals-set assertion: the host-injected
     * globals (surface, cfg) are part of the expected runtime set. */
    {
        const ghostlock::script::CfgField fields[] = {{"probe", 4242U}};
        expect(sandbox.set_cfg_snapshot(fields, 1), "the cfg snapshot installs");
    }
    lua_globals_are_the_whitelist(sandbox);
    forbidden_globals_are_absent(sandbox);
    surface_registration_follows_the_table(sandbox);
    budgets_are_enforced(sandbox);
    cfg_and_unknown_names(fixture, sandbox);
    sandbox.close();
    if (failures == 0) {
        std::printf("sandbox: PASS (whitelist set equality + forbidden absent + table-driven surface + four budgets)\n");
        return 0;
    }
    std::printf("sandbox: FAIL (%d)\n", failures);
    return 1;
}
