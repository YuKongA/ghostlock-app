/** @file tests/surface_manifest_test.cpp
 *  Bare-run export + pin for the exposure surface (the manifest pattern this
 *  repository already uses for combination tokens, vocabulary and step sets).
 *
 *  It prints the surface manifest as TSV /
 *    name<TAB>form<TAB>implements_layer<TAB>phase<TAB>holds_resource<TAB>
 *    recordable<TAB>executable<TAB>arg_key<TAB>arg_required<TAB>arg_ceiling
 *  one line per argument (a row without arguments prints one line with "-"),
 *  then asserts the export is byte-identical to the PIN FILE named by
 *  SURFACE_MANIFEST_PIN plus the structural invariants of the table.
 *
 *  The pin is a GENERATED, human-reviewed file -- never hand-edited:
 *    make -C src surface-manifest         verify (default path, runs this)
 *    make -C src surface-manifest-regen   print the diff, then write the new pin
 *  Regeneration happens only when asked for; check/host never rewrite the pin,
 *  and the printed diff must be reviewed and committed with the change.
 *
 *  Invoked with --export-only it prints the export and exits 0 without asserting
 *  (that is the mode the regeneration target uses).
 *
 *  phase is a STORED column (stage / pre_window / window / terminal) carried
 *  straight from the table, so a document that reproduces this manifest states
 *  the same phase as the code.
 *
 *  Build and run: make -C src surface-manifest
 */
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "contract/script_surface.hpp"

namespace {
    /* The pin is a GENERATED, human-reviewed data file -- never hand-edited:
     *   make -C src surface-manifest         verify (this path)
     *   make -C src surface-manifest-regen   print the diff, then write the pin
     * Regeneration is explicit only; check/host never rewrite the pin. */
#ifndef SURFACE_MANIFEST_PIN
#define SURFACE_MANIFEST_PIN "core/tests/surface-manifest.pin.tsv"
#endif

    /* Read the pin file into a caller buffer; returns the bytes read (0 on
     * failure). Header-only helper: no allocation, no exceptions. */
    std::size_t read_pin(char *buffer, std::size_t capacity) {
        std::FILE *file = std::fopen(SURFACE_MANIFEST_PIN, "rb");
        if (file == nullptr) {
            return 0;
        }
        const std::size_t read = std::fread(buffer, 1, capacity - 1, file);
        std::fclose(file);
        buffer[read] = '\0';
        return read;
    }

    constexpr std::size_t kBufferBytes = 16384;

    char g_text[kBufferBytes];
    std::size_t g_used = 0;

    bool append(const char *piece) {
        const std::size_t length = std::strlen(piece);
        if (g_used + length + 1 >= kBufferBytes) {
            return false;
        }
        std::memcpy(g_text + g_used, piece, length);
        g_used += length;
        g_text[g_used] = '\0';
        return true;
    }

    bool append_line(const char *name, const char *form, const char *layer, const char *phase, bool holds,
                     bool recordable, bool executable, const char *arg_key, const char *arg_required,
                     const char *ceiling) {
        char line[256];
        std::snprintf(line, sizeof(line), "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", name, form, layer,
                      phase, holds ? "true" : "false", recordable ? "true" : "false",
                      executable ? "true" : "false", arg_key, arg_required, ceiling);
        return append(line);
    }

    const char *form_name(ghostlock::contract::SurfaceForm form) {
        switch (form) {
            case ghostlock::contract::SurfaceForm::Call: return "call";
            case ghostlock::contract::SurfaceForm::Value: return "value";
            case ghostlock::contract::SurfaceForm::HandleMethod: return "handle_method";
        }
        return "unknown";
    }

