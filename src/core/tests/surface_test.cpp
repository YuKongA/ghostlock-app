/** @file tests/surface_test.cpp
 *  Evidence for the exposure-surface table: every row is reachable through the
 *  single lookup path (the test iterates the table instead of restating the
 *  names, so it is not a second list), an unknown name is refused by name
 *  (unknown-symbol), and the structural invariants hold at runtime as well.
 *
 *  Build and run: make -C src check
 */
#include <cstddef>
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
        const std::string_view unknown = "not-a-surface-name";
        const ghostlock::contract::SurfaceResult result = ghostlock::contract::lookup(unknown);
        expect(result.code == ghostlock::contract::SurfaceLookup::UnknownSymbol, "unknown name must be refused");
        expect(result.entry == nullptr, "a refused lookup must not return a row");
        expect(std::string_view(ghostlock::contract::surface_lookup_name(result.code)) == "unknown-symbol",
               "the refusal code must be spelled unknown-symbol");
        std::printf("surface lookup: unknown -> %s\n", ghostlock::contract::surface_lookup_name(result.code));
    }

    /* The compile-time invariants, observed at runtime too. */
    void invariants_hold() {
        expect(ghostlock::contract::kSurfaceCount >= 1, "the table must not be empty");
        expect(ghostlock::contract::surface_names_are_unique_and_non_empty(), "names unique and non-empty");
        expect(ghostlock::contract::surface_has_no_window_prefixed_names(),
               "no window.* row may appear in the exposure surface");
        expect(ghostlock::contract::surface_layers_exist(), "layers must be declared");
        expect(ghostlock::contract::surface_args_are_well_formed(), "argument specs well formed");
        expect(ghostlock::contract::surface_flags_are_consistent(), "flags consistent");
        std::size_t value_rows = 0;
        std::size_t required_args = 0;
        for (std::size_t i = 0; i < ghostlock::contract::kSurfaceCount; ++i) {
            const ghostlock::contract::SurfaceEntry &row = ghostlock::contract::kSurface[i];
            if (row.form == ghostlock::contract::SurfaceForm::Value) {
                ++value_rows;
                expect(!row.executable, "a Value row is a read-only snapshot, never executable");
            }
            for (std::size_t a = 0; a < row.arg_count; ++a) {
                if (row.args[a].required) {
                    ++required_args;
                }
            }
        }
        expect(value_rows >= 1, "at least one read-only snapshot row exists");
        expect(required_args >= 1, "at least one required argument is declared");
        std::printf("surface table: %zu rows (%zu value, %zu required args), invariants hold\n",
                    ghostlock::contract::kSurfaceCount, value_rows, required_args);
    }
}  // namespace

int main() {
    rows_are_reachable();
    unknown_name_is_named();
    invariants_hold();
    if (failures == 0) {
        std::printf("surface: PASS (single lookup path + named unknown-symbol + invariants)\n");
        return 0;
    }
    std::printf("surface: FAIL (%d)\n", failures);
    return 1;
}
