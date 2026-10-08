/** @file tests/surface_test.cpp
 *  Evidence for the exposure-surface table: every row is reachable through the
 *  single lookup path (the test iterates the table instead of restating the
 *  names, so it is not a second list), an unknown name is refused by name
 *  (unknown-symbol), the tagged write targets and the handle method behave as
 *  the latest design requires, and the removed shell entry stays removed.
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
            std::printf("surface %-12s layer=%-9s window=%-3s holds=%s\n", row.name, row.implements_layer,
                        row.window_allowed ? "yes" : "no", row.holds_resource ? "yes" : "no");
        }
    }

    /* An unknown name is a named refusal, and nothing is returned. */
    void unknown_name_is_named() {
        const ghostlock::contract::SurfaceResult result = ghostlock::contract::lookup("not-a-surface-name");
        expect(result.code == ghostlock::contract::SurfaceLookup::UnknownSymbol, "unknown name must be refused");
        expect(result.entry == nullptr, "a refused lookup must not return a row");
        expect(std::string_view(ghostlock::contract::surface_lookup_name(result.code)) == "unknown-symbol",
               "the refusal code must be spelled unknown-symbol");
        std::printf("surface lookup: unknown -> %s\n", ghostlock::contract::surface_lookup_name(result.code));
    }

    /* The compile-time invariants, observed at runtime too. */
    void invariants_hold() {
        expect(ghostlock::contract::kSurfaceCount == 7, "the table carries seven rows");
        expect(ghostlock::contract::surface_names_are_unique_and_non_empty(), "names unique and non-empty");
        expect(ghostlock::contract::surface_has_no_window_prefixed_names(), "no window.* row");
        expect(ghostlock::contract::surface_has_no_run_row(), "no run row (removed by user ruling)");
        expect(ghostlock::contract::surface_layers_exist(), "layers must be declared");
        expect(ghostlock::contract::surface_args_are_well_formed(), "argument specs well formed");
        expect(ghostlock::contract::writeop_target_is_a_pair(), "writeop target is the tagged pair");
        expect(ghostlock::contract::copyop_sides_are_symmetric(), "copyop is symmetric with a width");
        expect(ghostlock::contract::handle_methods_are_well_formed(), "handle methods take a handle");
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
        expect(handle_rows == 1, "exactly one handle-method row");
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

    /* close(h): a handle method, and the generation rule behind handle-stale. */
    void close_row_and_handles() {
        const ghostlock::contract::SurfaceResult row = ghostlock::contract::lookup("close");
        expect(row.code == ghostlock::contract::SurfaceLookup::Found && row.entry != nullptr,
               "the close row must exist");
        if (row.entry == nullptr) {
            return;
        }
        expect(row.entry->form == ghostlock::contract::SurfaceForm::HandleMethod, "close is a handle method");
        expect(!row.entry->window_allowed, "close is not a window op");
        expect(row.entry->holds_resource, "a handle keeps its resource alive");
        using ghostlock::contract::HandleCheck;
        expect(ghostlock::contract::handle_use_check(true, 7U, 7U) == HandleCheck::Valid, "matching generation may use the handle");
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
        std::printf("close row: layer=%s, handle rejections %s / %s\n", row.entry->implements_layer,
                    ghostlock::contract::kHandleConflictReason, ghostlock::contract::kHandleStaleReason);
    }
}  // namespace

int main() {
    rows_are_reachable();
    unknown_name_is_named();
    invariants_hold();
    target_kinds();
    close_row_and_handles();
    if (failures == 0) {
        std::printf("surface: PASS (single lookup path + tagged targets + handle method + no run row)\n");
        return 0;
    }
    std::printf("surface: FAIL (%d)\n", failures);
    return 1;
}
