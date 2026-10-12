/** @file tests/procedure_load_test.cpp
 *  The procedure loader: the contract's load-time checks, and the named refusals it produces.
 *
 *  判据四条：① 合规的 procedure 被装载并执行 run；② 缺 name/入口 ⇒ procedure-missing；
 *  ③ 表里出现契约外的成员 ⇒ procedure-member-unknown；④ 不返回任何东西的裸 chunk ⇒ 照旧接受（fixtures 的路径）。
 *  拒绝名由 sink 的 on_error 记录，正是运行时看到的那一个。
 */
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include "script/call_sink.hpp"
#include "script/sandbox.hpp"

using namespace ghostlock;

namespace {
    std::size_t g_failures = 0U;

    void expect(bool ok, const char *what) {
        if (!ok) { std::printf("FAIL: %s\n", what); ++g_failures; }
    }

    /** What the sink saw: the last named refusal and how many surface calls arrived. */
    struct Seen final {
        const char *refusal = nullptr;
        std::size_t calls = 0U;
    };

    void on_op(void *context, const script::CallView *view) noexcept {
        auto *seen = static_cast<Seen *>(context);
        if (seen != nullptr && view != nullptr) { ++seen->calls; }
    }

    void on_error(void *context, const char *named_code) noexcept {
        auto *seen = static_cast<Seen *>(context);
        if (seen != nullptr && seen->refusal == nullptr) { seen->refusal = named_code; }
    }

    script::ScriptBudget budget() noexcept {
        script::ScriptBudget b{};
        b.max_instructions = 200000U;
        b.max_bytes = 4194304U;
        b.max_stack_depth = 32U;
        b.max_script_bytes = 65536U;
        return b;
    }

    script::ScriptRejection load(std::string_view script, Seen &seen, script::Sandbox &sandbox) noexcept {
        script::CallSink sink{};
        sink.context = &seen;
        sink.on_op = &on_op;
        sink.on_error = &on_error;
        if (!sandbox.open(budget(), sink)) { return script::ScriptRejection::TuningMissing; }
        const script::ScriptRejection rejection = sandbox.run_procedure(script);
        sandbox.close();
        return rejection;
    }

    void a_valid_procedure_is_loaded_and_run() {
        constexpr std::string_view kValid =
                "local p = { name = \"p\", run = function(ctx) surface.backend(\"b\") end } return p";
        Seen seen{};
        script::Sandbox sandbox{};
        const auto rejection = load(kValid, seen, sandbox);
        expect(rejection == script::ScriptRejection::None, "a valid procedure runs");
        expect(seen.calls == 1U, "its entry point reached the surface");
        expect(seen.refusal == nullptr, "and nothing was refused");
    }

    void a_table_without_an_entry_is_refused_by_name() {
        constexpr std::string_view kNoEntry = "return { name = \"p\" }";
        Seen seen{};
        script::Sandbox sandbox{};
        const auto rejection = load(kNoEntry, seen, sandbox);
        expect(rejection == script::ScriptRejection::ProcedureMalformed, "a table without run is malformed");
        expect(seen.refusal != nullptr && std::strcmp(seen.refusal, "procedure-missing") == 0,
               "the refusal names the missing entry");
    }

    void a_declarative_only_file_says_which_kind_of_missing_it_is() {
        constexpr std::string_view kDeclarative =
                "local p = { name = \"p\", steps = { \"w1\" } } return p";
        Seen seen{};
        script::Sandbox sandbox{};
        const auto rejection = load(kDeclarative, seen, sandbox);
        expect(rejection == script::ScriptRejection::ProcedureMalformed, "a steps-only file is malformed");
        expect(seen.refusal != nullptr &&
                       std::strcmp(seen.refusal, "procedure-steps-not-runnable") == 0,
               "and its refusal says the declarative form has no entry");
    }

    void a_member_outside_the_contract_is_refused_by_name() {
        constexpr std::string_view kExtra =
                "return { name = \"p\", run = function(ctx) end, payload = 1 }";
        Seen seen{};
        script::Sandbox sandbox{};
        const auto rejection = load(kExtra, seen, sandbox);
        expect(rejection == script::ScriptRejection::ProcedureMalformed, "an unknown member is malformed");
        expect(seen.refusal != nullptr && std::strcmp(seen.refusal, "procedure-member-unknown") == 0,
               "the refusal names the unknown member");
    }

    void a_bare_chunk_still_runs() {
        constexpr std::string_view kBare = "surface.backend(\"b\")";
        Seen seen{};
        script::Sandbox sandbox{};
        const auto rejection = load(kBare, seen, sandbox);
        expect(rejection == script::ScriptRejection::None, "a bare chunk is accepted");
        expect(seen.calls == 1U, "and its call arrived");
    }
}  // namespace

int main() {
    std::printf("procedure: the contract");
    a_valid_procedure_is_loaded_and_run();
    a_bare_chunk_still_runs();
    std::printf("procedure: the named refusals");
    a_table_without_an_entry_is_refused_by_name();
    a_member_outside_the_contract_is_refused_by_name();
    a_declarative_only_file_says_which_kind_of_missing_it_is();
    if (g_failures != 0U) {
        std::printf("procedure: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("procedure: PASS (loaded, run, and refused by name)\n");
    return 0;
}
