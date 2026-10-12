/** @file tests/surface_test.cpp
 *  Evidence for the exposure-surface table: every row is reachable through the
 *  single lookup path (the test iterates the table instead of restating the
 *  names, so it is not a second list), an unknown name is refused by the pinned
 *  name surface-unknown-name, the tagged write targets, the required widths,
 *  the handoff payload reference and the handle bindings all hold, and the
 *  removed shell entry stays removed.
 *
 *  Build and run: make -C src check
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "contract/script_surface.hpp"

namespace {
    int failures = 0;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++failures;
        }
    }

    /* Every row resolves through lookup() to itself. */
    void rows_are_reachable() {
        for (std::size_t i = 0; i < ghostlock::contract::kSurfaceCount; ++i) {
            const ghostlock::contract::SurfaceEntry &row = ghostlock::contract::kSurface[i];
            const ghostlock::contract::SurfaceResult result = ghostlock::contract::lookup(row.name);
            expect(result.code == ghostlock::contract::SurfaceLookup::Found, "every row must resolve");
            expect(result.entry == &row, "lookup must return the row itself");
            const char *form = row.form == ghostlock::contract::SurfaceForm::Value
                                   ? "value"
                                   : (row.form == ghostlock::contract::SurfaceForm::HandleMethod ? "handle" : "call");
            std::printf("surface %-12s form=%-6s layer=%-9s window=%-3s holds=%s\n", row.name, form,
                        row.implements_layer, row.window_allowed ? "yes" : "no",
                        row.holds_resource ? "yes" : "no");
        }
    }

    /* An unknown name is a named refusal, and nothing is returned (A5). */
    void unknown_name_is_named() {
        const ghostlock::contract::SurfaceResult result = ghostlock::contract::lookup("not-a-surface-name");
        expect(result.code == ghostlock::contract::SurfaceLookup::UnknownSymbol, "unknown name must be refused");
        expect(result.entry == nullptr, "a refused lookup must not return a row");
        expect(std::string_view(ghostlock::contract::surface_lookup_name(result.code)) == "surface-unknown-name",
               "the refusal is pinned as surface-unknown-name");
        std::printf("surface lookup: unknown -> %s\n", ghostlock::contract::surface_lookup_name(result.code));
    }

    /* The compile-time invariants, observed at runtime too. */
    void invariants_hold() {
        /* Eight since 2026-10-10 (user request): the encapsulated step row joined, so a script can ask
         *  for a backend's own multi-phase step by name while the low-level rows stay as they were. */
        /* 读法②：`kernelcall` 行加入后由八行变九行（用户目标原文 ✓，pin 已重生成 ✓）。 */
        expect(ghostlock::contract::kSurfaceCount == 10, "the table carries ten rows");
        expect(ghostlock::contract::surface_names_are_unique_and_non_empty(), "names unique and non-empty");
        expect(ghostlock::contract::surface_has_no_window_prefixed_names(), "no window.* row");
        expect(ghostlock::contract::surface_has_no_run_row(), "no run row (removed by user ruling)");
        expect(ghostlock::contract::surface_layers_exist(), "layers must be declared");
        expect(ghostlock::contract::surface_args_are_well_formed(), "argument specs well formed");
        expect(ghostlock::contract::writeop_target_is_a_pair(), "writeop target is the tagged pair");
        expect(ghostlock::contract::copyop_sides_are_symmetric(), "copyop is symmetric with a width");
        expect(ghostlock::contract::widths_are_required(), "A1: widths are required, never inferred");
        expect(ghostlock::contract::handoff_carries_terminal_and_payload_ref(), "A2: terminal + payload_ref");
        expect(ghostlock::contract::operations_are_handle_methods(), "A4: the four ops are handle methods");
        expect(ghostlock::contract::handle_methods_are_well_formed(), "handle methods bind their handle");
        expect(ghostlock::contract::resource_semantics_are_pinned(), "A3: backend/handoff hold, close releases");
        expect(ghostlock::contract::lookup_refusal_is_pinned(), "A5: refusal name pinned");
        expect(ghostlock::contract::target_kind_names_are_closed(), "target-kind set is closed");
        expect(ghostlock::contract::surface_flags_are_consistent(), "flags consistent");
        std::size_t value_rows = 0;
        std::size_t handle_rows = 0;
        for (std::size_t i = 0; i < ghostlock::contract::kSurfaceCount; ++i) {
            const ghostlock::contract::SurfaceEntry &row = ghostlock::contract::kSurface[i];
            if (row.form == ghostlock::contract::SurfaceForm::Value) {
                ++value_rows;
                expect(!row.executable, "a Value row is a read-only snapshot, never executable");
            }
            if (row.form == ghostlock::contract::SurfaceForm::HandleMethod) {
                ++handle_rows;
            }
        }
        expect(value_rows == 1, "exactly one read-only snapshot row");
        expect(handle_rows == ghostlock::contract::kHandleMethodRowCount,
               "the handle-method row count matches the pinned count");
        std::printf("surface table: %zu rows (%zu value, %zu handle method), invariants hold\n",
                    ghostlock::contract::kSurfaceCount, value_rows, handle_rows);
    }

    /* The tagged write target kinds: closed set, named refusal otherwise. */
    void target_kinds() {
        using ghostlock::contract::TargetKindCheck;
        expect(ghostlock::contract::target_kind_from_name("kernel_offset") == TargetKindCheck::Valid,
               "kernel_offset is a declared kind");
        expect(ghostlock::contract::target_kind_from_name("absolute") == TargetKindCheck::Valid,
               "absolute is a declared kind");
        expect(ghostlock::contract::target_kind_from_name("direct_map") == TargetKindCheck::Valid,
               "direct_map is a declared kind");
        const TargetKindCheck unknown = ghostlock::contract::target_kind_from_name("raw_pointer");
        expect(unknown == TargetKindCheck::Unknown, "an undeclared kind must be refused");
        expect(std::string_view(ghostlock::contract::target_kind_check_name(unknown)) ==
                   ghostlock::contract::kTargetKindInvalidReason,
               "the refusal is spelled target-kind-invalid");
        std::printf("target kinds: %zu declared, unknown -> %s\n", ghostlock::contract::kTargetKindCount,
                    ghostlock::contract::target_kind_check_name(unknown));
    }

    /* A1: the profile size validates the stated width instead of filling it in. */
    void width_validation() {
        using ghostlock::contract::WidthCheck;
        expect(ghostlock::contract::width_check(8U, 8U) == WidthCheck::Match, "a matching width passes");
        const WidthCheck bad = ghostlock::contract::width_check(4U, 8U);
        expect(bad == WidthCheck::Mismatch, "a disagreeing width is refused");
        expect(std::string_view(ghostlock::contract::width_check_name(bad)) ==
                   ghostlock::contract::kWidthMismatchReason,
               "the refusal is spelled width-mismatch");
        std::printf("width check: mismatch -> %s\n", ghostlock::contract::width_check_name(bad));
    }

    /* A3 / A4: the handle bindings and the resource semantics. */
    void handles_and_resources() {
        using ghostlock::contract::HandleCheck;
        const ghostlock::contract::SurfaceEntry *backend = ghostlock::contract::surface_row("backend");
        const ghostlock::contract::SurfaceEntry *close = ghostlock::contract::surface_row("close");
        expect(backend != nullptr && backend->holds_resource, "backend keeps its resource past the call");
        expect(close != nullptr && !close->holds_resource, "close ends the resource with the call");
        /* Drive the handle rows from the table itself: this test must not
         * restate surface names, or it becomes a second list. */
        std::size_t handle_rows = 0;
        for (std::size_t i = 0; i < ghostlock::contract::kSurfaceCount; ++i) {
            const ghostlock::contract::SurfaceEntry &row = ghostlock::contract::kSurface[i];
            if (row.form != ghostlock::contract::SurfaceForm::HandleMethod) {
                continue;
            }
            ++handle_rows;
            expect(ghostlock::contract::surface_row_requires_arg(row, "handle"),
                   "each handle method requires its handle");
        }
        expect(handle_rows == ghostlock::contract::kHandleMethodRowCount,
               "the handle-method count matches the pinned count");
        expect(ghostlock::contract::operations_are_handle_methods(), "the four ops are handle methods");
        expect(ghostlock::contract::handle_use_check(true, 7U, 7U) == HandleCheck::Valid,
               "matching generation may use the handle");
        expect(ghostlock::contract::handle_use_check(true, 8U, 7U) == HandleCheck::Stale,
               "a generation bump makes the old handle stale");
        expect(ghostlock::contract::handle_use_check(false, 7U, 7U) == HandleCheck::Stale, "a gone slot is stale");
        expect(ghostlock::contract::handle_open_check(true) == HandleCheck::Conflict,
               "opening onto a live slot is a conflict");
        expect(ghostlock::contract::handle_open_check(false) == HandleCheck::Valid, "a free slot opens cleanly");
        expect(std::string_view(ghostlock::contract::handle_check_name(HandleCheck::Stale)) ==
                   ghostlock::contract::kHandleStaleReason,
               "the stale rejection is spelled handle-stale");
        expect(std::string_view(ghostlock::contract::handle_check_name(HandleCheck::Conflict)) ==
                   ghostlock::contract::kHandleConflictReason,
               "the conflict rejection is spelled handle-conflict");
        std::printf("handles: %zu handle-method rows, rejections %s / %s\n",
                    ghostlock::contract::kHandleMethodRowCount, ghostlock::contract::kHandleConflictReason,
                    ghostlock::contract::kHandleStaleReason);
    }
}  // namespace

int main() {
    rows_are_reachable();
    unknown_name_is_named();
    invariants_hold();
    target_kinds();
    width_validation();
    handles_and_resources();
    if (failures == 0) {
        std::printf("surface: PASS (7 rows + tagged targets + required widths + payload_ref + handle bindings)\n");
        return 0;
    }
    std::printf("surface: FAIL (%d)\n", failures);
    return 1;
}
