/** @file tests/payload_manifest_test.cpp
 *  The payload family, exported for the other end to consume.
 *
 *  The lists this file prints are facts of the tree -- the segment, the visible
 *  terminals, the four keys and their kinds, the suffix bound -- and the other
 *  end must not hand-copy them. The text is compared byte for byte against a
 *  frozen pin: regeneration is explicit, prints the difference for review, and
 *  then writes the pin, so a change to any list above cannot land silently.
 *
 *  Invoked with --export-only it prints the export and exits 0 without asserting;
 *  otherwise it checks the pin and prints nothing when the two agree.
 */
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "contract/kind.hpp"
#include "profile/payload_keys.hpp"

#ifndef PAYLOAD_MANIFEST_PIN
#define PAYLOAD_MANIFEST_PIN "src/tests/payload-manifest.pin.tsv"
#endif

namespace {
    using ghostlock::contract::ComponentKind;
    using ghostlock::contract::kComponentCount;
    using ghostlock::contract::kComponents;
    using ghostlock::profile::kPayloadKeyCount;
    using ghostlock::profile::kPayloadKeys;
    using ghostlock::profile::kPayloadKindCount;
    using ghostlock::profile::kPayloadKinds;
    using ghostlock::profile::kPayloadSuffixMax;

    constexpr std::size_t kCapacity = 4096U;
    constexpr const char *kSegment = "backend.cve_2026_43499.payload";

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** Build the export text; returns its length. */
    std::size_t build_export(char *out) {
        std::size_t at = 0U;
        const auto line = [out, &at](const char *kind, std::string_view value) noexcept {
            const std::string_view row_kind(kind);
            if (at + row_kind.size() + value.size() + 2U >= kCapacity) {
                return;
            }
            for (const char c : row_kind) {
                out[at++] = c;
            }
            out[at++] = '\t';
            for (const char c : value) {
                out[at++] = c;
            }
            out[at++] = '\n';
            out[at] = '\0';
        };
        line("segment", kSegment);
        for (std::size_t i = 0U; i < kComponentCount; ++i) {
            if (kComponents[i].kind == ComponentKind::Frontend && kComponents[i].selectable) {
                line("terminal", kComponents[i].token);
            }
        }
        for (std::size_t i = 0U; i < kPayloadKeyCount; ++i) {
            line("key", kPayloadKeys[i]);
        }
        for (std::size_t i = 0U; i < kPayloadKindCount; ++i) {
            line("kind", kPayloadKinds[i]);
        }
        char bound[16];
        std::snprintf(bound, sizeof(bound), "%zu", kPayloadSuffixMax);
        line("suffix_max", bound);
        line("bare_name", "rejected");
        return at;
    }

    bool read_pin(char *out, std::size_t *length) {
        std::FILE *file = std::fopen(PAYLOAD_MANIFEST_PIN, "rb");
        if (file == nullptr) {
            return false;
        }
        const std::size_t got = std::fread(out, 1U, kCapacity - 1U, file);
        std::fclose(file);
        out[got] = '\0';
        *length = got;
        return true;
    }

    void report_difference(const char *want, std::size_t want_size, const char *got,
                           std::size_t got_size) {
        std::fprintf(stderr,
                     "FAIL: the pin does not match the export (pin %zu bytes, export %zu bytes)\n",
                     got_size, want_size);
        std::size_t at = 0U;
        while (at < want_size && at < got_size && want[at] == got[at]) {
            ++at;
        }
        std::size_t line = 1U;
        for (std::size_t i = 0U; i < at; ++i) {
            if (want[i] == '\n') {
                ++line;
            }
        }
        std::fprintf(stderr, "  first difference at line %zu: pin has [%.*s], export has [%.*s]\n", line, 24,
                    at < got_size ? got + at : "", 24, at < want_size ? want + at : "");
        std::fprintf(stderr, "  regenerate deliberately: make -C src payload-manifest-regen\n");
        ++g_failures;
    }
}  // namespace

int main(int argc, char **argv) {
    char exported[kCapacity];
    const std::size_t exported_size = build_export(exported);
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--export-only") == 0) {
            std::fwrite(exported, 1U, exported_size, stdout);
            return 0;
        }
    }
    expect(exported_size > 0U, "the export is not empty");
    expect(std::string_view(exported).find(kSegment) != std::string_view::npos,
           "the export names the segment");
    char pinned[kCapacity];
    std::size_t pinned_size = 0U;
    if (!read_pin(pinned, &pinned_size)) {
        std::fprintf(stderr, "FAIL: cannot read the pin file %s\n", PAYLOAD_MANIFEST_PIN);
        return 1;
    }
    if (pinned_size != exported_size) {
        report_difference(exported, exported_size, pinned, pinned_size);
    } else if (std::memcmp(pinned, exported, exported_size) != 0) {
        report_difference(exported, exported_size, pinned, pinned_size);
    }
    /* Diagnostics go to stderr so that stdout is exactly the export in every
     * mode: the artifact rule redirects this program's stdout, and a summary
     * line mixed into it would be pinned as if it were part of the manifest. */
    if (g_failures != 0U) {
        std::fprintf(stderr, "payload-manifest: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::fprintf(stderr, "payload-manifest: PASS (export == pin, %zu bytes)\n", exported_size);
    std::fwrite(exported, 1U, exported_size, stdout);
    return 0;
}
