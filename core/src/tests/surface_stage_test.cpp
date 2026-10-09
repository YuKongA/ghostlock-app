/** @file tests/surface_stage_test.cpp
 *  A6-2 evidence for the staged plan: one script run records calls, the
 *  recording is deterministic and bounded, and a refusal is named.
 *
 *  Row names are never restated here: every call is built from
 *  contract::kSurface, so this file cannot become a second copy of the table
 *  (the check that enforces a single definition site looks for exactly that).
 *
 *  Sections: 1. a real run records every call in order; 2. two runs serialize
 *  byte for byte identically; 3. the entry bound and the string arena refuse by
 *  name rather than truncating; 4. a sandbox refusal reaches the plan by name.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "contract/script_surface.hpp"
#include "pipeline/plan_gate.hpp"
#include "script/sandbox.hpp"
#include "script/stage_gate.hpp"
#include "script/staged_plan.hpp"

namespace {
    using ghostlock::contract::kSurface;
    using ghostlock::contract::kSurfaceCount;
    using ghostlock::contract::SurfaceForm;
    using ghostlock::script::ArgKind;
    using ghostlock::script::ArgView;
    using ghostlock::script::CallView;
    using ghostlock::script::kMaxStagedEntries;
    using ghostlock::script::Sandbox;
    using ghostlock::script::ScriptBudget;
    using ghostlock::script::ScriptRejection;
    using ghostlock::script::StagedPlan;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** The row index with this shape; kSurfaceCount when the table has none. */
    std::size_t row_for(SurfaceForm form, std::size_t arg_count) {
        for (std::size_t i = 0U; i < kSurfaceCount; ++i) {
            if (kSurface[i].form == form && kSurface[i].arg_count == arg_count) {
                return i;
            }
        }
        return kSurfaceCount;
    }

    ScriptBudget budget() {
        ScriptBudget b{};
        b.max_instructions = 2000000U;
        b.max_bytes = 1U << 20U;
        b.max_stack_depth = 32U;
        b.max_script_bytes = 65536U;
        return b;
    }

    std::size_t append_head(char *out, std::size_t used, std::size_t row) {
        const std::string_view prefix("surface.");
        for (const char c : prefix) {
            out[used++] = c;
        }
        const std::string_view name(kSurface[row].name);
        for (const char c : name) {
            out[used++] = c;
        }
        out[used++] = '(';
        return used;
    }

    std::size_t append_number(char *out, std::size_t used, std::uint64_t value) {
        char digits[24];
        std::size_t n = 0U;
        do {
            digits[n++] = static_cast<char>('0' + (value % 10U));
            value /= 10U;
        } while (value != 0U);
        while (n > 0U) {
            out[used++] = digits[--n];
        }
        return used;
    }

    std::size_t append_quoted(char *out, std::size_t used, std::string_view text) {
        out[used++] = '"';
        for (const char c : text) {
            out[used++] = c;
        }
        out[used++] = '"';
        return used;
    }

    std::size_t append_text(char *out, std::size_t used, std::string_view text) {
        for (const char c : text) {
            out[used++] = c;
        }
        return used;
    }

    std::size_t append_comma(char *out, std::size_t used, std::size_t index) {
        if (index > 0U) {
            out[used++] = ',';
        }
        return used;
    }

    /* ---- 1: a real run records what the script called ---- */
    void records_a_run() {
        const std::size_t row_plain = row_for(SurfaceForm::Call, 0U);
        const std::size_t row_word = row_for(SurfaceForm::HandleMethod, 5U);
        const std::size_t row_wait = row_for(SurfaceForm::HandleMethod, 2U);
        const std::size_t row_hand = row_for(SurfaceForm::HandleMethod, 3U);
        const std::size_t row_free = row_for(SurfaceForm::HandleMethod, 1U);
        expect(row_plain < kSurfaceCount && row_word < kSurfaceCount &&
                   row_wait < kSurfaceCount && row_hand < kSurfaceCount &&
                   row_free < kSurfaceCount,
               "the table declares the five shapes this test drives");

        char script[512];
        std::size_t used = append_text(script, 0U, std::string_view("local h = "));
        used = append_head(script, used, row_plain);
        used = append_text(script, used, std::string_view("cve_2026_43499)\n"));
        used = append_head(script, used, row_word);
        used = append_text(script, used, std::string_view("h"));
        used = append_comma(script, used, 1U); used = append_number(script, used, 2U);
        used = append_comma(script, used, 2U); used = append_number(script, used, 96U);
        used = append_comma(script, used, 3U); used = append_number(script, used, 7U);
        used = append_comma(script, used, 4U); used = append_number(script, used, 8U);
        used = append_text(script, used, std::string_view(")\n"));
        used = append_head(script, used, row_wait);
        used = append_text(script, used, std::string_view("h"));
        used = append_comma(script, used, 1U); used = append_number(script, used, 1000U);
        used = append_text(script, used, std::string_view(")\n"));
        used = append_head(script, used, row_hand);
        used = append_text(script, used, std::string_view("h"));
        used = append_comma(script, used, 1U); used = append_quoted(script, used, std::string_view("root_child"));
        used = append_comma(script, used, 2U); used = append_quoted(script, used, std::string_view("root.handoff"));
        used = append_text(script, used, std::string_view(")\n"));
        used = append_head(script, used, row_free);
        used = append_text(script, used, std::string_view("h)\n"));

        StagedPlan plan;
        Sandbox sandbox;
        expect(sandbox.open(budget(), ghostlock::script::staged_sink(plan)),
               "the sandbox opens with a staged sink");
        const ScriptRejection verdict = sandbox.run(std::string_view(script, used));
        expect(verdict == ScriptRejection::None, "the script runs inside the budgets");
        expect(plan.ok(), "no refusal was recorded");
        expect(plan.size() == 5U, "five calls were recorded");
        expect(plan.at(0U).surface_index == row_plain, "entry one is the no-argument row");
        expect(plan.at(1U).surface_index == row_word, "entry two is the five-argument row");
        expect(plan.at(3U).surface_index == row_hand, "entry four is the three-argument row");
        expect(plan.at(4U).surface_index == row_free, "entry five is the one-argument row");
        expect(plan.at(1U).arg_count == 5U, "the five-argument call recorded five arguments");
        expect(plan.at(1U).args[2U].kind == ArgKind::U64 && plan.at(1U).args[2U].u64 == 96U,
               "a numeric argument survives verbatim");
        expect(plan.at(3U).arg_count == 3U, "the three-argument call recorded three");
        expect(plan.string_of(plan.at(3U).args[1U]) == std::string_view("root_child"),
               "a string argument survives verbatim");
        expect(plan.string_of(plan.at(3U).args[2U]) == std::string_view("root.handoff"),
               "the payload reference survives verbatim");
        sandbox.close();
    }

    /* ---- 2: the same script stages the same bytes twice ---- */
    void stages_are_deterministic() {
        static std::uint8_t first[8192];
        static std::uint8_t second[8192];
        const std::size_t row_word = row_for(SurfaceForm::HandleMethod, 5U);
        char script[256];
        std::size_t used = append_text(script, 0U, std::string_view("local h = "));
        used = append_head(script, used, row_for(SurfaceForm::Call, 0U));
        used = append_text(script, used, std::string_view("cve_2026_43499)\n"));
        used = append_head(script, used, row_word);
        used = append_text(script, used, std::string_view("h"));
        used = append_comma(script, used, 1U); used = append_number(script, used, 2U);
        used = append_comma(script, used, 2U); used = append_number(script, used, 96U);
        used = append_comma(script, used, 3U); used = append_number(script, used, 7U);
        used = append_comma(script, used, 4U); used = append_number(script, used, 8U);
        script[used++] = ')'; script[used++] = '\n';
        const std::string_view text(script, used);

        StagedPlan a;
        StagedPlan b;
        Sandbox one;
        expect(one.open(budget(), ghostlock::script::staged_sink(a)), "open for run one");
        expect(one.run(text) == ScriptRejection::None, "run one succeeds");
        one.close();
        Sandbox two;
        expect(two.open(budget(), ghostlock::script::staged_sink(b)), "open for run two");
        expect(two.run(text) == ScriptRejection::None, "run two succeeds");
        two.close();
        expect(a.size() == 2U && b.size() == 2U, "both runs recorded the same two calls");
        const std::size_t na = a.serialize(first, sizeof(first));
        const std::size_t nb = b.serialize(second, sizeof(second));
        expect(na != 0U && na == nb, "both runs serialize to the same length");
        bool same = na == nb;
        for (std::size_t i = 0U; same && i < na; ++i) {
            same = first[i] == second[i];
        }
        expect(same, "two stages of one script are byte-identical");
    }

    /* ---- 3: bounds refuse by name ---- */
    void bounds_refuse_by_name() {
        const std::size_t row_wait = row_for(SurfaceForm::HandleMethod, 2U);
        char script[4096];
        std::size_t used = 0U;
        used = append_text(script, used, std::string_view("local h = "));
        used = append_head(script, used, row_for(SurfaceForm::Call, 0U));
        used = append_text(script, used, std::string_view("cve_2026_43499)\n"));
        for (std::size_t i = 0U; i < 70U; ++i) {
            used = append_head(script, used, row_wait);
            used = append_text(script, used, std::string_view("h"));
            used = append_comma(script, used, 1U);
            used = append_number(script, used, i + 1U);
            used = append_text(script, used, std::string_view(")\n"));
        }
        StagedPlan plan;
        Sandbox sandbox;
        expect(sandbox.open(budget(), ghostlock::script::staged_sink(plan)), "open for bounds");
        const ScriptRejection verdict = sandbox.run(std::string_view(script, used));
        expect(verdict == ScriptRejection::None, "the script itself completes");
        expect(plan.size() == kMaxStagedEntries, "recording stops exactly at the entry bound");
        expect(std::string_view(plan.refusal_name()) == std::string_view("budget-exceeded"),
               "exceeding the entry bound is refused by name");
        sandbox.close();

        /* The arena bound belongs to the container, so it is exercised directly:

         * the sandbox refuses an oversized argument earlier, by the ceiling the

         * row itself declares, which is a different guard. */

        StagedPlan arena_plan;

        static char big[6000];

        for (std::size_t i = 0U; i < sizeof(big); ++i) {

            big[i] = 'x';

        }

        ArgView arg{};

        arg.kind = ArgKind::Str;

        arg.str = std::string_view(big, sizeof(big));

        CallView view{};

        view.surface_index = row_for(SurfaceForm::HandleMethod, 3U);

        view.form = SurfaceForm::HandleMethod;

        view.name = kSurface[view.surface_index].name;

        view.args = &arg;

        view.arg_count = 1U;

        expect(!arena_plan.record(view), "a string past the arena is refused");

        expect(std::string_view(arena_plan.refusal_name()) == std::string_view("budget-exceeded"),

               "exceeding the string arena is refused by name");

        expect(arena_plan.size() == 0U, "nothing is recorded when a bound is hit");

    }

    /* ---- 5: the recording is checked against the table it came from ---- */
    void surface_entries_are_checked() {
        const std::size_t row_word = row_for(SurfaceForm::HandleMethod, 5U);
        char script[256];
        std::size_t used = append_text(script, 0U, std::string_view("local h = "));
        used = append_head(script, used, row_for(SurfaceForm::Call, 0U));
        used = append_text(script, used, std::string_view("cve_2026_43499)\n"));
        used = append_head(script, used, row_word);
        used = append_text(script, used, std::string_view("h"));
        /* Eight arguments in all: inside this container's per-entry ceiling, above
         * the five the row declares, so the recording survives and the check is
         * what refuses it (a ninth would be refused by the container first). */
        for (std::size_t i = 1U; i < 8U; ++i) {
            used = append_comma(script, used, i);
            used = append_number(script, used, i + 1U);
        }
        script[used++] = ')'; script[used++] = '\n';
        StagedPlan plan;
        Sandbox sandbox;
        expect(sandbox.open(budget(), ghostlock::script::staged_sink(plan)), "open for the ceiling");
        expect(sandbox.run(std::string_view(script, used)) == ScriptRejection::None,
               "the over-long call still runs");
        expect(plan.size() == 2U, "the acquisition and the call were recorded");
        const ghostlock::script::StageVerdict verdict =
            ghostlock::script::check_surface_entries(plan);
        expect(!verdict.ok, "an argument count past the row's list is refused");
        expect(std::string_view(verdict.reason) == std::string_view("param-invalid"),
               "the over-long call is refused as param-invalid");
        expect(verdict.index == 1U, "the refusal points at the over-long entry");
        sandbox.close();
    }

    /* ---- 6: the admission gate's own reason is folded back in ---- */
    void gate_reasons_are_folded() {
        using ghostlock::contract::op_token_hash;
        using ghostlock::pipeline::gate_reason_name;
        using ghostlock::pipeline::GateReason;
        using ghostlock::pipeline::plan_gate;
        using ghostlock::pipeline::PlanInput;
        using ghostlock::pipeline::PlanOp;

        PlanOp op{};
        op.token = "not.an.op";
        op.token_hash = op_token_hash(op.token);
        op.route_mask = ghostlock::contract::RouteMask{1U};
        op.params = nullptr;
        op.param_count = 0U;
        op.seam_inside_window = false;
        op.depends_on = -1;
        PlanInput input{};
        input.backend_id = "cve_2026_43499";
        input.ops = &op;
        input.op_count = 1U;
        input.script_present = true;
        input.builtin_plan_present = false;
        input.queue_present = false;
        input.geometry_present = true;
        input.capability_present = true;
        input.executor_available = true;
        const ghostlock::pipeline::GateVerdict verdict = plan_gate(input);
        expect(!verdict.ok, "an unknown op token is refused by the gate");
        expect(verdict.reason == GateReason::UnknownOp, "the reason is the unknown token");

        StagedPlan plan;
        expect(ghostlock::script::fold_refusal(plan, gate_reason_name(verdict.reason)),
               "the gate reason folds into the plan");
        expect(std::string_view(plan.refusal_name()) == std::string_view("unknown-op"),
               "the plan carries the gate's own name");
        expect(!plan.ok(), "a plan refused by the gate does not run");
    }

    /* ---- 7: the snapshot answers by declaration, never with a nil ---- */
    void cfg_snapshot_is_guarded() {
        using ghostlock::script::CfgField;
        StagedPlan plan;
        Sandbox sandbox;
        expect(sandbox.open(budget(), ghostlock::script::staged_sink(plan)), "open for the snapshot");
        const CfgField fields[] = {{"declared", 4242U}};
        expect(sandbox.set_cfg_snapshot(fields, 1U), "the snapshot installs");

        char script[256];
        std::size_t used = append_text(script, 0U, std::string_view("local v = cfg.declared\n"));
        /* The acquiring row takes the backend id; here it carries the cfg value,
         * which is what proves the snapshot reached the recording. */
        used = append_head(script, used, row_for(SurfaceForm::Call, 0U));
        used = append_text(script, used, std::string_view("v)"));
        expect(sandbox.run(std::string_view(script, used)) == ScriptRejection::None,
               "a declared field reads through and the call runs");
        expect(plan.size() == 1U && plan.at(0U).args[0U].u64 == 4242U,
               "the declared value reaches the recording");

        const ScriptRejection missing = sandbox.run("local v = cfg.undeclared");
        expect(missing == ScriptRejection::CfgUnknownField,
               "an undeclared field is refused as cfg-unknown-field");
        expect(std::string_view(plan.refusal_name()) == std::string_view("cfg-unknown-field"),
               "the plan carries the refusal by name");
        sandbox.close();
    }

    /* ---- 8: a script acquires, uses and releases a handle, and no more ---- */
    void handles_flow_through_the_surface() {
        const std::size_t row_acquire = row_for(SurfaceForm::Call, 0U);
        const std::size_t row_word = row_for(SurfaceForm::HandleMethod, 5U);
        const std::size_t row_free = row_for(SurfaceForm::HandleMethod, 1U);
        StagedPlan plan;
        Sandbox sandbox;
        expect(sandbox.open(budget(), ghostlock::script::staged_sink(plan)), "open for handles");

        char script[512];
        std::size_t used = append_text(script, 0U, std::string_view("local h = "));
        used = append_head(script, used, row_acquire);
        used = append_text(script, used, std::string_view("cve_2026_43499)\n"));
        used = append_head(script, used, row_word);
        used = append_text(script, used, std::string_view("h,2,96,7,8)\n"));
        used = append_head(script, used, row_free);
        used = append_text(script, used, std::string_view("h)\n"));
        expect(sandbox.run(std::string_view(script, used)) == ScriptRejection::None,
               "acquire, use and release all run");
        expect(plan.ok() && plan.size() == 3U, "three calls were recorded");
        expect(plan.at(1U).args[0U].kind == ArgKind::Handle,
               "the handed-over argument is a handle, not a number");
        expect(plan.at(1U).args[0U].handle.generation == 1U && plan.at(1U).args[0U].handle.slot == 0U,
               "the recorded handle is the one that was issued");
        /* Acquire, release, then use the value that was released. */
        char reuse[512];
        std::size_t n = append_text(reuse, 0U, std::string_view("local h = "));
        n = append_head(reuse, n, row_acquire);
        n = append_text(reuse, n, std::string_view("cve_2026_43499)\n"));
        n = append_head(reuse, n, row_free);
        n = append_text(reuse, n, std::string_view("h)\n"));
        n = append_head(reuse, n, row_word);
        n = append_text(reuse, n, std::string_view("h,2,96,7,8)\n"));
        expect(sandbox.run(std::string_view(reuse, n)) == ScriptRejection::HandleStale,
               "using a released handle is handle-stale");
        expect(std::string_view(plan.refusal_name()) == std::string_view("handle-stale"),
               "the plan carries handle-stale by name");

        /* Two acquisitions in one run, with no release between them. This runs
         * last on purpose: the conflict aborts before any release, so the first
         * handle stays live and any later script would be refused an acquire. */
        char twice[512];
        std::size_t n2 = append_text(twice, 0U, std::string_view("local a = "));
        n2 = append_head(twice, n2, row_acquire);
        n2 = append_text(twice, n2, std::string_view("cve_2026_43499)\nlocal b = "));
        n2 = append_head(twice, n2, row_acquire);
        n2 = append_text(twice, n2, std::string_view("cve_2026_43499)\n"));
        expect(sandbox.run(std::string_view(twice, n2)) == ScriptRejection::HandleConflict,
               "acquiring again while one is live is handle-conflict");
        sandbox.close();
    }

    /* ---- 4: a sandbox refusal reaches the plan by name ---- */
    void refusals_are_recorded() {
        StagedPlan plan;
        Sandbox sandbox;
        expect(sandbox.open(budget(), ghostlock::script::staged_sink(plan)), "open for refusal");
        const ScriptRejection verdict = sandbox.run("surface.this_row_does_not_exist()");
        expect(verdict != ScriptRejection::None, "an unknown row is refused");
        expect(std::string_view(plan.refusal_name()) == std::string_view("surface-unknown-name"),
               "the refusal reaches the plan by name");
        sandbox.close();
    }
}  // namespace

int main() {
    std::printf("stage: recording one script run\n");
    records_a_run();
    std::printf("stage: determinism\n");
    stages_are_deterministic();
    std::printf("stage: bounds\n");
    bounds_refuse_by_name();
    std::printf("stage: refusal names\n");
    refusals_are_recorded();
    std::printf("stage: surface ceiling\n");
    surface_entries_are_checked();
    std::printf("stage: gate reasons\n");
    gate_reasons_are_folded();
    std::printf("stage: cfg snapshot\n");
    cfg_snapshot_is_guarded();
    std::printf("stage: handles\n");
    handles_flow_through_the_surface();
    if (g_failures != 0U) {
        std::printf("stage: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("stage: PASS (records verbatim + deterministic + bounded + named refusals)\n");
    return 0;
}
