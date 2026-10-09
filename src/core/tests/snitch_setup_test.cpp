/** @file tests/snitch_setup_test.cpp
 *  What the search derives, and what it refuses to derive.
 *
 *  The refusals carry the weight here. The object size is the profile's own -- the
 *  retired tree had a default for it in code and this tree does not -- so a setup
 *  without it is refused rather than stepped over at some guessed size. The same goes
 *  for a page size nobody measured, a collision count with nothing to compare, and a
 *  slab order whose shift would lose bits: each one is named, and none of them leaves a
 *  half-filled setup behind.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "backend/cve_2026_43499/leak/snitch_setup.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::build_snitch_setup;
    using ghostlock::backend::cve_2026_43499::leak::kCoarseStride;
    using ghostlock::backend::cve_2026_43499::leak::SnitchInputs;
    using ghostlock::backend::cve_2026_43499::leak::SnitchSetup;
    using ghostlock::backend::cve_2026_43499::leak::snitch_setup_verdict_name;
    using ghostlock::backend::cve_2026_43499::leak::SnitchSetupVerdict;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    SnitchInputs usable() {
        SnitchInputs inputs;
        inputs.page_size = 4096U;
        inputs.mm_struct_size = 512U;
        inputs.mm_slab_order = 3U;
        inputs.collisions = 2U;
        inputs.thread_count = 4U;
        inputs.possible_cpus = 8U;
        return inputs;
    }

    void the_shape_it_derives() {
        SnitchSetup setup;
        expect(build_snitch_setup(usable(), setup) == SnitchSetupVerdict::Ok,
               "a full set of inputs builds a setup");
        expect(setup.strides.coarse == kCoarseStride,
               "the coarse stride is the mechanism own, not the device's");
        expect(setup.strides.slab == 4096U << 3U, "the slab stride is a page shifted by the order");
        expect(setup.strides.candidate == 512U,
               "and the candidate stride is the object size the profile declared");
        expect(setup.collisions == 2U && setup.thread_count == 4U, "with the counts it was given");
        expect(setup.futex_table_size == 2048U, "and the table the kernel sizes for eight processors");
    }

    void what_it_refuses_to_derive() {
        SnitchSetup setup;
        SnitchInputs no_object = usable();
        no_object.mm_struct_size = 0U;
        expect(build_snitch_setup(no_object, setup) == SnitchSetupVerdict::GeometryMissing,
               "no object size is no search: the profile has to say how large it is");
        SnitchInputs no_page = usable();
        no_page.page_size = 0U;
        expect(build_snitch_setup(no_page, setup) == SnitchSetupVerdict::GeometryMissing,
               "nor is an unmeasured page size");
        SnitchInputs one_collision = usable();
        one_collision.collisions = 1U;
        expect(build_snitch_setup(one_collision, setup) == SnitchSetupVerdict::GeometryMissing,
               "one collision compares nothing, which the matcher already enforces");
        SnitchInputs no_threads = usable();
        no_threads.thread_count = 0U;
        expect(build_snitch_setup(no_threads, setup) == SnitchSetupVerdict::GeometryMissing,
               "and a search with no thread to run in");
        SnitchInputs no_cpus = usable();
        no_cpus.possible_cpus = 0U;
        expect(build_snitch_setup(no_cpus, setup) == SnitchSetupVerdict::GeometryMissing,
               "nor one with no processor count to size the table from");
        SnitchInputs wide_order = usable();
        wide_order.mm_slab_order = 63U;
        expect(build_snitch_setup(wide_order, setup) == SnitchSetupVerdict::GeometryMissing,
               "an order whose shift would lose bits is refused, not shifted");
        SnitchInputs huge_page = usable();
        huge_page.page_size = UINT64_MAX;
        expect(build_snitch_setup(huge_page, setup) == SnitchSetupVerdict::GeometryMissing,
               "as is a page size that would overflow the shift");
        expect(setup.strides.slab == 0U && setup.futex_table_size == 0U,
               "and a refusal leaves nothing half-filled behind");
        expect(std::string_view(snitch_setup_verdict_name(SnitchSetupVerdict::GeometryMissing)) ==
                   std::string_view("geometry-missing"),
               "each refusal under the geometry name");
    }
}  // namespace

int main() {
    std::printf("snitch setup: what it derives\n");
    the_shape_it_derives();
    std::printf("snitch setup: what it refuses\n");
    what_it_refuses_to_derive();
    if (g_failures != 0U) {
        std::printf("snitch setup: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("snitch setup: PASS (the shape derived, the missing inputs named)\n");
    return 0;
}