    bool export_row(const ghostlock::contract::SurfaceEntry &row) {
        const char *form = form_name(row.form);
        const char *phase = ghostlock::contract::surface_phase_name(row.phase);
        if (row.arg_count == 0) {
            return append_line(row.name, form, row.implements_layer, phase, row.holds_resource, row.recordable,
                               row.executable, "-", "-", "-");
        }
        for (std::size_t a = 0; a < row.arg_count; ++a) {
            char ceiling[32];
            std::snprintf(ceiling, sizeof(ceiling), "%llu",
                          static_cast<unsigned long long>(row.args[a].max));
            if (!append_line(row.name, form, row.implements_layer, phase, row.holds_resource, row.recordable,
                             row.executable, row.args[a].key, row.args[a].required ? "true" : "false", ceiling)) {
                return false;
            }
        }
        return true;
    }
}  // namespace

int main(int argc, char **argv) {
    bool export_only = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--export-only") == 0) {
            export_only = true;
        }
    }
    std::size_t lines = 0;
    bool emitted = true;
    for (std::size_t i = 0; i < ghostlock::contract::kSurfaceCount; ++i) {
        const ghostlock::contract::SurfaceEntry &row = ghostlock::contract::kSurface[i];
        emitted = export_row(row);
        lines += row.arg_count == 0 ? 1 : row.arg_count;
        if (!emitted) {
            break;
        }
    }
    std::fputs(g_text, stdout);
    std::fflush(stdout);
    if (export_only) {
        std::fprintf(stderr, "surface-manifest: exported %zu rows / %zu lines (pin not checked)\n",
                     ghostlock::contract::kSurfaceCount, lines);
        return 0;
    }

    int failures = 0;
    if (!emitted) {
        std::fprintf(stderr, "FAIL: the manifest buffer overflowed\n");
        ++failures;
    }
    const bool invariants =
        ghostlock::contract::kSurfaceCount >= 1 &&
        ghostlock::contract::surface_names_are_unique_and_non_empty() &&
        ghostlock::contract::surface_has_no_window_prefixed_names() &&
        ghostlock::contract::surface_has_no_run_row() && ghostlock::contract::surface_layers_exist() &&
        ghostlock::contract::surface_args_are_well_formed() &&
        ghostlock::contract::surface_flags_are_consistent() && ghostlock::contract::widths_are_required() &&
        ghostlock::contract::handoff_carries_terminal_and_payload_ref() &&
        ghostlock::contract::operations_are_handle_methods() &&
        ghostlock::contract::resource_semantics_are_pinned() &&
        ghostlock::contract::lookup_refusal_is_pinned() &&
        ghostlock::contract::phase_names_are_closed() &&
        ghostlock::contract::phases_are_assigned_per_design();
    if (!invariants) {
        std::fprintf(stderr, "FAIL: a surface invariant does not hold\n");
        ++failures;
    }
    char pin[kBufferBytes];
    const std::size_t pin_bytes = read_pin(pin, sizeof(pin));
    if (pin_bytes == 0) {
        std::fprintf(stderr, "FAIL: cannot read the pin file %s\n", SURFACE_MANIFEST_PIN);
        ++failures;
    } else if (std::strcmp(g_text, pin) != 0) {
        std::fprintf(stderr,
                     "FAIL: the export differs from the pin %s (run surface-manifest-regen after review)\n",
                     SURFACE_MANIFEST_PIN);
        ++failures;
    }
    std::fprintf(stderr, "surface-manifest: %zu rows, %zu manifest lines, pin %zu bytes\n",
                 ghostlock::contract::kSurfaceCount, lines, pin_bytes);
    /* Diagnostics stay on stderr so that stdout is EXACTLY the manifest and can
     * be redirected into surface-manifest.tsv without filtering. */
    if (failures != 0) {
        std::fprintf(stderr, "surface-manifest: FAIL (%d)\n", failures);
        return 1;
    }
    std::fprintf(stderr, "surface-manifest: PASS (%zu rows exported, byte-identical to the pin)\n",
                 ghostlock::contract::kSurfaceCount);
    return 0;
}
