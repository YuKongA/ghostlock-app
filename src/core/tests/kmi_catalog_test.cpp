/** @file tests/kmi_catalog_test.cpp
 *  The KMI labels, and the release that wants each one.
 *
 *  Sections: 1. the catalogue is the one the other end already reads: every row
 *  is compared against the manifest that is shipped, so a label added here and
 *  not there -- or the other way round -- is caught; 2. the derivation matches the
 *  rule the other end applies, including on the release this device reports;
 *  3. a release that carries neither number gets no label.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "contract/kmi_catalog.hpp"

#ifndef KMI_MANIFEST
#define KMI_MANIFEST "../profile-core/src/main/resources/lkm-kmi-manifest.tsv"
#endif

namespace {
    using ghostlock::contract::kKmiCatalog;
    using ghostlock::contract::kKmiCount;
    using ghostlock::contract::kmi_of;
    using ghostlock::contract::KmiEntry;
    using ghostlock::contract::label_for_release;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    bool label_of(const char *release, char *out, std::size_t capacity) {
        return label_for_release(release, out, capacity);
    }

    /* ---- 1: the catalogue is the one that is shipped ---- */
    void the_catalogue_matches_the_manifest() {
        std::FILE *file = std::fopen(KMI_MANIFEST, "rb");
        if (file == nullptr) {
            std::printf("FAIL: cannot read %s\n", KMI_MANIFEST);
            ++g_failures;
            return;
        }
        char line[512];
        std::size_t rows = 0U;
        while (std::fgets(line, static_cast<int>(sizeof(line)), file) != nullptr) {
            if (line[0U] == '#' || line[0U] == '\n' || line[0U] == '\0') {
                continue;
            }
            char label[64] = {};
            unsigned android = 0U;
            unsigned kmi = 0U;
            if (std::sscanf(line, "%63[^\t]\t%u\t%u", label, &android, &kmi) != 3) {
                continue;
            }
            expect(rows < kKmiCount, "the manifest has no row the catalogue does not");
            if (rows >= kKmiCount) {
                break;
            }
            expect(std::strcmp(kKmiCatalog[rows].label, label) == 0,
                   "the labels are in the same order in both places");
            expect(kKmiCatalog[rows].android == android, "and carry the same generation");
            expect(kKmiCatalog[rows].kmi == kmi, "and the same interface version");
            ++rows;
        }
        std::fclose(file);
        expect(rows == kKmiCount, "the manifest has a row for every label");
    }

    /* ---- 2: the derivation ---- */
    void the_derivation_holds() {
        char label[64];
        /* The release this device reports. */
        expect(label_of("5.15.189-android13-8-00016-g51bba4309aac-ab14546557", label,

                        sizeof(label)),
               "the device's release yields a label");
        expect(std::strcmp(label, "android13-5.15") == 0, "and it is the one the manifest has");
        expect(kmi_of(std::string_view(label)) != nullptr, "which the catalogue carries");
        expect(label_of("6.6.118-android15-8-g93e223c276e7-abogki500782043-4k", label,

                        sizeof(label)) &&
                   std::strcmp(label, "android15-6.6") == 0,
               "another release yields its own label");
        expect(label_of("6.1.145-android14-11-maybe-dirty", label, sizeof(label)) &&
                   std::strcmp(label, "android14-6.1") == 0,
               "and a release with a dirty suffix yields the same label as a clean one");
        expect(label_of("5.10.209-android12-9-00001-gdeadbeef-4k", label, sizeof(label)) &&
                   std::strcmp(label, "android12-5.10") == 0,
               "and 5.10 keeps its generation, though it shares the interface version");
        /* Two labels, one interface version: the label is the delivery identity. */
        expect(kmi_of(std::string_view("android12-5.10")) != nullptr &&
                   kmi_of(std::string_view("android13-5.10")) != nullptr &&
                   kmi_of(std::string_view("android12-5.10"))->kmi ==

                       kmi_of(std::string_view("android13-5.10"))->kmi,
               "the two 5.10 labels share a version and stay two labels");
    }

    /* ---- 3: what gets no label ---- */
    void no_label_without_both_numbers() {
        char label[64];
        expect(!label_of("5.15.189", label, sizeof(label)), "a release with no generation gets none");
        expect(!label_of("android13-8-00016", label, sizeof(label)),

               "and one with no kernel version either");
        expect(!label_of("", label, sizeof(label)), "nor does an empty release");
        expect(!label_of(nullptr, label, sizeof(label)), "nor a missing one");
        expect(!label_of("5.15.189-android13-8", label, 4U),

               "and a label that does not fit is refused rather than truncated");
        expect(kmi_of(std::string_view("android99-9.99")) == nullptr,

               "a label the catalogue does not carry is not a label");
    }
}  // namespace

int main() {
    std::printf("kmi: the catalogue\n");
    the_catalogue_matches_the_manifest();
    std::printf("kmi: the derivation\n");
    the_derivation_holds();
    std::printf("kmi: no label\n");
    no_label_without_both_numbers();
    if (g_failures != 0U) {
        std::printf("kmi: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("kmi: PASS (catalogue matches the shipped manifest + derivation + no label)\n");
    return 0;
}
